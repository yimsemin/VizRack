#pragma once

#include "builtin/draw_list.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace vizrack {

struct OscilloscopeOptions {
    int fps{60};
    int scalePercent{70};
    int smoothing{1};
    bool historyMode{false};
};

namespace builtin {

struct OscilloscopeFrameInfo {
    OscilloscopeOptions options;
    uint32_t sampleRate{};
    bool hasSignalTrace{};
};

class OscilloscopeEngine {
public:
    static constexpr size_t kMaxSamples = 4096;
    static constexpr size_t kHistoryPoints = 900;
    static constexpr size_t kMaxRenderedPoints = 2048;

    OscilloscopeEngine();

    void setOptions(OscilloscopeOptions options) noexcept;
    OscilloscopeOptions options() const noexcept { return options_; }
    void setSampleRate(uint32_t sampleRate) noexcept;

    std::span<float> inputLeft() noexcept { return incomingLeft_; }
    std::span<float> inputRight() noexcept { return incomingRight_; }
    void update(size_t sampleCount) noexcept;
    void reset() noexcept;

    void buildFrame(float width, float height, DrawList& output);
    OscilloscopeFrameInfo frameInfo() const noexcept;

private:
    void appendTrace(size_t count) noexcept;
    void appendHistory(float left, float right) noexcept;
    float traceSample(const std::array<float, kMaxSamples>& channel,
                      size_t logicalIndex) const noexcept;
    void addWaveform(DrawList& output, float width, float height);
    void addHistory(DrawList& output, float width, float height);
    void addTraceCommands(DrawList& output, std::span<const Point> points,
                          uint32_t rgb, float coreWidth);

    std::array<float, kMaxSamples> left_{};
    std::array<float, kMaxSamples> right_{};
    std::array<float, kMaxSamples> incomingLeft_{};
    std::array<float, kMaxSamples> incomingRight_{};
    std::array<float, kHistoryPoints> historyLeft_{};
    std::array<float, kHistoryPoints> historyRight_{};
    size_t traceCount_{};
    size_t traceWrite_{};
    size_t historyCount_{};
    size_t historyWrite_{};
    float historyLeftLevel_{};
    float historyRightLevel_{};
    std::atomic<uint32_t> sampleRate_{48000};
    OscilloscopeOptions options_;
    std::vector<Point> scratchA_;
    std::vector<Point> scratchB_;
};

} // namespace builtin
} // namespace vizrack
