#pragma once

#include "builtin/draw_list.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace vizrack {

// Which trigger mechanism drives the mid layer's pole/tree pulse between
// confirmed low-band peaks (see docs/STAR_GUITAR_FREQUENCY_BANDS.md). Every
// layer reacts to a real peak the same way in both modes; this only selects
// what happens in the gaps between peaks.
enum class StarGuitarAlgorithmMode : uint8_t {
    // "실시간 인식": no tempo estimate. Gaps between confirmed low-band peaks
    // are filled by a jittered timer keyed to the song's general mid-band
    // busyness.
    reactive,
    // "알고리즘 방식": estimates the song's tempo from recent low-band peak
    // intervals and fills the gaps on the predicted beat phase instead, so
    // the cadence stays locked to the beat. Falls back to reactive's cadence
    // until a lock is acquired.
    predictive,
};

struct StarGuitarOptions {
    StarGuitarAlgorithmMode algorithmMode{StarGuitarAlgorithmMode::reactive};
};

namespace builtin {

enum class StarGuitarObjectType : uint8_t {
    pole,
    tree,
    buildingA,
    buildingB,
    buildingC,
    waterTower,
    signalMarker,
    bird,
    plane,
    star,
};

// A "Star Guitar" style rhythmic landscape, referencing the Chemical
// Brothers video: several depth layers of scenery scroll past at different
// speeds (parallax). What spawns, when, how big and in which layer is driven
// by one idea applied uniformly across four frequency bands (see
// docs/STAR_GUITAR_FREQUENCY_BANDS.md): a band's *relative* rise over its
// own recent baseline -- not an absolute level -- marks a peak; the peak's
// height sets the spawned object's size, and which band peaked selects the
// object type/layer. Every spawn animates in with the same grow-from-ground
// motion, so the distinction between "this is a real hit" and "this is
// ambient filler" is expressed only through size, never through whether it
// animates at all.
class StarGuitarEngine {
public:
    static constexpr size_t kMaxSamples = 4096;
    // Independent depth layers, far to near, plus a sky layer for sparse
    // birds/planes/stars. Far is slowest/smallest, near is fastest/largest,
    // selling parallax depth; sky scrolls independently of the ground stack.
    static constexpr size_t kLayerCount = 4;
    static constexpr size_t kFarLayer = 0;
    static constexpr size_t kMidLayer = 1;
    static constexpr size_t kNearLayer = 2;
    static constexpr size_t kSkyLayer = 3;
    // Fixed per-layer object capacity: bounds simultaneous on-screen objects
    // (and therefore draw commands) regardless of audio activity. Generous
    // relative to the far layer's crossing time so a busy passage doesn't
    // silently starve it of new spawns until old ones scroll off (that was a
    // real bug: capacity was tight enough that a normal kick tempo could
    // exhaust it well before objects reached the left edge).
    static constexpr size_t kLayerCapacity = 40;

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
    // properties of the audio, not of a layer.
    struct Layer {
        float speed{};             // reference px / second
        float depthScale{};        // draws smaller/less detailed the farther back
        float baseIntervalSeconds{}; // ambient-fallback cadence when nothing peaks for a while
        float jitterSeconds{};
        float idleTimer{};
        std::array<Instance, kLayerCapacity> objects{};
        size_t nextSlot{};         // round-robin cursor for the next spawn to reuse
    };

    // Result of testing one band for a peak this frame.
    struct Peak {
        bool fired{};
        float magnitude{}; // the band's own 0..1 level at the moment it peaked
    };

    void analyzeSamples(float frameScale) noexcept;
    float nextRandom() noexcept;
    // Spawns into the next free slot in round-robin order; if every slot is
    // still occupied by an object that hasn't scrolled off screen yet, the
    // spawn is dropped rather than stomping (and visually truncating) that
    // still-active object.
    void spawnInstance(Layer& layer, StarGuitarObjectType type, float sizeScale) noexcept;
    // A band "peaks" when its current level rises sharply relative to its
    // own recent baseline (not merely above some fixed number) and enough
    // time has passed since the last peak on that band (`cooldown`). This is
    // the one rule every band uses -- see docs/STAR_GUITAR_FREQUENCY_BANDS.md.
    Peak detectPeak(float level, float baseline, float& cooldown, float cooldownSeconds,
                   float relativeThreshold, float floorLevel) noexcept;
    // Ambient fallback: spawns a small filler object on a jittered timer so a
    // layer doesn't sit empty through a stretch with no qualifying peak.
    // Always a fixed, modest size -- real peaks are what get to be big.
    void spawnAmbient(Layer& layer, StarGuitarObjectType type, float frameSeconds) noexcept;
    // Tracks the song's tempo from confirmed low-band peaks (the "쿵"/kick):
    // keeps a short history of inter-peak intervals, estimates the beat
    // period from their median (robust to an occasional missed or doubled
    // peak), and re-syncs its phase to zero on every confirmed peak so it
    // never drifts away from the real audio. The lock expires if too long
    // passes without a confirmed peak, so a stale tempo from one section
    // can't keep driving spawns into a different or silent one.
    void updateTempoTracker(bool lowPeakFired, float frameSeconds) noexcept;
    // The mid layer's steady pulse: spawns immediately on every confirmed
    // low-band peak (landing alongside the far layer's own kick reaction, in
    // both modes), sized by the peak's magnitude. In predictive mode, once a
    // tempo estimate exists, also fills the gaps on the estimated beat phase.
    // In reactive mode -- or before a lock is acquired -- falls back to the
    // ambient timer instead.
    void updateMidPulse(Layer& layer, Peak lowPeak, StarGuitarAlgorithmMode mode,
                        StarGuitarObjectType primaryType, StarGuitarObjectType secondaryType,
                        float frameSeconds) noexcept;
    // Sky trigger: must be driven by the air band (hi-hats/cymbals/shimmer,
    // the highest band) and nothing lower -- see
    // docs/STAR_GUITAR_FREQUENCY_BANDS.md. Weighted heavily toward stars
    // (a light, frequent twinkle) with birds/planes as rarer flourishes.
    void spawnSky(Layer& layer, Peak airPeak) noexcept;

