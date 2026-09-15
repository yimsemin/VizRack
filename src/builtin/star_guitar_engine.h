#pragma once

#include "builtin/draw_list.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace vizrack {

// Which trigger mechanism drives the mid layer's pole/tree pulse (see
// docs/STAR_GUITAR_FREQUENCY_BANDS.md for the reasoning). Every other layer
// (far/near/sky) is onset-reactive in both modes; this only selects between
// the mid layer's two cadences.
enum class StarGuitarAlgorithmMode : uint8_t {
    // "실시간 인식": the original approach. The mid layer reacts only to what
    // just happened -- a direct spawn on every confirmed kick onset, filled
    // in between hits by a jittered timer keyed to the song's general mid-
    // band busyness. No tempo estimate is computed or used.
    reactive,
    // "알고리즘 방식": estimates the song's tempo from recent kick onsets and
    // fills in the mid layer's pulse on the predicted beat phase between
    // hits, so the cadence stays locked to the beat instead of drifting on
    // its own timer. Falls back to the reactive cadence until a tempo lock
    // is acquired.
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

// A "Star Guitar" style rhythmic landscape, referencing the Chemical Brothers
// video: several depth layers of scenery scroll past at different speeds
// (parallax), each spawning its own objects on its own audio-driven cue
// rather than all layers sharing one fixed-interval grid. Frequency-band
// choices are documented in docs/STAR_GUITAR_FREQUENCY_BANDS.md.
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
    // (and therefore draw commands) regardless of audio activity.
    static constexpr size_t kLayerCapacity = 16;

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
    // real elapsed seconds since spawn (independent of layer speed), used
    // only to time the spawn-moment accent flash below.
    struct Instance {
        bool active{false};
        // True when this instance was spawned directly by a confirmed
        // rhythmic onset (a real "쿵"/"짝"/hi-hat hit), as opposed to an
        // ambient/interval fallback spawn. Only accented instances get the
        // spawn-moment flash -- the point is for the object's arrival itself
        // to read as a struck beat note, not a generic decoration.
        bool accented{false};
        StarGuitarObjectType type{StarGuitarObjectType::pole};
        uint32_t seed{};
        float traveled{};
        float age{};
    };

    // Per-layer scroll speed, spawn cadence and trigger state. Each layer is
    // driven by its own slice of the shared frequency-band analysis rather
    // than a shared beat grid, so layers spawn independently and can overlap.
    struct Layer {
        float speed{};             // reference px / second
        float depthScale{};        // draws smaller/less detailed the farther back
        float baseIntervalSeconds{};
        float jitterSeconds{};
        float spawnCooldown{};     // seconds until another onset-triggered spawn is allowed
        float secondaryCooldown{}; // seconds until another secondary-onset spawn is allowed
        float idleTimer{};         // counts down to a fallback ambient spawn / next interval spawn
        std::array<Instance, kLayerCapacity> objects{};
        size_t nextSlot{};         // round-robin cursor for the next spawn to reuse
    };

    void analyzeSamples(float frameScale) noexcept;
    float nextRandom() noexcept;
    // Spawns into the next free slot in round-robin order; if every slot is
    // still occupied by an object that hasn't scrolled off screen yet, the
    // spawn is dropped rather than stomping (and visually truncating) that
    // still-active object. `accented` marks a real onset-triggered spawn (as
    // opposed to an ambient fallback) for the spawn-moment flash in buildFrame.
    void spawnInstance(Layer& layer, StarGuitarObjectType type, bool accented) noexcept;
    // Debounced rising-edge check shared by every onset-driven trigger: fires
    // when `rise` crosses `threshold` and at least `cooldownSeconds` (plus a
    // little jitter) has passed since the last firing tracked in `cooldown`.
    bool detectOnset(float rise, float threshold, float& cooldown,
                     float cooldownSeconds) noexcept;
    // Spawns `onsetType` when `onset` is true (already debounced by the
    // caller); otherwise falls back to an ambient spawn after a jittered
    // idle period so the layer is never permanently empty during a quiet-
    // but-audible passage. The caller suspends this entirely during silence.
    void updateEdgeTriggeredLayer(Layer& layer, bool onset,
                                  StarGuitarObjectType onsetType,
                                  StarGuitarObjectType ambientType,
                                  float frameSeconds) noexcept;
    // Interval trigger: spawns on a randomized (jittered) timer whose period
    // shortens as `sustainedLevel` rises, used for the mid layer's
    // pole/tree cadence in reactive mode so it tracks general busyness
    // without being a strict metronome.
    void updateIntervalLayer(Layer& layer, float sustainedLevel,
                             StarGuitarObjectType primaryType,
                             StarGuitarObjectType secondaryType,
                             float frameSeconds) noexcept;
    // Secondary onset trigger layered on top of the mid layer: gives a
    // distinct, immediately-recognisable marker on the second rhythmic voice
    // (the "짝"/snare-like presence-band onset) without disturbing the
    // primary pulse.
    void updateSecondaryOnset(Layer& layer, float rise, float threshold,
                              StarGuitarObjectType type, float frameSeconds) noexcept;
    // Tracks the song's tempo from confirmed low-band onsets (the "쿵"/kick):
    // keeps a short history of inter-onset intervals, estimates the beat
    // period from their median (robust to an occasional missed or doubled
    // hit), and re-syncs its phase to zero on every confirmed onset so it
    // never drifts away from the real audio. The lock expires (falls back to
    // untracked) if too long passes without a confirmed onset, so a stale
    // tempo from one section can't keep driving spawns into a different or
    // silent one. Returns true if a confirmed onset happened this call.
    bool updateTempoTracker(float lowRise, float frameSeconds) noexcept;
    // The mid layer's steady pulse: always spawns immediately on every
    // confirmed low-band onset (so poles/trees visibly land together with
    // the far layer's kick reaction, in both modes). In predictive mode, once
    // a tempo estimate exists, also fills in on the estimated beat phase
    // between onsets so the cadence doesn't stall if a soft hit is missed. In
    // reactive mode -- or before a lock is acquired -- falls back to the
    // jittered interval timer instead.
    void updateMidLayerBeatLocked(Layer& layer, bool confirmedOnset,
                                  StarGuitarAlgorithmMode mode,
                                  StarGuitarObjectType primaryType,
                                  StarGuitarObjectType secondaryType,
                                  float frameSeconds) noexcept;
    // Sky trigger: must be driven by the air band (hi-hats/cymbals/shimmer,
    // the highest band) and nothing lower -- see
    // docs/STAR_GUITAR_FREQUENCY_BANDS.md. A long minimum spacing keeps it a
    // rare flourish even in a hi-hat-heavy mix; a song with no air-band
    // presence at all simply never spawns anything here.
    void updateSkyLayer(Layer& layer, float airRise, float frameSeconds) noexcept;

