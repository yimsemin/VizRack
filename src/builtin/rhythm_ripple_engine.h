#pragma once

#include "builtin/draw_list.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>

namespace vizrack {

// `sensitivity` (0-100, higher fires more easily) sets how large a jump in a
// band's level counts as a hit. `syncDelayMs` postpones every detected
// ripple by that long: loopback sees audio before it is heard (Bluetooth and
// some USB devices add 100ms+), and a ripple can be delayed but never
// advanced.
struct RhythmRippleOptions {
    int sensitivity{50};
    int syncDelayMs{0};
};

namespace builtin {

// "Rhythm Ripple": rain falling on a still, dark puddle in time with the
// music. Every accepted hit spawns exactly one raindrop-ripple at a fully
// random point on the surface -- there is no lane, no single "current beat"
// position, and no limit to one at a time: a busy passage can and should
// drop several raindrops in close succession, just like real rain speeding
// up with the music.
//
// Hits are found per band, not on the overall level. The signal is split
// into low (kick), mid (snare/body) and high (hats) bands; each band follows
// a rectified envelope that is sampled every kBlockSamples and compared in
// the log domain with the value a few milliseconds earlier. A jump above the
// sensitivity threshold fires immediately -- the detector never waits for
// the peak -- and the ripple is back-dated to the block where the jump
// happened, so it starts as far into its life as the hit already is rather
// than at the frame boundary. A band that fired keeps raising its ripple's
// magnitude for a few more milliseconds, so the size still follows the real
// peak. A low-band ripple is drawn larger and a high-band one smaller.
// There is deliberately no tempo estimation: rain does not wait its turn.
class RhythmRippleEngine {
public:
    static constexpr size_t kMaxSamples = 4096;
    // Fixed ripple capacity: several raindrops can be visible at once during
    // a busy passage (each lives under a second), but the count is always
    // bounded regardless of how fast hits are accepted.
    static constexpr size_t kMaxRipples = 24;
    static constexpr int kMaxSyncDelayMs = 300;

    void setOptions(RhythmRippleOptions options) noexcept;
    RhythmRippleOptions options() const noexcept { return options_; }
    void setSampleRate(uint32_t sampleRate) noexcept;
    std::span<float> inputLeft() noexcept { return left_; }
    std::span<float> inputRight() noexcept { return right_; }
    void update(size_t sampleCount, float frameSeconds = 1.0f / 60.0f) noexcept;
    void reset() noexcept;

    void buildFrame(float width, float height, DrawList& output);

    // Test hooks: how many ripples have been spawned since construction/reset,
    // and how far into its life the newest one already was at spawn (a
    // detected hit is back-dated to its true position inside the audio chunk;
    // negative when delayed by syncDelayMs).
    uint32_t spawnedRippleCount() const noexcept { return spawnedCount_; }
    float lastSpawnStartAge() const noexcept { return lastSpawnStartAge_; }

private:
    // A single raindrop-ripple lifecycle: spawned at one reference-frame
    // position (see kReferenceWidth/kReferenceHeight in the .cpp), it grows
    // through a fixed real-time lifetime and is then reclaimed. `age` starts
    // negative for a delayed ripple, which is not drawn until it reaches 0.
    // `magnitude` is the hit's own level, used to scale the impact flash and
    // ring brightness so a harder hit reads bigger. `seed` drives the splash
    // droplets' angles so each raindrop's splash looks distinct.
    struct Ripple {
        bool active{false};
        float x{};
        float y{};
        float age{};
        float magnitude{1.0f};
        float radiusScale{1.0f};
        uint32_t seed{};
    };

    static constexpr size_t kBandCount = 3;
    static constexpr size_t kBlockSamples = 128;
    static constexpr size_t kHistoryBlocks = 16;
    struct Band {
        float envelope{};
        std::array<float, kHistoryBlocks> history{};
        size_t historyNext{};
        size_t historyCount{};
        float refractorySeconds{};
        bool armed{true};
        // The ripple this band most recently fired: its magnitude keeps
        // growing toward the true peak for a few ms without delaying the
        // ripple itself.
        size_t refineSlot{};
        uint32_t refineSeed{};
        float refineSeconds{};
    };

    // Filters the chunk into bands, evaluates each band's envelope every
    // kBlockSamples and spawns ripples directly.
    void analyzeSamples(float safeSeconds) noexcept;
    float nextRandom() noexcept;
    // Spawns a raindrop at an explicit position. `startAge` back-dates
    // (positive) or delays (negative) the ripple; returns its pool slot, or
    // kMaxRipples when the pool was full.
    size_t spawnRippleAt(float x, float y, float magnitude, float startAge = 0.0f,
                         float radiusScale = 1.0f) noexcept;
    void drawBackground(DrawList& output, float width, float height) const;
    void drawRipple(DrawList& output, float screenX, float screenY, float minDimension,
                    const Ripple& ripple) const;

    std::array<float, kMaxSamples> left_{};
    std::array<float, kMaxSamples> right_{};
    size_t sampleCount_{};
    std::atomic<uint32_t> sampleRate_{48000};

    float lowStage1_{};
    float lowStage2_{};
    float midLowpass_{};
    size_t blockFill_{};
    std::array<Band, kBandCount> bands_{};

    float overallLevel_{}; // drives the silence gate
    float silenceSeconds_{};

    std::array<Ripple, kMaxRipples> ripples_{};
    size_t nextRippleSlot_{};
    uint32_t spawnSerial_{};
    uint32_t spawnedCount_{};
    float lastSpawnStartAge_{};
    uint32_t randomState_{0xc2b2ae35u};

    RhythmRippleOptions options_;
};

} // namespace builtin
} // namespace vizrack
