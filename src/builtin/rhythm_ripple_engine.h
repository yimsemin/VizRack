#pragma once

#include "builtin/draw_list.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace vizrack {

// Two independent sensitivities, both 0-100 with the same "higher fires
// more easily" direction:
//  - `sensitivity` is the broadband raindrop onset detector's peak-detection
//    sensitivity, same semantics as StarGuitarOptions' per-band sliders.
//  - `longNoteSensitivity` controls the separate long-note sustain test (see
//    RhythmRippleEngine): how much a sustained level is allowed to sag and
//    still count as "still held". 0 turns long notes off entirely -- there
//    is no separate on/off flag, since a fully-insensitive sustain test and
//    "never try" are the same thing.
struct RhythmRippleOptions {
    int sensitivity{50};
    int longNoteSensitivity{0};
};

namespace builtin {

// "Rhythm Ripple": rain falling on a still, dark puddle in time with the
// music. Every accepted onset spawns exactly one raindrop-ripple at a fully
// random point on the surface -- there is no lane, no single "current beat"
// position, and no limit to one at a time: a busy passage can and should
// drop several raindrops in close succession, just like real rain speeding
// up with the music.
//
// A single broadband onset detector (relative rise over a slow baseline,
// same technique as StarGuitarEngine::detectPeak) decides when a raindrop
// falls; a short fixed cooldown only prevents the exact same transient from
// re-firing across consecutive frames. There is deliberately no tempo
// estimation or "one beat at a time" pacing here -- rain does not wait its
// turn.
class RhythmRippleEngine {
public:
    static constexpr size_t kMaxSamples = 4096;
    // Fixed ripple capacity: several raindrops can be visible at once during
    // a busy passage (each lives under a second), but the count is always
    // bounded regardless of how fast onsets are accepted.
    static constexpr size_t kMaxRipples = 24;
    // Several long notes may be held at once during a busy, sustained
    // passage -- bounded the same way the raindrop pool is.
    static constexpr size_t kMaxHeldNotes = 4;

    RhythmRippleEngine();

    void setOptions(RhythmRippleOptions options) noexcept;
    RhythmRippleOptions options() const noexcept { return options_; }
    void setSampleRate(uint32_t sampleRate) noexcept;
    std::span<float> inputLeft() noexcept { return left_; }
    std::span<float> inputRight() noexcept { return right_; }
    void update(size_t sampleCount, float frameSeconds = 1.0f / 60.0f) noexcept;
    void reset() noexcept;

    void buildFrame(float width, float height, DrawList& output);

private:
    // A single raindrop-ripple lifecycle: spawned at one fully random
    // reference-frame position (see kReferenceWidth/kReferenceHeight in the
    // .cpp), it grows through a fixed real-time lifetime and is then
    // reclaimed. `magnitude` is the onset's own 0..1 level at the moment it
    // was accepted, used to scale the impact flash and ring brightness so a
    // harder hit reads bigger. `seed` drives the splash droplets' angles so
    // each raindrop's splash looks distinct.
    struct Ripple {
        bool active{false};
        float x{};
        float y{};
        float age{};
        float magnitude{1.0f};
        uint32_t seed{};
    };

    // A "long note": one sustained ripple source that lasts as long as the
    // real audio event does (see updateHeldNotes for how that is judged),
    // capped at kHeldMaxDurationSeconds so a single hold can't run away
    // indefinitely. Unlike a raindrop it never re-bursts while held --
    // exactly one ripple marks its start and one marks its end -- and it
    // travels at a constant speed along one single gentle arc (a fixed
    // angular velocity chosen once at spawn, not a wandering random walk),
    // so its motion reads as one smooth curve rather than jitter. A short
    // fading trail of recent positions is kept purely for drawing that
    // curve. Several of these can be alive at once (see kMaxHeldNotes) -- a
    // busy, sustained passage can hold more than one at a time, the same way
    // a busy passage can drop several raindrops in close succession.
    struct HeldNote {
        static constexpr size_t kTrailPointCount = 48;