    void drawGround(DrawList& output, float width, float height, float groundY) const;
    void drawPole(DrawList& output, float baseX, float groundY, float unit) const;
    void drawTree(DrawList& output, float baseX, float groundY, float unit,
                 uint32_t seed) const;
    void drawBuilding(DrawList& output, float baseX, float groundY, float unit,
                      StarGuitarObjectType variant, uint32_t seed) const;
    void drawWaterTower(DrawList& output, float baseX, float groundY, float unit,
                        uint32_t seed);
    void drawSignalMarker(DrawList& output, float baseX, float groundY, float unit) const;
    // The spawn-moment "note attack": a brief bright glow drawn on top of an
    // accented instance for its first fraction of a second, fading out as
    // `ageFraction` (0 at spawn, 1 at the end of the flash window) rises.
    // This is what should make the object's arrival itself read as a struck
    // beat rather than scenery that simply appeared.
    void drawAccentFlash(DrawList& output, float baseX, float baseY, float unit,
                         float ageFraction) const;
    void drawBird(DrawList& output, float baseX, float baseY, float unit) const;
    void drawPlane(DrawList& output, float baseX, float baseY, float unit) const;
    void drawStar(DrawList& output, float baseX, float baseY, float unit) const;

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
    float midLevel_{};       // 150Hz-2.5kHz sustained level: vocals/guitars/keys (busyness only)
    float presenceLevel_{};  // 2.5-6kHz sustained level: snare/clap snap, vocal sibilance
    float airLevel_{};       // >6kHz sustained level: hi-hats/cymbals/shimmer

    float lowAverage_{};
    float presenceAverage_{};
    float airAverage_{};
    float lowBeatLevel_{};       // onset envelope, low band (kick-like, the "쿵")
    float presenceBeatLevel_{};  // onset envelope, presence band (snare/clap-like, the "짝")
    float airBeatLevel_{};       // onset envelope, air band (hi-hat/cymbal/shimmer-like)

    float songEnergy_{};     // slow (several-second) macro loudness average, for the sky layer
    float silenceSeconds_{}; // time the overall signal has stayed below the silence floor

    // Tempo tracking (predictive mode only), driven entirely by confirmed
    // low-band ("쿵") onsets -- see updateTempoTracker().
    float songClock_{};             // free-running seconds, only differences are meaningful
    float tempoOnsetCooldown_{};    // debounce for what counts as one confirmed onset
    float lastLowOnsetTime_{-1.0f}; // songClock_ value at the previous confirmed onset, or -1
    float beatPeriodSeconds_{0.5f}; // estimated seconds per beat once locked
    float beatPhase_{};             // 0..1, wraps once per estimated beat once locked
    bool tempoLocked_{false};
    static constexpr size_t kOnsetHistory = 6;
    std::array<float, kOnsetHistory> onsetIntervals_{};
    size_t onsetIntervalCount_{};
    size_t onsetIntervalCursor_{};

    std::array<Layer, kLayerCount> layers_{};
    uint32_t spawnSerial_{};
    uint32_t randomState_{0x9f2c86adu};

    std::vector<Point> scratch_;
    StarGuitarOptions options_;
};

} // namespace builtin
} // namespace vizrack
