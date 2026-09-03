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

struct Spectrum3dOptions {
    int style{0};          // 0 = CLASSIC CASCADE, 1 = JOY DIVISION
    int palette{0};
    int rotation{50};      // 0..100 quarter-view yaw (CLASSIC CASCADE)
    int tilt{50};          // 0..100 viewing pitch / perspective strength
    int depth{50};         // 0..100 how many history slices stay visible
    int heightScale{50};   // 0..100 magnitude amplitude
};

namespace builtin {

struct Spectrum3dPalette {
    std::string_view name;
    uint32_t background{};  // gradient bottom / Joy Division occlusion fill
    uint32_t horizon{};     // gradient top / far fade target
    uint32_t ridgeNear{};   // front-row line colour
    uint32_t ridgeFar{};    // back-row line colour
    uint32_t accent{};      // newest-slice highlight
};

struct Spectrum3dFrameInfo {
    int style{};
    int palette{};
    std::string_view styleName;
    Spectrum3dPalette colors;
    float lowLevel{};
    float highLevel{};
    float fill{};  // 0..1 fraction of the depth buffer holding real slices
};

// Time-as-depth spectrum view: every frame the newest magnitude spectrum is pushed to the
// front of a fixed history buffer and older slices recede, shrink and fade. CLASSIC CASCADE
// draws the receding surface Winamp-style; JOY DIVISION stacks hidden-line ridge curves.
// Standard C++20 only -- no platform, graphics or I/O dependencies.
class Spectrum3dEngine {
public:
    static constexpr size_t kMaxSamples = 4096;
    static constexpr size_t kFftSize = 1024;
    static constexpr size_t kFftBits = 10;
    static constexpr size_t kBandCount = 40;
    static constexpr size_t kDepth = 72;
    static constexpr int kStyleCount = 2;
    static constexpr int kPaletteCount = 6;
    static constexpr size_t kJoyResolution = 84;  // interpolated points per ridge curve
    static constexpr size_t kMaxRenderedPoints = 7500;

    Spectrum3dEngine();

    void setOptions(Spectrum3dOptions options) noexcept;
    Spectrum3dOptions options() const noexcept { return options_; }
    void setSampleRate(uint32_t sampleRate) noexcept;

    std::span<float> inputLeft() noexcept { return incomingLeft_; }
    std::span<float> inputRight() noexcept { return incomingRight_; }
    void update(size_t sampleCount, float frameSeconds = 1.0f / 60.0f) noexcept;
    void reset() noexcept;

    void buildFrame(float width, float height, DrawList& output);
    Spectrum3dFrameInfo frameInfo() const noexcept;

    static std::string_view styleName(int style) noexcept;
    static Spectrum3dPalette palette(int index) noexcept;

private:
    void ingest(size_t count) noexcept;
    void computeSpectrum() noexcept;
    void advanceHistory(float frameSeconds) noexcept;
    void pushSlice() noexcept;
    const float* sliceFromFront(size_t depth) const noexcept;
    uint32_t saltFromFront(size_t depth) const noexcept;
    void drawClassicCascade(DrawList& output, float width, float height);
    void drawJoyDivision(DrawList& output, float width, float height);

    std::array<float, kMaxSamples> incomingLeft_{};
    std::array<float, kMaxSamples> incomingRight_{};
    std::array<float, kFftSize> sampleWindow_{};
    std::array<uint16_t, kFftSize> bitReverse_{};
    std::array<float, kFftSize> hann_{};
    std::array<float, kFftSize / 2> twiddleCos_{};
    std::array<float, kFftSize / 2> twiddleSin_{};
    std::array<float, kFftSize> real_{};
    std::array<float, kFftSize> imag_{};
    std::array<float, kBandCount> bands_{};
    std::array<float, kBandCount> smooth_{};
    std::array<std::array<float, kBandCount>, kDepth> history_{};
    std::array<uint32_t, kDepth> sliceSalt_{};
    uint32_t saltCounter_{};
    size_t sampleWrite_{};
    size_t historyWrite_{};
    size_t historyCount_{};
    float sliceAccumulator_{};
    float lowLevel_{};
    float highLevel_{};
    std::atomic<uint32_t> sampleRate_{48000};
    Spectrum3dOptions options_;
    std::vector<Point> scratch_;
};

} // namespace builtin
} // namespace vizrack
