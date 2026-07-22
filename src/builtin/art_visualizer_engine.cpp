#include "builtin/art_visualizer_engine.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace vizrack::builtin {
namespace {

constexpr float kPi = std::numbers::pi_v<float>;

constexpr std::array<std::string_view, ArtVisualizerEngine::kSceneCount> kSceneNames{
    "PHASE ORBIT", "NEON BLOOM", "SIGNAL HORIZON",
    "PRISM TUNNEL", "PULSE MATRIX", "RADIANT BARS",
};

constexpr std::array<ArtPalette, ArtVisualizerEngine::kPaletteCount> kPalettes{{
    {"NEON",   0x060d18, 0x0c0519, 0x3aecd6, 0x6976ff, 0xf64daf},
    {"AURORA", 0x031713, 0x071126, 0x59f19b, 0x37b9ff, 0xb66cff},
    {"SUNSET", 0x210810, 0x10091f, 0xff5d73, 0xffa45b, 0xb36cff},
    {"OCEAN",  0x031522, 0x02080f, 0x35d7ff, 0x3176ff, 0x4ff0c8},
    {"MONO",   0x111418, 0x030405, 0xf2f5f7, 0xa6b0b8, 0x64707a},
    {"EMBER",  0x1c0702, 0x080302, 0xffc04d, 0xff633d, 0xd83250},
}};

float finiteSample(float value) noexcept {
    return std::isfinite(value) ? value : 0.0f;
}

float clampUnit(float value) noexcept {
    return std::clamp(value, 0.0f, 1.0f);
}

float frameFollow(float current, float target, float attack, float release,
                  float frameScale) noexcept {
    const float base = target > current ? attack : release;
    const float coefficient = 1.0f - std::pow(1.0f - base, frameScale);
    return current + (target - current) * coefficient;
}

} // namespace

ArtVisualizerEngine::ArtVisualizerEngine() {
    scratchA_.reserve(720);
    scratchB_.reserve(720);
}

void ArtVisualizerEngine::setOptions(ArtVisualizerOptions options) noexcept {
    options.scene = std::clamp(options.scene, 0, kSceneCount - 1);
    options.palette = std::clamp(options.palette, 0, kPaletteCount - 1);
    options_ = options;
}

void ArtVisualizerEngine::setSampleRate(uint32_t sampleRate) noexcept {
    if (sampleRate >= 8000 && sampleRate <= 768000) {
        sampleRate_.store(sampleRate, std::memory_order_release);
    }
}

void ArtVisualizerEngine::update(size_t sampleCount, float frameSeconds) noexcept {
    const float safeFrameSeconds = std::isfinite(frameSeconds) ? frameSeconds : 1.0f / 60.0f;
    const float frameScale = std::clamp(safeFrameSeconds * 60.0f, 0.25f, 4.0f);
    if (sampleCount > 0) {
        sampleCount_ = std::min(sampleCount, kMaxSamples);
        analyzeSamples(frameScale);
    } else {
        lowLevel_ *= std::pow(0.95f, frameScale);
        midLevel_ *= std::pow(0.94f, frameScale);
        highLevel_ *= std::pow(0.92f, frameScale);
        stereoLevel_ *= std::pow(0.95f, frameScale);
    }
    phase_ = std::fmod(phase_ +
                           (0.008f + lowLevel_ * 0.025f + highLevel_ * 0.008f) * frameScale,
                       kPi * 200.0f);
}

void ArtVisualizerEngine::reset() noexcept {
    sampleCount_ = 0;
    lowFilter_ = 0.0f;
    midFilter_ = 0.0f;
    lowLevel_ = 0.0f;
    midLevel_ = 0.0f;
    highLevel_ = 0.0f;
    stereoLevel_ = 0.0f;
    phase_ = 0.0f;
}

