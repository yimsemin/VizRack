#pragma once

#include "builtin/draw_list.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace vizrack {

// Peak-detection sensitivity for each of the four coarse band groups (see
// docs/STAR_GUITAR_FREQUENCY_BANDS.md), 0-100. Higher fires on a smaller
// relative rise (more peaks, more spawns); lower requires a sharper rise
// (fewer, more selective peaks). 50 is the shipped default and is
// recalibrated per group (see kBandReferenceSensitivity in the .cpp) to
// land on values found good by ear during tuning; there is no single
// "correct" value in general -- it depends on the source material, hence
// this being an exposed setting rather than a hardcoded constant.
struct StarGuitarOptions {
    int lowSensitivity{50};
    int midSensitivity{50};
    int trebleSensitivity{50};
    int airSensitivity{50};
};

namespace builtin {

enum class StarGuitarObjectType : uint8_t {
    pole,
    tree,
    pine, // a second, visually distinct tree silhouette (rounded canopy vs. tree's tiered one)
    buildingA,
    buildingB,
    buildingC,
    signalMarker,
    star,
    ufo,
};

// A "Star Guitar" style rhythmic landscape, referencing the Chemical
// Brothers video: several depth layers of scenery scroll past at different
// speeds (parallax). What spawns, when, how big and in which layer is driven
// by one idea applied across eight frequency sub-bands (see
// docs/STAR_GUITAR_FREQUENCY_BANDS.md): a sub-band's *relative* rise over
// its own recent baseline -- not an absolute level -- marks a peak; the
// peak's height sets the spawned object's size, and which sub-band peaked
// selects both the object type and which depth layer it spawns into. Every
// spawn animates in with the same grow-from-ground motion, so the
// distinction between "this is a real hit" and "this is ambient filler" is
// expressed only through size, never through whether it animates at all.
class StarGuitarEngine {
public:
    static constexpr size_t kMaxSamples = 4096;
    // Four depth layers, plus sky. Far is slowest/smallest, near is
    // fastest/largest, selling parallax depth; sky scrolls independently.
    // Which frequency group drives which layer is set out in
    // docs/STAR_GUITAR_FREQUENCY_BANDS.md (near <- low, mid <- mid,
    // far <- treble, sky <- air).
    static constexpr size_t kLayerCount = 4;
    static constexpr size_t kFarLayer = 0;
    static constexpr size_t kMidLayer = 1;
    static constexpr size_t kNearLayer = 2;
    static constexpr size_t kSkyLayer = 3;
    // Fixed per-layer object capacity: bounds simultaneous on-screen objects
    // (and therefore draw commands) regardless of audio activity. Generous
    // relative to each layer's crossing time so a busy passage doesn't
    // silently starve it of new spawns until old ones scroll off.
    static constexpr size_t kLayerCapacity = 56;
    // Eight frequency sub-bands, low to high -- see
    // docs/STAR_GUITAR_FREQUENCY_BANDS.md for the cutoffs and what each
    // drives.
    static constexpr size_t kBandCount = 8;

    StarGuitarEngine();

    void setOptions(StarGuitarOptions options) noexcept;
    StarGuitarOptions options() const noexcept { return options_; }
    void setSampleRate(uint32_t sampleRate) noexcept;
    std::span<float> inputLeft() noexcept { return left_; }
    std::span<float> inputRight() noexcept { return right_; }
    void update(size_t sampleCount, float frameSeconds = 1.0f / 60.0f) noexcept;
    void reset() noexcept;

    void buildFrame(float width, float height, DrawList& output);

private:
    // A single scrolling object. `traveled` is the distance (in reference
    // pixels) the object has moved since it spawned at the right edge of the
    // reference frame; screen position is derived from it every frame rather
    // than stored, so layers need no index/grid bookkeeping. `seed` also
    // supplies the sky layer's deterministic vertical placement. `age` is
    // real elapsed seconds since spawn, used to time the grow-in every
    // instance gets. `sizeScale` is how big the object grows to: a real
    // peak's magnitude for a peak-triggered spawn, or a fixed modest value
    // for an ambient filler spawn -- the only difference between the two.
    struct Instance {
        bool active{false};
        StarGuitarObjectType type{StarGuitarObjectType::pole};
        uint32_t seed{};
        float traveled{};
        float age{};
        float sizeScale{1.0f};
    };

