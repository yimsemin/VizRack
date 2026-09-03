#include "builtin/spectrum3d_engine.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace vizrack::builtin {
namespace {

constexpr float kPi = std::numbers::pi_v<float>;
constexpr float kSlicesPerSecond = 24.0f;

constexpr std::array<std::string_view, Spectrum3dEngine::kStyleCount> kStyleNames{
    "CLASSIC CASCADE", "JOY DIVISION",
};

constexpr std::array<Spectrum3dPalette, Spectrum3dEngine::kPaletteCount> kPalettes{{
    {"MONOLITH", 0x05070c, 0x101725, 0xe6edf7, 0x59668a, 0x9fc4ff},
    {"NEON",     0x0a0414, 0x1c0b33, 0x4bf5ff, 0x6b3bcf, 0xff5cb2},
    {"EMBER",    0x0c0402, 0x2a1207, 0xffbf5a, 0x9c3e1d, 0xff6a3d},
    {"AURORA",   0x03120f, 0x0a2822, 0x76f0b2, 0x2f7f79, 0x9c6cff},
    {"ICEFIELD", 0x040a12, 0x0f2740, 0xd0ecff, 0x40709e, 0x82d6ff},
    {"MONO",     0x0a0a0a, 0x1f1f1f, 0xf5f5f5, 0x6b6b6b, 0xb2b2b2},
}};

float finiteSample(float value) noexcept {
    return std::isfinite(value) ? value : 0.0f;
}

uint32_t lerpRgb(uint32_t from, uint32_t to, float t) noexcept {
    t = std::clamp(t, 0.0f, 1.0f);
    const auto mix = [t](uint32_t a, uint32_t b, int shift) {
        const float av = static_cast<float>((a >> shift) & 0xffu);
        const float bv = static_cast<float>((b >> shift) & 0xffu);
        return static_cast<uint32_t>(std::lround(av + (bv - av) * t)) & 0xffu;
    };
    return (mix(from, to, 16) << 16) | (mix(from, to, 8) << 8) | mix(from, to, 0);
}

uint8_t clampAlpha(float value) noexcept {
    return static_cast<uint8_t>(std::clamp(value, 0.0f, 255.0f));
}

float smoothstep(float edge0, float edge1, float x) noexcept {
    const float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

float hashNoise(uint32_t a, uint32_t b) noexcept {
    uint32_t h = a * 0x9e3779b9u + b * 0x85ebca77u + 0x165667b1u;
    h ^= h >> 15;
    h *= 0xd168aaadu;
    h ^= h >> 15;
    return static_cast<float>(h) * (1.0f / 2147483648.0f) - 1.0f;
}

float valueNoise(uint32_t salt, float x) noexcept {
    const float base = std::floor(x);
    const uint32_t cell = static_cast<uint32_t>(std::max(0.0f, base));
    const float f = x - base;
    const float a = hashNoise(salt, cell);
    const float b = hashNoise(salt, cell + 1u);
    return a + (b - a) * (f * f * (3.0f - 2.0f * f));
}

// Deterministic per-slice pen tremble so a ridge is never a perfectly straight polyline.
float ridgeWiggle(uint32_t salt, float t) noexcept {
    return 0.48f * valueNoise(salt, t * 13.0f) +
           0.32f * valueNoise(salt ^ 0x5bd1e995u, t * 33.0f) +
           0.20f * valueNoise(salt * 3u + 7u, t * 78.0f);
}

float sampleBand(const float* slice, float t) noexcept {
    const float pos = std::clamp(t, 0.0f, 1.0f) *
                      static_cast<float>(Spectrum3dEngine::kBandCount - 1);
    const float base = std::floor(pos);
    const size_t index = static_cast<size_t>(base);
    const size_t next = std::min(index + 1, Spectrum3dEngine::kBandCount - 1);
    return slice[index] + (slice[next] - slice[index]) * (pos - base);
}

} // namespace

Spectrum3dEngine::Spectrum3dEngine() {
    scratch_.reserve(std::max({kBandCount, kDepth, kJoyResolution}) + 8);
    for (size_t i = 0; i < kFftSize; ++i) {
        size_t reversed = 0;
        for (size_t bit = 0; bit < kFftBits; ++bit) {
            reversed |= ((i >> bit) & 1u) << (kFftBits - 1 - bit);
        }
        bitReverse_[i] = static_cast<uint16_t>(reversed);
        hann_[i] = 0.5f - 0.5f * std::cos(2.0f * kPi * static_cast<float>(i) /
                                          static_cast<float>(kFftSize - 1));
    }
    for (size_t i = 0; i < kFftSize / 2; ++i) {
        const float angle = -2.0f * kPi * static_cast<float>(i) / static_cast<float>(kFftSize);
        twiddleCos_[i] = std::cos(angle);
        twiddleSin_[i] = std::sin(angle);
    }
}

void Spectrum3dEngine::setOptions(Spectrum3dOptions options) noexcept {
    options.style = std::clamp(options.style, 0, kStyleCount - 1);
    options.palette = std::clamp(options.palette, 0, kPaletteCount - 1);
    options.rotation = std::clamp(options.rotation, 0, 100);
    options.tilt = std::clamp(options.tilt, 0, 100);
    options.depth = std::clamp(options.depth, 0, 100);
    options.heightScale = std::clamp(options.heightScale, 0, 100);
    options_ = options;
}

void Spectrum3dEngine::setSampleRate(uint32_t sampleRate) noexcept {
    if (sampleRate >= 8000 && sampleRate <= 768000) {
        sampleRate_.store(sampleRate, std::memory_order_release);
    }
}

std::string_view Spectrum3dEngine::styleName(int style) noexcept {
    return kStyleNames[static_cast<size_t>(std::clamp(style, 0, kStyleCount - 1))];
}

Spectrum3dPalette Spectrum3dEngine::palette(int index) noexcept {
    return kPalettes[static_cast<size_t>(std::clamp(index, 0, kPaletteCount - 1))];
}

void Spectrum3dEngine::ingest(size_t count) noexcept {
    for (size_t index = 0; index < count; ++index) {
        const float mono = 0.5f * (finiteSample(incomingLeft_[index]) +
                                   finiteSample(incomingRight_[index]));
        sampleWindow_[sampleWrite_] = mono;
        sampleWrite_ = (sampleWrite_ + 1) % kFftSize;
    }
}

void Spectrum3dEngine::computeSpectrum() noexcept {
    for (size_t i = 0; i < kFftSize; ++i) {
        const size_t source = (sampleWrite_ + i) % kFftSize;
        real_[bitReverse_[i]] = sampleWindow_[source] * hann_[i];
        imag_[bitReverse_[i]] = 0.0f;
    }
    for (size_t span = 2; span <= kFftSize; span <<= 1) {
        const size_t half = span >> 1;
        const size_t step = kFftSize / span;
        for (size_t base = 0; base < kFftSize; base += span) {
            for (size_t j = 0; j < half; ++j) {
                const float wc = twiddleCos_[j * step];
                const float ws = twiddleSin_[j * step];
                const size_t a = base + j;
                const size_t b = a + half;
                const float tr = real_[b] * wc - imag_[b] * ws;
                const float ti = real_[b] * ws + imag_[b] * wc;
                real_[b] = real_[a] - tr;
                imag_[b] = imag_[a] - ti;
                real_[a] += tr;
                imag_[a] += ti;
            }
        }
    }
    const float rate = static_cast<float>(sampleRate_.load(std::memory_order_acquire));
    const float nyquist = rate * 0.5f;
    const float fMin = 40.0f;
    const float fMax = std::min(18000.0f, nyquist * 0.98f);
    const float ratio = fMax / fMin;
    for (size_t band = 0; band < kBandCount; ++band) {
        const float f0 = fMin * std::pow(ratio, static_cast<float>(band) /
                                                    static_cast<float>(kBandCount));
        const float f1 = fMin * std::pow(ratio, static_cast<float>(band + 1) /
                                                    static_cast<float>(kBandCount));
        const float bins = static_cast<float>(kFftSize) / rate;
        size_t k0 = std::max<size_t>(1, static_cast<size_t>(f0 * bins));
        size_t k1 = std::max(k0 + 1, static_cast<size_t>(f1 * bins));
        k1 = std::min(k1, kFftSize / 2);
        float peak = 0.0f;
        double sum = 0.0;
        for (size_t k = k0; k < k1; ++k) {
            const float magnitude = std::sqrt(real_[k] * real_[k] + imag_[k] * imag_[k]) *
                                    (2.0f / static_cast<float>(kFftSize));
            peak = std::max(peak, magnitude);
            sum += magnitude;
        }
        const float mean = static_cast<float>(sum / static_cast<double>(std::max<size_t>(1, k1 - k0)));
        const float magnitude = 0.5f * peak + 0.5f * mean;
        const float db = 20.0f * std::log10(magnitude + 1e-6f);
        bands_[band] = std::clamp((db + 84.0f) / 64.0f, 0.0f, 1.0f);
    }
}

void Spectrum3dEngine::pushSlice() noexcept {
    history_[historyWrite_] = smooth_;
    sliceSalt_[historyWrite_] = ++saltCounter_;
    historyWrite_ = (historyWrite_ + 1) % kDepth;
    historyCount_ = std::min(kDepth, historyCount_ + 1);
}

void Spectrum3dEngine::advanceHistory(float frameSeconds) noexcept {
    sliceAccumulator_ += frameSeconds * kSlicesPerSecond;
    size_t pushes = 0;
    while (sliceAccumulator_ >= 1.0f && pushes < kDepth) {
        pushSlice();
        sliceAccumulator_ -= 1.0f;
        ++pushes;
    }
    sliceAccumulator_ = std::min(sliceAccumulator_, static_cast<float>(kDepth));
}

void Spectrum3dEngine::update(size_t sampleCount, float frameSeconds) noexcept {
    const float safeSeconds = std::isfinite(frameSeconds) ? frameSeconds : 1.0f / 60.0f;
    const float clamped = std::clamp(safeSeconds, 1.0f / 240.0f, 1.0f / 15.0f);
    const float frameScale = std::clamp(clamped * 60.0f, 0.25f, 4.0f);
    if (sampleCount > 0) {
        ingest(std::min(sampleCount, kMaxSamples));
        computeSpectrum();
        for (size_t band = 0; band < kBandCount; ++band) {
            const float target = bands_[band];
            const float base = target > smooth_[band] ? 0.5f : 0.2f;
            const float coefficient = 1.0f - std::pow(1.0f - base, frameScale);
            smooth_[band] += (target - smooth_[band]) * coefficient;
        }
    } else {
        const float decay = std::pow(0.90f, frameScale);
        for (float& value : smooth_) value *= decay;
    }
    const size_t lowEnd = std::max<size_t>(1, kBandCount / 5);
    const size_t highStart = std::min(kBandCount - 1, kBandCount * 3 / 5);
    float low = 0.0f;
    float high = 0.0f;
    for (size_t band = 0; band < lowEnd; ++band) low += smooth_[band];
    for (size_t band = highStart; band < kBandCount; ++band) high += smooth_[band];
    lowLevel_ = std::clamp(low / static_cast<float>(lowEnd), 0.0f, 1.0f);
    highLevel_ = std::clamp(high / static_cast<float>(kBandCount - highStart), 0.0f, 1.0f);
    advanceHistory(clamped);
}

void Spectrum3dEngine::reset() noexcept {
    sampleWindow_.fill(0.0f);
    bands_.fill(0.0f);
    smooth_.fill(0.0f);
    for (auto& slice : history_) slice.fill(0.0f);
    sliceSalt_.fill(0u);
    saltCounter_ = 0;
    sampleWrite_ = 0;
    historyWrite_ = 0;
    historyCount_ = 0;
    sliceAccumulator_ = 0.0f;
    lowLevel_ = 0.0f;
    highLevel_ = 0.0f;
}

const float* Spectrum3dEngine::sliceFromFront(size_t depth) const noexcept {
    const size_t index = (historyWrite_ + kDepth - 1 - depth) % kDepth;
    return history_[index].data();
}

uint32_t Spectrum3dEngine::saltFromFront(size_t depth) const noexcept {
    const size_t index = (historyWrite_ + kDepth - 1 - depth) % kDepth;
    return sliceSalt_[index];
}

void Spectrum3dEngine::drawClassicCascade(DrawList& output, float width, float height) {
    const auto colors = palette(options_.palette);
    const size_t span = static_cast<size_t>(10 + options_.depth * 40 / 100);
    const size_t count = std::clamp(span, size_t{2}, historyCount_);
    const float ampScale = 0.40f + 0.011f * static_cast<float>(options_.heightScale);
    const float yaw = 0.10f + 0.0085f * static_cast<float>(options_.rotation);
    const float pitch = 0.20f + 0.00385f * static_cast<float>(options_.tilt);

    const float axisX = width * 0.62f;
    const float axisZ = width * 0.42f;
    const float ampHeight = height * 0.24f * ampScale;
    const float focal = (axisX + axisZ) * 1.45f;
    const float sinYaw = std::sin(yaw);
    const float cosYaw = std::cos(yaw);
    const float sinPitch = std::sin(pitch);
    const float cosPitch = std::cos(pitch);
    const float originX = width * 0.50f;
    const float originY = height * 0.72f;

    // Quarter view: newest slice near the camera at the bottom, history receding up and back.
    const auto project = [&](float t, float value, float zt) noexcept -> Point {
        const float wx = (t - 0.5f) * axisX;
        const float wz = zt * axisZ - axisZ * 0.35f;
        const float wy = std::clamp(value, 0.0f, 1.0f) * ampHeight;
        const float x1 = wx * cosYaw - wz * sinYaw;
        const float z1 = wx * sinYaw + wz * cosYaw;
        const float y2 = wy * cosPitch + z1 * sinPitch;
        const float z2 = z1 * cosPitch - wy * sinPitch;
        const float persp = focal / std::max(focal * 0.2f, focal + z2);
        return {originX + x1 * persp, originY - y2 * persp};
    };
    const auto depthT = [&](size_t depth) noexcept {
        return static_cast<float>(depth) / static_cast<float>(std::max<size_t>(1, count - 1));
    };
    const auto fadeAt = [](float zt) noexcept { return std::pow(1.0f - zt, 0.85f); };

    // Faint receding floor to anchor the plane.
    for (size_t line = 0; line <= 6; ++line) {
        const float zt = static_cast<float>(line) / 6.0f;
        const Point a = project(0.0f, 0.0f, zt);
        const Point b = project(1.0f, 0.0f, zt);
        output.addLine(a.x, a.y, b.x, b.y, color(colors.horizon, 20), 1.0f);
    }

    // Filled surface strips between consecutive time slices, back-to-front, so the trail
    // reads as a shaded sheet rather than a stack of lines.
    for (size_t step = 0; step + 1 < count; ++step) {
        const size_t front = count - 1 - step;
        const size_t back = front + 1;
        const float ztFront = depthT(front);
        const float ztBack = depthT(back);
        const float fade = fadeAt(0.5f * (ztFront + ztBack));
        const float* sliceFront = sliceFromFront(front);
        const float* sliceBack = sliceFromFront(back);
        scratch_.clear();
        for (size_t band = 0; band < kBandCount; ++band) {
            const float t = static_cast<float>(band) / static_cast<float>(kBandCount - 1);
            scratch_.push_back(project(t, sliceFront[band], ztFront));
        }
        for (size_t band = kBandCount; band-- > 0;) {
            const float t = static_cast<float>(band) / static_cast<float>(kBandCount - 1);
            scratch_.push_back(project(t, sliceBack[band], ztBack));
        }
        const auto range = output.appendPoints(scratch_);
        const uint32_t fillRgb = lerpRgb(colors.background,
                                         lerpRgb(colors.ridgeFar, colors.ridgeNear, fade), 0.85f);
        output.addFillPolygon(range, color(fillRgb, clampAlpha(55.0f + 160.0f * fade)));
    }

    // Rib outlines on top so each time step keeps a crisp leading edge.
    for (size_t step = 0; step < count; ++step) {
        const size_t depth = count - 1 - step;
        const float zt = depthT(depth);
        const float fade = fadeAt(zt);
        const float* slice = sliceFromFront(depth);
        scratch_.clear();
        for (size_t band = 0; band < kBandCount; ++band) {
            const float t = static_cast<float>(band) / static_cast<float>(kBandCount - 1);
            scratch_.push_back(project(t, slice[band], zt));
        }
        const auto range = output.appendPoints(scratch_);
        const uint32_t rgb = depth == 0 ? colors.accent
                                        : lerpRgb(colors.ridgeFar, colors.ridgeNear, fade);
        output.addPolyline(range, color(rgb, clampAlpha(35.0f + 175.0f * fade)),
                           depth == 0 ? 2.2f : 0.7f + 1.1f * fade, false);
    }
}

void Spectrum3dEngine::drawJoyDivision(DrawList& output, float width, float height) {
    const auto colors = palette(options_.palette);
    // Option value 50 is the reference look; each knob spreads out from there.
    const float depthMix = 0.35f + 0.0065f * static_cast<float>(options_.depth);
    const size_t span = static_cast<size_t>(static_cast<float>(kDepth) * depthMix);
    const size_t count = std::clamp(span, size_t{2}, historyCount_);
    const float ampScale =
        std::max(0.06f, 0.55f + 0.009f * static_cast<float>(options_.heightScale - 50));
    const float rowExp =
        std::clamp(1.10f + 0.008f * static_cast<float>(options_.tilt - 50), 0.55f, 1.8f);

    // Centred plot box: consistent margins on all four sides, gently taller than wide.
    const float plotHeight = height * 0.56f;
    const float plotWidth = std::min(width * 0.50f, plotHeight * 0.95f);
    const float centerX = width * 0.5f;
    const float topY = (height - plotHeight) * 0.5f;
    const float bottomY = topY + plotHeight;
    const float halfWidth = plotWidth * 0.5f;
    const float wiggle = plotHeight * 0.026f;
    const float deadZone = 0.13f;  // outer fraction held flat on the baseline
    const float rampEnd = 0.28f;   // ... rising to the full spectrum by here

    for (size_t step = 0; step < count; ++step) {
        const size_t depth = count - 1 - step;  // oldest first, drawn behind
        const float zt = static_cast<float>(depth) / static_cast<float>(std::max<size_t>(1, count - 1));
        const float nearness = 1.0f - zt;
        const float rowY = bottomY + (topY - bottomY) * std::pow(zt, rowExp);
        const float rowAmp = plotHeight * 0.70f * ampScale * (0.45f + 0.55f * nearness);
        const float* slice = sliceFromFront(depth);
        const uint32_t salt = saltFromFront(depth);
        scratch_.clear();
        for (size_t i = 0; i < kJoyResolution; ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(kJoyResolution - 1);
            const float mask = smoothstep(deadZone, rampEnd, t) *
                               smoothstep(deadZone, rampEnd, 1.0f - t);
            const float raw = std::clamp(sampleBand(slice, t), 0.0f, 1.0f);
            const float shaped = std::pow(raw, 1.9f);  // widen crest-to-trough contrast
            const float jitter = mask * wiggle * ridgeWiggle(salt, t);
            const float x = centerX + (t - 0.5f) * 2.0f * halfWidth;
            const float y = rowY - shaped * mask * rowAmp - jitter;
            scratch_.push_back({x, y});
        }
        scratch_.push_back({centerX + halfWidth, rowY + wiggle + 8.0f});
        scratch_.push_back({centerX - halfWidth, rowY + wiggle + 8.0f});
        const auto range = output.appendPoints(scratch_);
        output.addFillPolygon(range, color(colors.background));
        // No depth fade: every ridge is drawn at full strength, only occlusion gives depth.
        const uint32_t lineRgb = depth == 0 ? colors.accent : colors.ridgeNear;
        output.addPolyline({range.offset, static_cast<uint32_t>(kJoyResolution)},
                           color(lineRgb, 235), depth == 0 ? 1.6f : 1.0f, false);
    }
}

void Spectrum3dEngine::buildFrame(float width, float height, DrawList& output) {
    output.reset();
    if (!std::isfinite(width) || !std::isfinite(height) || width <= 0.0f || height <= 0.0f) {
        return;
    }
    const auto colors = palette(options_.palette);
    if (options_.style == 1) {
        output.addVerticalGradient(0.0f, 0.0f, width, height,
                                   color(colors.background), color(colors.background));
    } else {
        output.addVerticalGradient(0.0f, 0.0f, width, height,
                                   color(colors.horizon), color(colors.background));
    }
    if (historyCount_ < 2) return;
    if (options_.style == 1) {
        drawJoyDivision(output, width, height);
    } else {
        drawClassicCascade(output, width, height);
    }
}

Spectrum3dFrameInfo Spectrum3dEngine::frameInfo() const noexcept {
    Spectrum3dFrameInfo info;
    info.style = options_.style;
    info.palette = options_.palette;
    info.styleName = styleName(options_.style);
    info.colors = palette(options_.palette);
    info.lowLevel = lowLevel_;
    info.highLevel = highLevel_;
    info.fill = static_cast<float>(historyCount_) / static_cast<float>(kDepth);
    return info;
}

} // namespace vizrack::builtin