void ArtVisualizerEngine::analyzeSamples(float frameScale) noexcept {
    const float rate = static_cast<float>(sampleRate_.load(std::memory_order_acquire));
    const float lowCoefficient = 1.0f - std::exp(-2.0f * kPi * 180.0f / rate);
    const float midCoefficient = 1.0f - std::exp(-2.0f * kPi * 2200.0f / rate);
    double lowEnergy = 0.0;
    double midEnergy = 0.0;
    double highEnergy = 0.0;
    double sideEnergy = 0.0;
    for (size_t index = 0; index < sampleCount_; ++index) {
        const float left = finiteSample(left_[index]);
        const float right = finiteSample(right_[index]);
        const float mono = (left + right) * 0.5f;
        lowFilter_ += lowCoefficient * (mono - lowFilter_);
        midFilter_ += midCoefficient * (mono - midFilter_);
        const float low = lowFilter_;
        const float mid = midFilter_ - lowFilter_;
        const float high = mono - midFilter_;
        const float side = (left - right) * 0.5f;
        lowEnergy += static_cast<double>(low) * low;
        midEnergy += static_cast<double>(mid) * mid;
        highEnergy += static_cast<double>(high) * high;
        sideEnergy += static_cast<double>(side) * side;
    }
    const float divisor = static_cast<float>(std::max<size_t>(1, sampleCount_));
    const float lowTarget = clampUnit(std::sqrt(static_cast<float>(lowEnergy) / divisor) * 5.0f);
    const float midTarget = clampUnit(std::sqrt(static_cast<float>(midEnergy) / divisor) * 7.0f);
    const float highTarget = clampUnit(std::sqrt(static_cast<float>(highEnergy) / divisor) * 10.0f);
    const float sideTarget = clampUnit(std::sqrt(static_cast<float>(sideEnergy) / divisor) * 7.0f);
    lowLevel_ = frameFollow(lowLevel_, lowTarget, 0.32f, 0.075f, frameScale);
    midLevel_ = frameFollow(midLevel_, midTarget, 0.32f, 0.075f, frameScale);
    highLevel_ = frameFollow(highLevel_, highTarget, 0.32f, 0.075f, frameScale);
    stereoLevel_ = frameFollow(stereoLevel_, sideTarget, 0.32f, 0.075f, frameScale);
}

float ArtVisualizerEngine::averageMono(size_t center, size_t radius) const noexcept {
    if (sampleCount_ == 0) return 0.0f;
    const size_t first = center > radius ? center - radius : 0;
    const size_t last = std::min(sampleCount_ - 1, center + radius);
    float sum = 0.0f;
    for (size_t index = first; index <= last; ++index) {
        sum += (finiteSample(left_[index]) + finiteSample(right_[index])) * 0.5f;
    }
    return sum / static_cast<float>(last - first + 1);
}

void ArtVisualizerEngine::fillBackground(DrawList& output, float width, float height) const {
    const auto colors = palette(options_.palette);
    output.addVerticalGradient(0.0f, 0.0f, width, height,
                               color(colors.top), color(colors.bottom));
    for (int division = 1; division < 8; ++division) {
        const float x = width * static_cast<float>(division) / 8.0f;
        output.addLine(x, 0.0f, x, height, color(colors.secondary, 18), 1.0f);
    }
    for (int division = 1; division < 6; ++division) {
        const float y = height * static_cast<float>(division) / 6.0f;
        output.addLine(0.0f, y, width, y, color(colors.secondary, 18), 1.0f);
    }
}

void ArtVisualizerEngine::addGlowPolyline(DrawList& output, std::span<const Point> points,
                                           Color value, float width) {
    if (points.size() < 2) return;
    const auto range = output.appendPoints(points);
    output.addPolyline(range, color(value.rgb, 24), width + 10.0f);
    output.addPolyline(range, color(value.rgb, 70), width + 4.0f);
    output.addPolyline(range, color(value.rgb, 225), width);
}