    void drawGround(DrawList& output, float width, float height, float groundY) const;
    // `scale` (0..1+] scales every size-related measurement while keeping the
    // shape bottom/center-anchored, so a value below 1 reads as the object
    // still rising/growing in rather than a shrunken copy of the final shape.
    // Applied uniformly to every object type for consistency.
    void drawPole(DrawList& output, float baseX, float groundY, float unit, float scale) const;
    void drawTree(DrawList& output, float baseX, float groundY, float unit, uint32_t seed,
                 float scale) const;
    void drawBuilding(DrawList& output, float baseX, float groundY, float unit,
                      StarGuitarObjectType variant, uint32_t seed, float scale) const;
    void drawWaterTower(DrawList& output, float baseX, float groundY, float unit, uint32_t seed,
                        float scale);
    void drawSignalMarker(DrawList& output, float baseX, float groundY, float unit,
                          float scale) const;
    void drawBird(DrawList& output, float baseX, float baseY, float unit, float scale) const;
    void drawPlane(DrawList& output, float baseX, float baseY, float unit, float scale) const;
    void drawStar(DrawList& output, float baseX, float baseY, float unit, float scale) const;

    std::array<float, kMaxSamples> left_{};
    std::array<float, kMaxSamples> right_{};
    size_t sampleCount_{};
    std::atomic<uint32_t> sampleRate_{48000};

    // Four frequency bands from three cascaded one-pole low-pass filters
    // (cutoffs at lowFilter_ < midFilter_ < presenceFilter_); each band is
    // the residual between consecutive filters. See
    // docs/STAR_GUITAR_FREQUENCY_BANDS.md for why these specific splits.
    float lowFilter_{};       // cutoff ~150Hz
    float midFilter_{};       // cutoff ~2.5kHz
    float presenceFilter_{};  // cutoff ~6kHz

    float lowLevel_{};       // <150Hz sustained level: kick/sub-bass ("쿵" source)
    float midLevel_{};       // 150Hz-2.5kHz sustained level: vocals/guitars/keys (busyness only;
                              // deliberately excluded from peak detection -- see analyzeSamples)
    float presenceLevel_{};  // 2.5-6kHz sustained level: snare/clap snap, vocal sibilance
    float airLevel_{};       // >6kHz sustained level: hi-hats/cymbals/shimmer

    // Slow-moving baselines each band's peak detector compares against.
    float lowBaseline_{};
    float presenceBaseline_{};
    float airBaseline_{};

    // Per-band peak cooldowns. These belong to the audio band, not to any
    // one layer -- multiple layers can react to the same band's peak.
    float lowPeakCooldown_{};
    float presencePeakCooldown_{};
    float airPeakCooldown_{};

    // This frame's peak result per band, computed once in analyzeSamples()
    // and consumed by every layer that reacts to that band.
    Peak lowPeak_{};
    Peak presencePeak_{};
    Peak airPeak_{};

    float songEnergy_{};     // slow (several-second) macro loudness average
    float silenceSeconds_{}; // time the overall signal has stayed below the silence floor

    // Tempo tracking (predictive mode only), driven entirely by confirmed
    // low-band peaks -- see updateTempoTracker().
    float songClock_{};             // free-running seconds, only differences are meaningful
    float lastLowPeakTime_{-1.0f};  // songClock_ value at the previous confirmed peak, or -1
    float beatPeriodSeconds_{0.5f}; // estimated seconds per beat once locked
    float beatPhase_{};             // 0..1, wraps once per estimated beat once locked
    bool tempoLocked_{false};
    static constexpr size_t kPeakHistory = 6;
    std::array<float, kPeakHistory> peakIntervals_{};
    size_t peakIntervalCount_{};
    size_t peakIntervalCursor_{};

    std::array<Layer, kLayerCount> layers_{};
    uint32_t spawnSerial_{};
    uint32_t randomState_{0x9f2c86adu};

    std::vector<Point> scratch_;
    StarGuitarOptions options_;
};

} // namespace builtin
} // namespace vizrack