    // Per-layer scroll speed and ambient-fallback cadence. Peak-driven
    // spawning itself has no per-layer state -- the peak detectors below are
    // properties of the audio bands, not of a layer.
    struct Layer {
        float speed{};             // reference px / second
        float depthScale{};        // draws smaller/less detailed the farther back
        float baseIntervalSeconds{}; // ambient-fallback cadence when nothing peaks for a while
        float jitterSeconds{};
        float idleTimer{};
        std::array<Instance, kLayerCapacity> objects{};
        size_t nextSlot{};         // round-robin cursor for the next spawn to reuse
    };

    // Result of testing one sub-band for a peak this frame.
    struct Peak {
        bool fired{};
        float magnitude{}; // the sub-band's own 0..1 level at the moment it peaked
    };

    void analyzeSamples(float frameScale) noexcept;
    float nextRandom() noexcept;
    // Spawns into the next free slot in round-robin order; if every slot is
    // still occupied by an object that hasn't scrolled off screen yet, the
    // spawn is dropped rather than stomping (and visually truncating) that
    // still-active object.
    void spawnInstance(Layer& layer, StarGuitarObjectType type, float sizeScale) noexcept;
    // A sub-band "peaks" when its current level rises sharply relative to
    // its own recent baseline (not merely above some fixed number) and
    // enough time has passed since the last peak on that sub-band
    // (`cooldown`). This is the one rule every sub-band uses -- see
    // docs/STAR_GUITAR_FREQUENCY_BANDS.md.
    Peak detectPeak(float level, float baseline, float& cooldown, float cooldownSeconds,
                   float relativeThreshold, float floorLevel) noexcept;
    // Ambient fallback: spawns a small filler object on a jittered timer so a
    // layer doesn't sit empty through a stretch with no qualifying peak.
    // Always a fixed, modest size -- real peaks are what get to be big.
    void spawnAmbient(Layer& layer, StarGuitarObjectType type, float frameSeconds) noexcept;

    void drawGround(DrawList& output, float width, float height, float groundY) const;
    // `scale` (0..1+] scales every size-related measurement while keeping the
    // shape bottom/center-anchored, so a value below 1 reads as the object
    // still rising/growing in rather than a shrunken copy of the final shape.
    // Applied uniformly to every object type for consistency.
    void drawPole(DrawList& output, float baseX, float groundY, float unit, float scale) const;
    void drawTree(DrawList& output, float baseX, float groundY, float unit, uint32_t seed,
                 float scale) const;
    void drawPine(DrawList& output, float baseX, float groundY, float unit, uint32_t seed,
                 float scale) const;
    void drawBuilding(DrawList& output, float baseX, float groundY, float unit,
                      StarGuitarObjectType variant, uint32_t seed, float scale) const;
    void drawSignalMarker(DrawList& output, float baseX, float groundY, float unit,
                          float scale) const;
    void drawStar(DrawList& output, float baseX, float baseY, float unit, float scale) const;
    void drawUfo(DrawList& output, float baseX, float baseY, float unit, float scale) const;

    std::array<float, kMaxSamples> left_{};
    std::array<float, kMaxSamples> right_{};
    size_t sampleCount_{};
    std::atomic<uint32_t> sampleRate_{48000};

    // Eight sub-bands from seven cascaded one-pole low-pass filters at
    // increasing cutoffs; each sub-band is the residual between consecutive
    // filters (the last is the residual above the highest cutoff). See
    // docs/STAR_GUITAR_FREQUENCY_BANDS.md for the cutoffs and what each
    // sub-band is meant to capture and drive.
    std::array<float, kBandCount - 1> bandFilters_{};
    std::array<float, kBandCount> bandBaseline_{};  // slow baseline each peak test compares against
    std::array<float, kBandCount> bandCooldown_{};
    std::array<Peak, kBandCount> bandPeak_{};        // this frame's result, set by analyzeSamples()

    float overallLevel_{}; // mean of all 8 band targets this frame, for the silence gate

    float silenceSeconds_{}; // time the overall signal has stayed below the silence floor

    std::array<Layer, kLayerCount> layers_{};
    uint32_t spawnSerial_{};
    uint32_t randomState_{0x9f2c86adu};

    std::vector<Point> scratch_;
    StarGuitarOptions options_;
};

} // namespace builtin
} // namespace vizrack