void ArtVisualizerEngine::drawPhaseOrbit(DrawList& output, float width, float height) {
    const auto colors = palette(options_.palette);
    fillBackground(output, width, height);
    const float extent = std::min(width, height);
    const Point center{width * 0.5f, height * 0.52f};
    const float radius = extent * (0.31f + lowLevel_ * 0.025f);
    const std::array<uint32_t, 3> rings{colors.primary, colors.secondary, colors.tertiary};
    for (int ring = 0; ring < 3; ++ring) {
        const float ringRadius = radius * (0.54f + static_cast<float>(ring) * 0.23f);
        const float level = ring == 0 ? lowLevel_ : (ring == 1 ? midLevel_ : highLevel_);
        const float start = phase_ * (ring % 2 == 0 ? 20.0f : -15.0f) + ring * 70.0f;
        const float sweep = 175.0f + level * 135.0f;
        const float x = center.x - ringRadius;
        const float y = center.y - ringRadius;
        output.addArc(x, y, ringRadius * 2.0f, ringRadius * 2.0f, start, sweep,
                      color(rings[ring], 24), 8.0f + level * 5.0f);
        output.addArc(x, y, ringRadius * 2.0f, ringRadius * 2.0f, start, sweep,
                      color(rings[ring], 105), 1.2f + level * 2.0f);
        output.addArc(x, y, ringRadius * 2.0f, ringRadius * 2.0f, start + 190.0f,
                      82.0f + level * 65.0f, color(rings[ring], 105), 1.2f + level * 2.0f);
    }
    if (sampleCount_ >= 2) {
        const size_t pointCount = std::min<size_t>(420, sampleCount_);
        float peak = 0.025f;
        for (size_t index = 0; index < sampleCount_; ++index) {
            peak = std::max(peak, std::abs(finiteSample(left_[index])));
            peak = std::max(peak, std::abs(finiteSample(right_[index])));
        }
        const float gain = radius * 0.82f / peak;
        const float rotation = std::sin(phase_ * 0.37f) * (0.05f + stereoLevel_ * 0.12f);
        const float cosine = std::cos(rotation);
        const float sine = std::sin(rotation);
        scratchA_.clear();
        for (size_t point = 0; point < pointCount; ++point) {
            const size_t index = point * (sampleCount_ - 1) / (pointCount - 1);
            const float x = (finiteSample(left_[index]) - finiteSample(right_[index])) *
                            0.7071068f * gain;
            const float y = -(finiteSample(left_[index]) + finiteSample(right_[index])) *
                            0.7071068f * gain;
            scratchA_.push_back({center.x + x * cosine - y * sine,
                                 center.y + x * sine + y * cosine});
        }
        addGlowPolyline(output, scratchA_, color(colors.primary), 1.35f);
    }
    const float coreRadius = 3.0f + lowLevel_ * extent * 0.018f;
    output.addFillEllipse(center.x - coreRadius * 4.0f, center.y - coreRadius * 4.0f,
                          coreRadius * 8.0f, coreRadius * 8.0f, color(colors.primary, 38));
    output.addFillEllipse(center.x - coreRadius, center.y - coreRadius,
                          coreRadius * 2.0f, coreRadius * 2.0f, color(colors.primary, 230));
}

