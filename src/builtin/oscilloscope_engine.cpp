#include "builtin/oscilloscope_engine.h"

#include <algorithm>
#include <cmath>

namespace vizrack::builtin {
namespace {

constexpr uint32_t kBackground = 0x080e12;
constexpr uint32_t kGrid = 0x19373d;
constexpr uint32_t kLeft = 0x3ad6ae;
constexpr uint32_t kRight = 0x55a8ff;

float finiteSample(float value) noexcept {
    return std::isfinite(value) ? value : 0.0f;
}

uint32_t dimRgb(uint32_t rgb) noexcept {
    const uint32_t red = ((rgb >> 16) & 0xff) / 3;
    const uint32_t green = ((rgb >> 8) & 0xff) / 3;
    const uint32_t blue = (rgb & 0xff) / 3;
    return (red << 16) | (green << 8) | blue;
}

} // namespace

OscilloscopeEngine::OscilloscopeEngine() {
    scratchA_.reserve(kMaxRenderedPoints);
    scratchB_.reserve(kMaxRenderedPoints);
}

void OscilloscopeEngine::setOptions(OscilloscopeOptions options) noexcept {
    options.fps = options.fps == 15 || options.fps == 30 ? options.fps : 60;
    options.scalePercent = options.scalePercent == 50 || options.scalePercent == 100
                               ? options.scalePercent
                               : 70;
    options.smoothing = std::clamp(options.smoothing, 0, 2);
    options_ = options;
}

void OscilloscopeEngine::setSampleRate(uint32_t sampleRate) noexcept {
    if (sampleRate >= 8000 && sampleRate <= 768000) {
        sampleRate_.store(sampleRate, std::memory_order_release);
    }
}

void OscilloscopeEngine::appendTrace(size_t count) noexcept {
    if (count > kMaxSamples) count = kMaxSamples;
    for (size_t index = 0; index < count; ++index) {
        left_[traceWrite_] = finiteSample(incomingLeft_[index]);
        right_[traceWrite_] = finiteSample(incomingRight_[index]);
        traceWrite_ = (traceWrite_ + 1) % kMaxSamples;
    }
    traceCount_ = std::min(kMaxSamples, traceCount_ + count);
}

void OscilloscopeEngine::appendHistory(float left, float right) noexcept {
    historyLeft_[historyWrite_] = left;
    historyRight_[historyWrite_] = right;
    historyWrite_ = (historyWrite_ + 1) % kHistoryPoints;
    historyCount_ = std::min(kHistoryPoints, historyCount_ + 1);
}

void OscilloscopeEngine::update(size_t sampleCount) noexcept {
    const size_t count = std::min(sampleCount, kMaxSamples);
    float leftTarget = 0.0f;
    float rightTarget = 0.0f;
    if (count > 0) {
        appendTrace(count);
        double leftEnergy = 0.0;
        double rightEnergy = 0.0;
        for (size_t index = 0; index < count; ++index) {
            const float left = finiteSample(incomingLeft_[index]);
            const float right = finiteSample(incomingRight_[index]);
            leftEnergy += static_cast<double>(left) * left;
            rightEnergy += static_cast<double>(right) * right;
        }
        leftTarget = std::clamp(
            std::sqrt(static_cast<float>(leftEnergy / static_cast<double>(count))) * 4.5f,
            0.0f, 1.0f);
        rightTarget = std::clamp(
            std::sqrt(static_cast<float>(rightEnergy / static_cast<double>(count))) * 4.5f,
            0.0f, 1.0f);
    }
    const auto follow = [](float current, float target) {
        return current + (target - current) * (target > current ? 0.45f : 0.10f);
    };
    historyLeftLevel_ = follow(historyLeftLevel_, leftTarget);
    historyRightLevel_ = follow(historyRightLevel_, rightTarget);
    appendHistory(historyLeftLevel_, historyRightLevel_);
}

void OscilloscopeEngine::reset() noexcept {
    traceCount_ = 0;
    traceWrite_ = 0;
    historyCount_ = 0;
    historyWrite_ = 0;
    historyLeftLevel_ = 0.0f;
    historyRightLevel_ = 0.0f;
}

float OscilloscopeEngine::traceSample(const std::array<float, kMaxSamples>& channel,
                                      size_t logicalIndex) const noexcept {
    const size_t oldest = (traceWrite_ + kMaxSamples - traceCount_) % kMaxSamples;
    return channel[(oldest + logicalIndex) % kMaxSamples];
}

void OscilloscopeEngine::addTraceCommands(DrawList& output, std::span<const Point> points,
                                           uint32_t rgb, float coreWidth) {
    if (points.size() < 2) return;
    const auto range = output.appendPoints(points);
    output.addPolyline(range, color(dimRgb(rgb)), 4.0f, false);
    output.addPolyline(range, color(rgb), coreWidth, false);
}

void OscilloscopeEngine::addWaveform(DrawList& output, float width, float height) {
    if (traceCount_ < 2 || width < 2.0f) return;
    size_t start = 0;
    const size_t triggerLimit = traceCount_ / 2;
    for (size_t index = 1; index < triggerLimit; ++index) {
        if (finiteSample(traceSample(left_, index - 1)) <= 0.0f &&
            finiteSample(traceSample(left_, index)) > 0.0f) {
            start = index;
            break;
        }
    }
    const size_t count = traceCount_ - start;
    float peak = 0.0f;
    for (size_t index = start; index < traceCount_; ++index) {
        peak = std::max(peak, std::abs(finiteSample(traceSample(left_, index))));
        peak = std::max(peak, std::abs(finiteSample(traceSample(right_, index))));
    }
    const float amplitude = std::max(6.0f, height / 5.0f * options_.scalePercent / 100.0f);
    const float scale = amplitude / std::max(peak, 0.025f);
    const size_t radius = options_.smoothing == 0 ? 0 : (options_.smoothing == 1 ? 2 : 6);
    const size_t pointCount = std::clamp(static_cast<size_t>(width), size_t{2}, kMaxRenderedPoints);
    scratchA_.clear();
    scratchB_.clear();
    for (size_t point = 0; point < pointCount; ++point) {
        const size_t index = start + point * (count - 1) / (pointCount - 1);
        const size_t first = std::max(start, index > radius ? index - radius : start);
        const size_t last = std::min(traceCount_ - 1, index + radius);
        float leftValue = 0.0f;
        float rightValue = 0.0f;
        for (size_t sample = first; sample <= last; ++sample) {
            leftValue += finiteSample(traceSample(left_, sample));
            rightValue += finiteSample(traceSample(right_, sample));
        }
        const float divisor = static_cast<float>(last - first + 1);
        leftValue /= divisor;
        rightValue /= divisor;
        const float x = static_cast<float>(point) * (width - 1.0f) /
                        static_cast<float>(pointCount - 1);
        scratchA_.push_back({x, std::clamp(height * 0.25f - leftValue * scale,
                                           height * 0.25f - amplitude,
                                           height * 0.25f + amplitude)});
        scratchB_.push_back({x, std::clamp(height * 0.75f - rightValue * scale,
                                           height * 0.75f - amplitude,
                                           height * 0.75f + amplitude)});
    }
    addTraceCommands(output, scratchA_, kLeft, 1.0f);
    addTraceCommands(output, scratchB_, kRight, 1.0f);
}

void OscilloscopeEngine::addHistory(DrawList& output, float width, float height) {
    const size_t visible = std::min({historyCount_, kHistoryPoints,
                                     static_cast<size_t>(options_.fps) * 15});
    if (visible < 2 || width < 2.0f) return;
    const float amplitude = std::max(6.0f, height / 5.0f * options_.scalePercent / 100.0f);
    const size_t pointCount = std::clamp(static_cast<size_t>(width), size_t{2}, kMaxRenderedPoints);
    const size_t oldest = (historyWrite_ + kHistoryPoints - visible) % kHistoryPoints;
    scratchA_.clear();
    scratchB_.clear();
    for (size_t point = 0; point < pointCount; ++point) {
        const size_t offset = point * (visible - 1) / (pointCount - 1);
        const size_t index = (oldest + offset) % kHistoryPoints;
        const float x = static_cast<float>(point) * (width - 1.0f) /
                        static_cast<float>(pointCount - 1);
        scratchA_.push_back({x, height * 0.25f + amplitude * 0.5f -
                                   std::clamp(historyLeft_[index], 0.0f, 1.0f) * amplitude});
        scratchB_.push_back({x, height * 0.75f + amplitude * 0.5f -
                                   std::clamp(historyRight_[index], 0.0f, 1.0f) * amplitude});
    }
    addTraceCommands(output, scratchA_, kLeft, 2.0f);
    addTraceCommands(output, scratchB_, kRight, 2.0f);
}

void OscilloscopeEngine::buildFrame(float width, float height, DrawList& output) {
    output.reset();
    if (!std::isfinite(width) || !std::isfinite(height) ||
        width <= 0.0f || height <= 0.0f) return;
    output.addFillRectangle(0.0f, 0.0f, width, height, color(kBackground));
    for (int division = 1; division < 8; ++division) {
        const float x = width * static_cast<float>(division) / 8.0f;
        output.addLine(x, 0.0f, x, height, color(kGrid), 1.0f);
    }
    output.addLine(0.0f, height * 0.25f, width, height * 0.25f, color(kGrid), 1.0f);
    output.addLine(0.0f, height * 0.75f, width, height * 0.75f, color(kGrid), 1.0f);
    if (options_.historyMode) {
        addHistory(output, width, height);
    } else {
        addWaveform(output, width, height);
    }
}

OscilloscopeFrameInfo OscilloscopeEngine::frameInfo() const noexcept {
    return {options_, sampleRate_.load(std::memory_order_acquire), traceCount_ >= 2};
}

} // namespace vizrack::builtin
