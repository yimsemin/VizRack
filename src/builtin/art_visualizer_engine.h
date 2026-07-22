#pragma once

#include "builtin/draw_list.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace vizrack {

struct ArtVisualizerOptions {
    int scene{0};
    int palette{0};
};

namespace builtin {

struct ArtPalette {
    std::string_view name;
    uint32_t top{};
    uint32_t bottom{};
    uint32_t primary{};
    uint32_t secondary{};
    uint32_t tertiary{};
};

struct ArtVisualizerFrameInfo {
    int scene{};
    int palette{};
    std::string_view sceneName;
    ArtPalette colors;
    float lowLevel{};
    float midLevel{};
    float highLevel{};
    float stereoLevel{};
};

class ArtVisualizerEngine {
public:
    static constexpr size_t kMaxSamples = 4096;
    static constexpr int kSceneCount = 6;
    static constexpr int kPaletteCount = 6;

    ArtVisualizerEngine();

    void setOptions(ArtVisualizerOptions options) noexcept;
    ArtVisualizerOptions options() const noexcept { return options_; }
    void setSampleRate(uint32_t sampleRate) noexcept;

    std::span<float> inputLeft() noexcept { return left_; }
    std::span<float> inputRight() noexcept { return right_; }
    void update(size_t sampleCount, float frameSeconds = 1.0f / 60.0f) noexcept;
    void reset() noexcept;

    void buildFrame(float width, float height, DrawList& output);
    ArtVisualizerFrameInfo frameInfo() const noexcept;

    static std::string_view sceneName(int scene) noexcept;
    static ArtPalette palette(int palette) noexcept;

private:
    void analyzeSamples(float frameScale) noexcept;
    void fillBackground(DrawList& output, float width, float height) const;
    void addGlowPolyline(DrawList& output, std::span<const Point> points,
                         Color value, float width);
    void drawPhaseOrbit(DrawList& output, float width, float height);
    void drawNeonBloom(DrawList& output, float width, float height);
    void drawSignalHorizon(DrawList& output, float width, float height);
    void drawPrismTunnel(DrawList& output, float width, float height);
    void drawPulseMatrix(DrawList& output, float width, float height);
    void drawRadiantBars(DrawList& output, float width, float height);
    void drawLevelBars(DrawList& output) const;
    float averageMono(size_t center, size_t radius) const noexcept;

    std::array<float, kMaxSamples> left_{};
    std::array<float, kMaxSamples> right_{};
    size_t sampleCount_{};
    std::atomic<uint32_t> sampleRate_{48000};
    float lowFilter_{};
    float midFilter_{};
    float lowLevel_{};
    float midLevel_{};
    float highLevel_{};
    float stereoLevel_{};
    float phase_{};
    ArtVisualizerOptions options_;
    std::vector<Point> scratchA_;
    std::vector<Point> scratchB_;
};

} // namespace builtin
} // namespace vizrack