void ArtVisualizerEngine::drawNeonBloom(DrawList& output, float width, float height) {
    const auto colors = palette(options_.palette);
    fillBackground(output, width, height);
    const float extent = std::min(width, height);
    const Point center{width * 0.5f, height * 0.52f};
    const std::array<float, 3> levels{lowLevel_, midLevel_, highLevel_};
    const std::array<uint32_t, 3> lines{colors.tertiary, colors.secondary, colors.primary};
    constexpr size_t pointCount = 181;
    for (int layer = 0; layer < 3; ++layer) {
        scratchA_.clear();
        const float base = extent * (0.12f + static_cast<float>(layer) * 0.058f);
        const float response = extent * (0.035f + levels[layer] * 0.11f);
        for (size_t point = 0; point < pointCount; ++point) {
            const float angle = static_cast<float>(point) / static_cast<float>(pointCount - 1) *
                                2.0f * kPi;
            float sample = 0.0f;
            if (sampleCount_ > 0) {
                const size_t index = point * (sampleCount_ - 1) / (pointCount - 1);
                sample = (finiteSample(left_[index]) + finiteSample(right_[index])) * 0.5f;
            }
            const float petals = std::sin(angle * static_cast<float>(3 + layer * 2) +
                                          phase_ * (1.1f + layer * 0.23f));
            const float radius = base + response * (0.42f + sample * 2.1f) +
                                 extent * 0.012f * petals;
            scratchA_.push_back({center.x + std::cos(angle) * radius,
                                 center.y + std::sin(angle) * radius});
        }
        addGlowPolyline(output, scratchA_, color(lines[layer]),
                        1.15f + levels[layer] * 1.8f);
    }
    constexpr float goldenAngle = 2.3999632f;
    for (int particle = 0; particle < 54; ++particle) {
        const float seed = static_cast<float>(particle);
        const float angle = seed * goldenAngle + phase_ * (0.13f + (particle % 3) * 0.035f);
        const float orbit = extent * (0.23f + 0.17f *
            (0.5f + 0.5f * std::sin(seed * 1.73f + phase_ * 0.31f)));
        const float size = (1.0f + static_cast<float>(particle % 3)) *
                           (1.0f + highLevel_ * (0.5f + 0.5f * std::sin(seed)));
        const float x = center.x + std::cos(angle) * orbit;
        const float y = center.y + std::sin(angle) * orbit;
        output.addFillEllipse(x - size, y - size, size * 2.0f, size * 2.0f,
                              color(colors.secondary,
                                    static_cast<uint8_t>(55 + highLevel_ * 150.0f)));
    }
}

void ArtVisualizerEngine::drawSignalHorizon(DrawList& output, float width, float height) {
    const auto colors = palette(options_.palette);
    fillBackground(output, width, height);
    const float horizon = height * 0.55f;
    const std::array<float, 3> levels{lowLevel_, midLevel_, highLevel_};
    const std::array<uint32_t, 3> lines{colors.tertiary, colors.secondary, colors.primary};
    output.addLine(0.0f, horizon, width, horizon, color(colors.secondary, 65), 1.0f);
    const size_t pointCount = static_cast<size_t>(std::clamp(width, 96.0f, 720.0f));
    std::array<float, 720> broadSamples;
    std::array<float, 720> mediumSamples;
    std::array<float, 720> rawSamples;
    for (size_t point = 0; point < pointCount; ++point) {
        const size_t index = sampleCount_ > 1 ? point * (sampleCount_ - 1) /
                                                   (pointCount - 1) : 0;
        broadSamples[point] = averageMono(index, 44);
        mediumSamples[point] = averageMono(index, 8);
        rawSamples[point] = averageMono(index, 0);
    }
    for (int layer = 0; layer < 3; ++layer) {
        scratchA_.clear();
        scratchB_.clear();
        for (size_t point = 0; point < pointCount; ++point) {
            const float unit = static_cast<float>(point) / static_cast<float>(pointCount - 1);
            const float broad = broadSamples[point];
            const float medium = mediumSamples[point];
            const float raw = rawSamples[point];
            const float band = layer == 0 ? broad : (layer == 1 ? medium - broad : raw - medium);
            const float idle = std::sin(unit * kPi * (4.0f + layer * 3.0f) +
                                        phase_ * (0.7f + layer * 0.35f));
            const float amplitude = height * (0.055f + levels[layer] * 0.12f);
            const float displacement = std::clamp(band * (layer == 0 ? 6.0f : 10.0f),
                                                  -1.0f, 1.0f) * amplitude +
                                       idle * height * 0.008f;
            scratchA_.push_back({unit * width, horizon - displacement});
            scratchB_.push_back({unit * width, horizon + displacement});
        }
        addGlowPolyline(output, scratchA_, color(lines[layer]), 1.0f + levels[layer] * 2.2f);
        addGlowPolyline(output, scratchB_, color(lines[layer], 150),
                        0.8f + levels[layer] * 1.5f);
    }
}