        bool active{false};
        float x{};
        float y{};
        float heading{};        // radians; advances at a fixed angularVelocity
        float angularVelocity{}; // radians/second, fixed for the note's whole life
        float age{};
        float belowThresholdSeconds{}; // this note's own release debounce accumulator
        // Frozen at confirmation: the onset level must stay above this floor
        // (a fraction of the level that started it) or the note releases.
        // Fixed per-note rather than recomputed, so a note's own release
        // criterion doesn't shift while it's already playing.
        float releaseFloor{};
        uint32_t seed{};
        std::array<Point, kTrailPointCount> trail{};
        size_t trailCount{};
        size_t trailNext{};
        float trailSampleSeconds{};
    };

    void analyzeSamples(float frameScale) noexcept;
    float nextRandom() noexcept;
    // Spawns a raindrop at an explicit position (used both by the random
    // raindrop placement in update() and by a held long note's start/end
    // markers).
    void spawnRippleAt(float x, float y, float magnitude) noexcept;
    // Sustain detection (a candidate window that confirms into a genuine
    // hold only if the level stays up rather than decaying like a normal
    // hit, and releases only when the level actually drops or the max
    // duration is hit) plus every currently-held note's own constant-speed
    // arc motion; called once per update(). `rawOnset` doubles as "a
    // raindrop is landing at (rawOnsetX, rawOnsetY) this very frame": a note
    // that happens to release on the same frame shares that spot instead of
    // spawning its own end ripple elsewhere, so the two visually coincide.
    void updateHeldNotes(float safeSeconds, bool rawOnset, float rawMagnitude, float rawOnsetX,
                        float rawOnsetY) noexcept;
    void drawBackground(DrawList& output, float width, float height) const;
    void drawRipple(DrawList& output, float screenX, float screenY, float minDimension,
                    const Ripple& ripple) const;
    void drawHeldNote(DrawList& output, float scaleX, float scaleY, float minDimension,
                      const HeldNote& note, float glowLevel);

    std::array<float, kMaxSamples> left_{};
    std::array<float, kMaxSamples> right_{};
    size_t sampleCount_{};
    std::atomic<uint32_t> sampleRate_{48000};

    // Broadband onset envelope: one one-pole low-pass (~5kHz, to de-
    // emphasize pure hiss/cymbal wash) feeds a fast/slow baseline follower
    // pair, the same relative-rise-over-baseline technique StarGuitarEngine
    // uses per sub-band, applied here to one global band.
    float lowpassFilter_{};
    float onsetLevel_{};
    float onsetBaseline_{};
    float onsetCooldown_{}; // short fixed cooldown against literal re-firing on consecutive frames

    float overallLevel_{}; // mirrors onsetLevel_'s target, drives the silence gate
    float silenceSeconds_{};

    // Running clock, accumulated from frameSeconds like StarGuitarEngine's
    // safeSeconds pattern (the engine has no wall clock of its own).
    float clockSeconds_{};

    std::array<Ripple, kMaxRipples> ripples_{};
    size_t nextRippleSlot_{};
    uint32_t spawnSerial_{};
    uint32_t randomState_{0xc2b2ae35u};

    // Sustain (long-note) candidate window: every accepted raw onset (the
    // same broadband detector that drops raindrops) opens one candidate,
    // which confirms into a genuine held note only if the level keeps
    // staying up near its starting peak for kSustainConfirmSeconds instead
    // of decaying away the way an ordinary percussive hit does. There is at
    // most one candidate pending at a time -- see kSustain* constants and
    // updateHeldNotes in the .cpp.
    bool sustainCandidateActive_{};
    float sustainCandidatePeakLevel_{};
    float sustainCandidateElapsedSeconds_{};
    std::array<HeldNote, kMaxHeldNotes> heldNotes_{};
    size_t nextHeldSlot_{};
    // Scratch buffer for building a held note's trail PointRange each frame
    // in drawHeldNote, reused rather than reallocated (StarGuitarEngine uses
    // the same pattern for its water tower's polygon points).
    std::vector<Point> scratch_;

    RhythmRippleOptions options_;
};

} // namespace builtin
} // namespace vizrack