void ArtVisualizerEngine::drawPrismTunnel(DrawList& output, float width, float height) {
    const auto colors = palette(options_.palette);
    fillBackground(output, width, height);
    const float extent = std::min(width, height);
    const Point center{width * 0.5f + std::sin(phase_ * 0.23f) * width * 0.035f,
                       height * 0.53f + std::cos(phase_ * 0.19f) * height * 0.025f};
    const std::array<uint32_t, 3> lines{colors.primary, colors.secondary, colors.tertiary};
    constexpr int sides = 6;
    for (int ring = 0; ring < 13; ++ring) {
        const float travel = std::fmod(static_cast<float>(ring) / 13.0f + phase_ * 0.035f, 1.0f);
        const float radius = extent * (0.035f + travel * travel * 0.57f) *
                             (1.0f + lowLevel_ * 0.08f);
        scratchA_.clear();
        const float rotation = phase_ * (ring % 2 ? -0.11f : 0.08f) + travel * 0.7f;
        for (int side = 0; side < sides; ++side) {
            const float angle = rotation + 2.0f * kPi * static_cast<float>(side) / sides;
            scratchA_.push_back({center.x + std::cos(angle) * radius,
                                 center.y + std::sin(angle) * radius});
        }
        const auto range = output.appendPoints(scratchA_);
        output.addStrokePolygon(range,
                                color(lines[ring % 3], static_cast<uint8_t>(35 + travel * 175.0f)),
                                0.8f + travel * 2.0f + midLevel_ * 1.5f);
    }
    const float coreSize = 3.0f + lowLevel_ * 16.0f;
    output.addFillEllipse(center.x - coreSize, center.y - coreSize,
                          coreSize * 2.0f, coreSize * 2.0f,
                          color(colors.primary, static_cast<uint8_t>(90 + lowLevel_ * 140.0f)));
}

void ArtVisualizerEngine::drawPulseMatrix(DrawList& output, float width, float height) {
    const auto colors = palette(options_.palette);
    fillBackground(output, width, height);
    const int columns = std::clamp(static_cast<int>(width / 34.0f), 12, 28);
    const int rows = std::clamp(static_cast<int>(height / 34.0f), 9, 20);
    const float cellWidth = width / static_cast<float>(columns);
    const float cellHeight = height / static_cast<float>(rows);
    const std::array<uint32_t, 3> dots{colors.primary, colors.secondary, colors.tertiary};
    std::array<float, 28> columnSamples;
    for (int column = 0; column < columns; ++column) {
        const float unit = (static_cast<float>(column) + 0.5f) / columns;
        const size_t index = sampleCount_ > 1
                                 ? static_cast<size_t>(unit * (sampleCount_ - 1)) : 0;
        columnSamples[static_cast<size_t>(column)] = std::abs(averageMono(index, 5));
    }
    for (int row = 1; row < rows - 1; ++row) {
        for (int column = 0; column < columns; ++column) {
            const float sample = columnSamples[static_cast<size_t>(column)];
            const float wave = 0.5f + 0.5f * std::sin(column * 0.58f + row * 0.42f -
                                                      phase_ * 2.2f);
            const float level = clampUnit(sample * 7.0f + wave * 0.20f +
                                          (row % 3 == 0 ? highLevel_ : midLevel_) * 0.42f);
            const float radius = 1.0f + level * std::min(cellWidth, cellHeight) * 0.32f;
            const float x = (column + 0.5f) * cellWidth;
            const float y = (row + 0.5f) * cellHeight;
            const uint32_t rgb = dots[(row + column) % 3];
            output.addFillEllipse(x - radius * 2.2f, y - radius * 2.2f,
                                  radius * 4.4f, radius * 4.4f,
                                  color(rgb, static_cast<uint8_t>(18 + level * 50.0f)));
            output.addFillEllipse(x - radius, y - radius, radius * 2.0f, radius * 2.0f,
                                  color(rgb, static_cast<uint8_t>(80 + level * 170.0f)));
        }
    }
}

void ArtVisualizerEngine::drawRadiantBars(DrawList& output, float width, float height) {
    const auto colors = palette(options_.palette);
    fillBackground(output, width, height);
    const float extent = std::min(width, height);
    const Point center{width * 0.5f, height * 0.53f};
    const float inner = extent * (0.15f + lowLevel_ * 0.025f);
    constexpr int bars = 96;
    const std::array<uint32_t, 3> lines{colors.primary, colors.secondary, colors.tertiary};
    for (int bar = 0; bar < bars; ++bar) {
        const float unit = static_cast<float>(bar) / bars;
        const float angle = unit * 2.0f * kPi - kPi * 0.5f + phase_ * 0.055f;
        const size_t index = sampleCount_ > 1
                                 ? static_cast<size_t>(unit * (sampleCount_ - 1)) : 0;
        const float sample = std::abs(averageMono(index, 4));
        const float band = bar % 3 == 0 ? lowLevel_ : (bar % 3 == 1 ? midLevel_ : highLevel_);
        const float length = extent *
            (0.018f + clampUnit(sample * 8.0f + band * 0.55f) * 0.18f);
        const float cosine = std::cos(angle);
        const float sine = std::sin(angle);
        const float x1 = center.x + cosine * inner;
        const float y1 = center.y + sine * inner;
        const float x2 = center.x + cosine * (inner + length);
        const float y2 = center.y + sine * (inner + length);
        output.addLine(x1, y1, x2, y2, color(lines[bar % 3], 35), 6.0f, true);
        output.addLine(x1, y1, x2, y2, color(lines[bar % 3], 220),
                       1.2f + band * 2.5f, true);
    }
    output.addStrokeEllipse(center.x - inner, center.y - inner, inner * 2.0f, inner * 2.0f,
                            color(colors.primary, 90), 1.0f + stereoLevel_ * 2.0f);
}

void ArtVisualizerEngine::drawLevelBars(DrawList& output) const {
    const auto colors = palette(options_.palette);
    const std::array<float, 3> levels{lowLevel_, midLevel_, highLevel_};
    const std::array<uint32_t, 3> bars{colors.primary, colors.secondary, colors.tertiary};
    for (int band = 0; band < 3; ++band) {
        const float x = 18.0f + static_cast<float>(band) * 44.0f;
        output.addFillRectangle(x, 42.0f, 32.0f, 2.0f, color(0xb4cdd4, 38));
        output.addFillRectangle(x, 42.0f, 32.0f * levels[band], 2.0f,
                                color(bars[band], 210));
    }
}

void ArtVisualizerEngine::buildFrame(float width, float height, DrawList& output) {
    output.reset();
    if (!std::isfinite(width) || !std::isfinite(height) ||
        width <= 0.0f || height <= 0.0f) return;
    switch (options_.scene) {
        case 0: drawPhaseOrbit(output, width, height); break;
        case 1: drawNeonBloom(output, width, height); break;
        case 2: drawSignalHorizon(output, width, height); break;
        case 3: drawPrismTunnel(output, width, height); break;
        case 4: drawPulseMatrix(output, width, height); break;
        default: drawRadiantBars(output, width, height); break;
    }
    drawLevelBars(output);
}

ArtVisualizerFrameInfo ArtVisualizerEngine::frameInfo() const noexcept {
    return {options_.scene, options_.palette, sceneName(options_.scene), palette(options_.palette),
            lowLevel_, midLevel_, highLevel_, stereoLevel_};
}

std::string_view ArtVisualizerEngine::sceneName(int scene) noexcept {
    return kSceneNames[static_cast<size_t>(std::clamp(scene, 0, kSceneCount - 1))];
}

ArtPalette ArtVisualizerEngine::palette(int paletteIndex) noexcept {
    return kPalettes[static_cast<size_t>(std::clamp(paletteIndex, 0, kPaletteCount - 1))];
}

} // namespace vizrack::builtin
