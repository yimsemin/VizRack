#include "builtin/rhythm_ripple_engine.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace vizrack::builtin {
namespace {

constexpr float kPi = std::numbers::pi_v<float>;

// Reference frame ripples are placed in before being scaled (independently
// per axis) to the actual render size -- same idea as StarGuitarEngine's
// reference-width scroll grid, extended to both axes since a ripple's
// position (not just its scroll speed) matters here.
constexpr float kReferenceWidth = 1920.0f;
constexpr float kReferenceHeight = 1080.0f;
// Keeps a fresh spawn from landing clipped at the very edge of the frame.
constexpr float kEdgeMargin = 130.0f;

// Broadband onset envelope tuning. A single one-pole low-pass below ~5kHz
// keeps pure hiss/cymbal wash from dominating the broadband energy the
// onset detector reacts to, per the visualizer's intent of tracking beats,
// not high-frequency noise. Gain/floor are tuned generously (and the
// baseline's attack is deliberately sluggish, see frameFollow calls below)
// so that at high sensitivity settings rain falls often and readily, rather
// than the detector self-limiting because its own baseline chases the
// signal too closely to ever see a "rise" again.
constexpr float kLowpassCutoffHz = 5000.0f;
constexpr float kOnsetGain = 5.5f;
constexpr float kOnsetFloor = 0.045f;
// A short fixed cooldown on the raw detector, only to stop the exact same
// transient from re-firing across consecutive frames -- short enough that
// genuinely fast, busy rain can still trigger many raindrops per second.
constexpr float kRawCooldownSeconds = 0.045f;

// Silence gate, same shape as StarGuitarEngine's: below this the source has
// effectively stopped, so no ripple spawns and onset-interval history is not
// allowed to accumulate a poisoned gap.
constexpr float kSilenceLevel = 0.03f;
constexpr float kSilenceHoldSeconds = 0.4f;

// Ripple visual lifetime and impact flash timing.
constexpr float kRippleLifetimeSeconds = 0.85f;
constexpr float kFlashLifetimeSeconds = 0.12f;
constexpr int kRingCount = 3;
constexpr float kRingStartDelaySeconds = 0.055f; // stagger between successive rings' start

// Splash droplets: a handful of tiny flecks kicked outward at the moment of
// impact, the way a real raindrop throws up a small crown of droplets. Purely
// cosmetic, layered on top of the flash+rings to sell "something just landed
// here" rather than "a ring appeared here".
constexpr int kSplashDropletCount = 5;
constexpr float kSplashLifetimeSeconds = 0.16f;
constexpr float kSplashDistanceFraction = 0.045f; // of the shorter screen dimension

// Rebound: the small center droplet that bounces back up an instant after
// impact, visible in any slow-motion raindrop footage -- a brief second dot
// at the impact point, after the first ring has started to clear it.
constexpr float kReboundStartSeconds = 0.16f;
constexpr float kReboundLifetimeSeconds = 0.14f;

// Long-note sustain detection: every accepted raw onset (the same broadband
// detector that drops a raindrop) also opens one "is this actually a hold?"
// candidate window. An ordinary percussive hit's level decays away within a
// fraction of a second; a genuinely held note (a drone, a sustained chord, a
// vocal note) keeps the level up near where it started. So the candidate
// simply watches whether onsetLevel_ stays above a fraction of its own
// starting peak for kSustainConfirmSeconds -- if it ever dips below that
// floor first, the candidate is just a raindrop and is dropped; if it stays
// up long enough, it confirms into a real held note, which then keeps living
// (no fixed or capped duration) until the level actually falls below that
// same floor for kSustainReleaseSeconds (or the max duration below is hit,
// whichever comes first). Because this judges every single onset fresh
// against its own peak -- never against a slowly-adapting average of the
// whole song -- it keeps firing evenly throughout a track instead of only
// near a structural mood change. sustainHoldFractionFromSensitivity's curve
// controls how forgiving "stays up" is: more sensitive tolerates more decay
// before disqualifying a candidate (and disqualifies a held note's release
// less eagerly), so turning longNoteSensitivity up makes long notes both
// easier to start and slower to end, not just more numerous. A
// longNoteSensitivity of exactly 0 skips the whole mechanism -- see
// updateHeldNotes -- which is how the feature is turned off.
constexpr float kSustainConfirmSeconds = 0.30f;  // must stay up this long to confirm a hold
constexpr float kSustainReleaseSeconds = 0.30f;  // must stay down this long to end one
// A hold never lasts longer than this regardless of how long the passage
// keeps qualifying -- long enough to read as a real sustained gesture, short
// enough that the visualizer never looks stuck on one hold.
constexpr float kHeldMaxDurationSeconds = 5.0f;

// Long-note motion: constant forward speed plus one fixed angular velocity
// chosen at spawn (never re-randomized while held), so the whole path traces
// a single smooth arc -- one gentle curve, not a wandering, direction-
// changing wiggle. `kHeldMaxAngularVelocity` is small enough that even a
// note held for several seconds only sweeps a modest arc, not a full loop.
constexpr float kHeldSpeed = 240.0f;                    // reference px/s, constant for the note's life
constexpr float kHeldMaxAngularVelocity = 0.45f;        // radians/second
constexpr float kHeldEdgeMargin = 110.0f;
constexpr float kHeldTrailSampleSeconds = 0.05f;        // cadence of trail-curve position sampling

float finiteSample(float value) noexcept {
    return std::isfinite(value) ? value : 0.0f;
}

float clampUnit(float value) noexcept {
    return std::clamp(value, 0.0f, 1.0f);
}

float frameFollow(float current, float target, float attack, float release,
                  float frameScale) noexcept {
    const float base = target > current ? attack : release;
    const float coefficient = 1.0f - std::pow(1.0f - base, frameScale);
    return current + (target - current) * coefficient;
}

uint32_t hashCombine(uint32_t a, uint32_t b) noexcept {
    uint32_t value = a * 0x9e3779b9u + b;
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    value *= 0x846ca68bu;
    value ^= value >> 16;
    return value;
}

float hash01(uint32_t value) noexcept {
    return static_cast<float>(value & 0x00ffffffu) / 16777215.0f;
}

// Eased-out growth: fast at first, decelerating toward the end, the way a
// real ripple's expansion slows as it travels outward.
float easeOutCubic(float t) noexcept {
    t = clampUnit(t);
    const float inverse = 1.0f - t;
    return 1.0f - inverse * inverse * inverse;
}

// The raw (uncalibrated) 0-100 -> threshold curve, identical shape to
// StarGuitarEngine's rawThresholdFromSensitivity: 0 is very insensitive (a
// huge relative jump needed), 100 is very sensitive (a small jump is
// enough).
float thresholdFromSensitivity(int sensitivity) noexcept {
    const float t = std::clamp(static_cast<float>(sensitivity), 0.0f, 100.0f) / 100.0f;
    return 1.05f + (0.09f - 1.05f) * t;
}

// How much of its own starting peak a long-note candidate (and, later, an
// already-confirmed hold) must keep for the level to still count as "still
// sustaining" -- see kSustain* above. Higher sensitivity lowers this fraction
// (more decay is tolerated before it's judged to have ended), so turning
// sensitivity up makes long notes both easier to start and slower to end.
float sustainHoldFractionFromSensitivity(int sensitivity) noexcept {
    const float t = std::clamp(static_cast<float>(sensitivity), 0.0f, 100.0f) / 100.0f;
    return 0.80f + (0.32f - 0.80f) * t;
}

} // namespace

RhythmRippleEngine::RhythmRippleEngine() {
    scratch_.reserve(HeldNote::kTrailPointCount);
}

void RhythmRippleEngine::setOptions(RhythmRippleOptions options) noexcept {
    options.sensitivity = std::clamp(options.sensitivity, 0, 100);
    options.longNoteSensitivity = std::clamp(options.longNoteSensitivity, 0, 100);
    options_ = options;
}

void RhythmRippleEngine::setSampleRate(uint32_t sampleRate) noexcept {
    if (sampleRate >= 8000 && sampleRate <= 768000) {
        sampleRate_.store(sampleRate, std::memory_order_release);
    }
}

void RhythmRippleEngine::update(size_t sampleCount, float frameSeconds) noexcept {
    const float safeSeconds = std::isfinite(frameSeconds)
                                  ? std::clamp(frameSeconds, 1.0f / 240.0f, 1.0f / 10.0f)
                                  : 1.0f / 60.0f;
    const float frameScale = safeSeconds * 60.0f;
    clockSeconds_ += safeSeconds;

    onsetCooldown_ = std::max(0.0f, onsetCooldown_ - safeSeconds);

    bool rawOnset = false;
    float rawMagnitude = 0.0f;
    float rawOnsetX = 0.0f;
    float rawOnsetY = 0.0f;
    if (sampleCount > 0) {
        sampleCount_ = std::min(sampleCount, kMaxSamples);
        analyzeSamples(frameScale);
        const float relativeRise = (onsetLevel_ - onsetBaseline_) / (onsetBaseline_ + 0.05f);
        const float threshold = thresholdFromSensitivity(options_.sensitivity);
        if (onsetLevel_ > kOnsetFloor && onsetCooldown_ <= 0.0f && relativeRise > threshold) {
            rawOnset = true;
            rawMagnitude = onsetLevel_;
            onsetCooldown_ = kRawCooldownSeconds;
            // Picked up front (rather than inside spawnRippleAt) so a long
            // note releasing on this same frame can share this exact spot --
            // see updateHeldNotes.
            rawOnsetX = kEdgeMargin + nextRandom() * (kReferenceWidth - kEdgeMargin * 2.0f);
            rawOnsetY = kEdgeMargin + nextRandom() * (kReferenceHeight - kEdgeMargin * 2.0f);
        }
    } else {
        onsetBaseline_ *= std::pow(0.985f, frameScale);
        onsetLevel_ *= std::pow(0.96f, frameScale);
        overallLevel_ *= std::pow(0.96f, frameScale);
    }

    if (overallLevel_ < kSilenceLevel) {
        silenceSeconds_ += safeSeconds;
    } else {
        silenceSeconds_ = 0.0f;
    }
    const bool audible = silenceSeconds_ < kSilenceHoldSeconds;

    for (auto& ripple : ripples_) {
        if (!ripple.active) continue;
        ripple.age += safeSeconds;
        if (ripple.age >= kRippleLifetimeSeconds) ripple.active = false;
    }

    // Runs every frame regardless of the silence gate below, so a held note
    // still winds down (via its own sustain-level release debounce) rather
    // than freezing mid-drift the instant the source goes silent.
    updateHeldNotes(safeSeconds, rawOnset, rawMagnitude, rawOnsetX, rawOnsetY);

    if (!audible) return;

    // Rain does not pace itself against a beat estimate -- every accepted
    // onset (already throttled by the raw cooldown above) drops exactly one
    // raindrop, and several can be falling in close succession during a busy
    // passage.
    if (!rawOnset) return;
    spawnRippleAt(rawOnsetX, rawOnsetY, rawMagnitude);
}

void RhythmRippleEngine::reset() noexcept {
    sampleCount_ = 0;
    lowpassFilter_ = 0.0f;
    onsetLevel_ = 0.0f;
    onsetBaseline_ = 0.0f;
    onsetCooldown_ = 0.0f;
    overallLevel_ = 0.0f;
    silenceSeconds_ = 0.0f;
    clockSeconds_ = 0.0f;
    ripples_ = {};
    nextRippleSlot_ = 0;
    spawnSerial_ = 0;
    randomState_ = 0xc2b2ae35u;
    sustainCandidateActive_ = false;
    sustainCandidatePeakLevel_ = 0.0f;
    sustainCandidateElapsedSeconds_ = 0.0f;
    heldNotes_ = {};
    nextHeldSlot_ = 0;
}

void RhythmRippleEngine::analyzeSamples(float frameScale) noexcept {
    const float rate = static_cast<float>(sampleRate_.load(std::memory_order_acquire));
    const float coefficient = 1.0f - std::exp(-2.0f * kPi * kLowpassCutoffHz / rate);

    double energy = 0.0;
    for (size_t index = 0; index < sampleCount_; ++index) {
        const float left = finiteSample(left_[index]);
        const float right = finiteSample(right_[index]);
        const float mono = (left + right) * 0.5f;
        lowpassFilter_ += coefficient * (mono - lowpassFilter_);
        energy += static_cast<double>(lowpassFilter_) * lowpassFilter_;
    }

    const float divisor = static_cast<float>(std::max<size_t>(1, sampleCount_));
    const float target = clampUnit(std::sqrt(static_cast<float>(energy) / divisor) * kOnsetGain);

    // The baseline is captured (as onsetBaseline_ before this frame updates
    // it) so the peak test below compares against where it already was, not
    // where this frame's own energy just dragged it -- mirrors
    // StarGuitarEngine::analyzeSamples.
    onsetBaseline_ = frameFollow(onsetBaseline_, target, 0.035f, 0.02f, frameScale);
    onsetLevel_ = target;
    overallLevel_ = target;
}

float RhythmRippleEngine::nextRandom() noexcept {
    randomState_ ^= randomState_ << 13;
    randomState_ ^= randomState_ >> 17;
    randomState_ ^= randomState_ << 5;
    return static_cast<float>(randomState_ & 0x00ffffffu) / 16777215.0f;
}

void RhythmRippleEngine::updateHeldNotes(float safeSeconds, bool rawOnset, float rawMagnitude,
                                         float rawOnsetX, float rawOnsetY) noexcept {
    if (options_.longNoteSensitivity <= 0) {
        // Zero sensitivity means the feature is off: end any in-progress
        // holds immediately and quietly -- this is the user opting out of
        // the look, not a musical event, so no release burst.
        for (auto& note : heldNotes_) note.active = false;
        sustainCandidateActive_ = false;
        return;
    }

    const float holdFraction = sustainHoldFractionFromSensitivity(options_.longNoteSensitivity);

    // Opening a candidate: every accepted raw onset is a chance the hit
    // turns out to be a hold rather than a normal raindrop -- there is no
    // "only one at a time" rule for held notes themselves, but only one
    // candidate window is judged at once (a fresh onset while one is already
    // pending just becomes its own raindrop; it doesn't reopen the window).
    if (rawOnset && !sustainCandidateActive_) {
        sustainCandidateActive_ = true;
        sustainCandidatePeakLevel_ = rawMagnitude;
        sustainCandidateElapsedSeconds_ = 0.0f;
    }

    if (sustainCandidateActive_) {
        const float candidateFloor = sustainCandidatePeakLevel_ * holdFraction;
        if (onsetLevel_ < candidateFloor) {
            // Decayed like an ordinary hit -- it already got its raindrop,
            // nothing more to do.
            sustainCandidateActive_ = false;
        } else {
            sustainCandidateElapsedSeconds_ += safeSeconds;
            if (sustainCandidateElapsedSeconds_ >= kSustainConfirmSeconds) {
                sustainCandidateActive_ = false;
                // Confirmed: claim any free slot (if every slot is already
                // held, this confirmation is simply dropped).
                for (size_t attempt = 0; attempt < kMaxHeldNotes; ++attempt) {
                    const size_t index = (nextHeldSlot_ + attempt) % kMaxHeldNotes;
                    HeldNote& note = heldNotes_[index];
                    if (note.active) continue;
                    nextHeldSlot_ = (index + 1) % kMaxHeldNotes;
                    ++spawnSerial_;
                    note.active = true;
                    note.x = kHeldEdgeMargin + nextRandom() * (kReferenceWidth - kHeldEdgeMargin * 2.0f);
                    note.y = kHeldEdgeMargin + nextRandom() * (kReferenceHeight - kHeldEdgeMargin * 2.0f);
                    note.heading = nextRandom() * 2.0f * kPi;
                    // Fixed for the note's whole life: one gentle, single-
                    // direction curve rather than a path that keeps changing
                    // which way it bends.
                    note.angularVelocity = (nextRandom() - 0.5f) * 2.0f * kHeldMaxAngularVelocity;
                    note.age = 0.0f;
                    note.belowThresholdSeconds = 0.0f;
                    note.releaseFloor = candidateFloor;
                    note.seed = hashCombine(spawnSerial_, spawnSerial_ * 2654435761u);
                    note.trail = {};
                    note.trailCount = 0;
                    note.trailNext = 0;
                    note.trailSampleSeconds = 0.0f;
                    // The start of a hold is marked by exactly one ripple,
                    // the same "something just happened here" language a
                    // raindrop's impact uses.
                    spawnRippleAt(note.x, note.y, 1.0f);
                    break;
                }
            }
        }
    }

    for (auto& note : heldNotes_) {
        if (!note.active) continue;
        note.age += safeSeconds;

        note.belowThresholdSeconds =
            onsetLevel_ < note.releaseFloor ? note.belowThresholdSeconds + safeSeconds : 0.0f;
        if (note.belowThresholdSeconds >= kSustainReleaseSeconds || note.age >= kHeldMaxDurationSeconds) {
            // The end of a hold is marked by exactly one ripple too -- never
            // more than the start and end, regardless of how it wandered
            // along the way. If a raindrop happens to land this same frame,
            // share its spot so the two ripples visually coincide instead of
            // landing apart.
            if (rawOnset) {
                spawnRippleAt(rawOnsetX, rawOnsetY, 1.0f);
            } else {
                spawnRippleAt(note.x, note.y, 1.0f);
            }
            note.active = false;
            continue;
        }

        // Constant-speed motion along a single fixed-curvature arc: heading
        // advances linearly (no per-frame retargeting), so the whole path
        // traces one smooth curve instead of a wandering line.
        note.heading += note.angularVelocity * safeSeconds;
        note.x += std::cos(note.heading) * kHeldSpeed * safeSeconds;
        note.y += std::sin(note.heading) * kHeldSpeed * safeSeconds;
        // Reflecting the heading off the reference frame's margins (rather
        // than clamping position) keeps the motion itself smooth even at an
        // edge.
        if (note.x < kHeldEdgeMargin || note.x > kReferenceWidth - kHeldEdgeMargin) {
            note.x = std::clamp(note.x, kHeldEdgeMargin, kReferenceWidth - kHeldEdgeMargin);
            note.heading = kPi - note.heading;
        }
        if (note.y < kHeldEdgeMargin || note.y > kReferenceHeight - kHeldEdgeMargin) {
            note.y = std::clamp(note.y, kHeldEdgeMargin, kReferenceHeight - kHeldEdgeMargin);
            note.heading = -note.heading;
        }

        note.trailSampleSeconds += safeSeconds;
        if (note.trailSampleSeconds >= kHeldTrailSampleSeconds) {
            note.trailSampleSeconds = 0.0f;
            note.trail[note.trailNext] = {note.x, note.y};
            note.trailNext = (note.trailNext + 1) % HeldNote::kTrailPointCount;
            note.trailCount = std::min(note.trailCount + 1, HeldNote::kTrailPointCount);
        }
    }
}

void RhythmRippleEngine::spawnRippleAt(float x, float y, float magnitude) noexcept {
    for (size_t attempt = 0; attempt < kMaxRipples; ++attempt) {
        const size_t index = (nextRippleSlot_ + attempt) % kMaxRipples;
        Ripple& ripple = ripples_[index];
        if (ripple.active) continue;
        nextRippleSlot_ = (index + 1) % kMaxRipples;
        ++spawnSerial_;
        ripple.active = true;
        ripple.x = x;
        ripple.y = y;
        ripple.age = 0.0f;
        ripple.magnitude = clampUnit(0.55f + magnitude * 0.6f);
        ripple.seed = hashCombine(spawnSerial_, spawnSerial_ * 2246822519u);
        return;
    }
    // Every slot is still occupied by a still-active ripple -- drop this
    // spawn rather than truncating one mid-lifecycle. This does not violate
    // "exactly one ripple per accepted onset": the onset was still accepted
    // and gated exactly once, it simply found no free slot to render into.
}

void RhythmRippleEngine::drawBackground(DrawList& output, float width, float height) const {
    // Deliberately minimal: a plain dark wash reading as still, deep water,
    // with no background scenery (this first version skips it by design).
    output.addVerticalGradient(0.0f, 0.0f, width, height, color(0x040912), color(0x01030a));
}

void RhythmRippleEngine::drawRipple(DrawList& output, float screenX, float screenY,
                                    float minDimension, const Ripple& ripple) const {
    const float maxRadius = minDimension * 0.30f;

    if (ripple.age < kFlashLifetimeSeconds) {
        const float flashT = ripple.age / kFlashLifetimeSeconds;
        const float flashRadius = minDimension * (0.02f + 0.05f * flashT);
        const uint8_t flashAlpha =
            static_cast<uint8_t>(clampUnit((1.0f - flashT) * (1.0f - flashT)) * 235.0f * ripple.magnitude);
        output.addFillEllipse(screenX - flashRadius, screenY - flashRadius, flashRadius * 2.0f,
                              flashRadius * 2.0f, color(0xdff3ff, flashAlpha));
    }

    // Splash droplets: a small crown of flecks thrown outward from the
    // impact point, the way a real raindrop briefly splashes on landing.
    // Each fleck's angle is fixed per-ripple (from its seed) but the whole
    // burst is over quickly, well before the first ring has traveled far.
    if (ripple.age < kSplashLifetimeSeconds) {
        const float splashT = ripple.age / kSplashLifetimeSeconds;
        const float eased = easeOutCubic(splashT);
        const float travel = minDimension * kSplashDistanceFraction * eased;
        const uint8_t splashAlpha = static_cast<uint8_t>(
            clampUnit(1.0f - splashT * splashT) * 220.0f * ripple.magnitude);
        if (splashAlpha > 0) {
            const float fleckRadius = std::max(1.0f, minDimension * 0.006f * (1.0f - splashT * 0.5f));
            for (int fleck = 0; fleck < kSplashDropletCount; ++fleck) {
                const uint32_t fleckSeed = hashCombine(ripple.seed, static_cast<uint32_t>(fleck) * 7919u);
                const float angle = (static_cast<float>(fleck) / static_cast<float>(kSplashDropletCount)) *
                                        2.0f * kPi +
                                    (hash01(fleckSeed) - 0.5f) * 0.7f;
                const float fx = screenX + std::cos(angle) * travel;
                const float fy = screenY + std::sin(angle) * travel;
                output.addFillEllipse(fx - fleckRadius, fy - fleckRadius, fleckRadius * 2.0f,
                                      fleckRadius * 2.0f, color(0xcdeeff, splashAlpha));
            }
        }
    }

    // Rebound: the small center droplet bouncing back up an instant after
    // impact, visible right after the flash and splash have cleared.
    if (ripple.age >= kReboundStartSeconds && ripple.age < kReboundStartSeconds + kReboundLifetimeSeconds) {
        const float reboundT = (ripple.age - kReboundStartSeconds) / kReboundLifetimeSeconds;
        const float reboundRadius = minDimension * (0.018f - 0.010f * reboundT);
        const uint8_t reboundAlpha =
            static_cast<uint8_t>(clampUnit(1.0f - reboundT) * 190.0f * ripple.magnitude);
        if (reboundRadius > 0.5f && reboundAlpha > 0) {
            output.addFillEllipse(screenX - reboundRadius, screenY - reboundRadius,
                                  reboundRadius * 2.0f, reboundRadius * 2.0f,
                                  color(0xdff3ff, reboundAlpha));
        }
    }

    for (int ring = 0; ring < kRingCount; ++ring) {
        const float ringDelay = static_cast<float>(ring) * kRingStartDelaySeconds;
        if (ripple.age < ringDelay) continue;
        const float ringLifetime = kRippleLifetimeSeconds - ringDelay;
        if (ringLifetime <= 0.0f) continue;
        const float progress = clampUnit((ripple.age - ringDelay) / ringLifetime);
        const float eased = easeOutCubic(progress);
        const float radius = maxRadius * eased;
        if (radius <= 0.5f) continue;
        const float fade = clampUnit(1.0f - progress);
        const uint8_t alpha = static_cast<uint8_t>(fade * fade * 210.0f * ripple.magnitude);
        if (alpha == 0) continue;
        const float strokeWidth = std::max(1.0f, minDimension * 0.012f * (1.0f - progress * 0.6f));
        output.addStrokeEllipse(screenX - radius, screenY - radius, radius * 2.0f, radius * 2.0f,
                                color(0x8fd6ff, alpha), strokeWidth);
    }
}

void RhythmRippleEngine::drawHeldNote(DrawList& output, float scaleX, float scaleY,
                                      float minDimension, const HeldNote& note, float glowLevel) {
    // The fading arc trail: recent positions drawn as a polyline that fades
    // from the core back to nothing, so the note's single smooth curve of
    // travel is visible without it ever re-bursting along the way -- only
    // its start and end spawn an actual ripple (see updateHeldNotes).
    if (note.trailCount >= 2) {
        scratch_.clear();
        const size_t oldestIndex = (note.trailNext + HeldNote::kTrailPointCount - note.trailCount) %
                                   HeldNote::kTrailPointCount;
        for (size_t step = 0; step < note.trailCount; ++step) {
            const size_t index = (oldestIndex + step) % HeldNote::kTrailPointCount;
            const Point& point = note.trail[index];
            scratch_.push_back({point.x * scaleX, point.y * scaleY});
        }
        const PointRange range = output.appendPoints(scratch_);
        // A single polyline command can only carry one color/alpha, so the
        // "fade toward the tail" read comes from thinning the line and
        // dimming it overall rather than a true per-segment gradient --
        // simple, and reads as a trailing curve well enough.
        const uint8_t alpha = static_cast<uint8_t>(130.0f * clampUnit(0.4f + 0.6f * clampUnit(glowLevel)));
        output.addPolyline(range, color(0x8fd6ff, alpha), std::max(1.0f, minDimension * 0.006f), true);
    }

    const float screenX = note.x * scaleX;
    const float screenY = note.y * scaleY;

    // A continuously visible, gently pulsing core marks where the long note
    // currently sits.
    const float pulse = 0.7f + 0.3f * std::sin(note.age * 6.0f + hash01(note.seed) * kPi);
    const float coreRadius = minDimension * (0.02f + 0.012f * clampUnit(glowLevel) * pulse);
    const uint8_t coreAlpha = static_cast<uint8_t>(190.0f + 45.0f * pulse);
    output.addFillEllipse(screenX - coreRadius, screenY - coreRadius, coreRadius * 2.0f,
                          coreRadius * 2.0f, color(0xfff6d8, coreAlpha));

    // A soft halo around the core, faint and steady, to distinguish the held
    // note's look from a raindrop's flash-then-ring language at a glance.
    const float haloRadius = coreRadius * 3.2f;
    output.addStrokeEllipse(screenX - haloRadius, screenY - haloRadius, haloRadius * 2.0f,
                            haloRadius * 2.0f, color(0xffe9a8, 90), 2.0f);
}

void RhythmRippleEngine::buildFrame(float width, float height, DrawList& output) {
    output.reset();
    if (!std::isfinite(width) || !std::isfinite(height) || width <= 0.0f || height <= 0.0f) {
        return;
    }

    drawBackground(output, width, height);

    const float scaleX = width / kReferenceWidth;
    const float scaleY = height / kReferenceHeight;
    const float minDimension = std::min(width, height);

    for (const auto& ripple : ripples_) {
        if (!ripple.active) continue;
        const float screenX = ripple.x * scaleX;
        const float screenY = ripple.y * scaleY;
        drawRipple(output, screenX, screenY, minDimension, ripple);
    }

    for (const auto& note : heldNotes_) {
        if (!note.active) continue;
        drawHeldNote(output, scaleX, scaleY, minDimension, note, overallLevel_);
    }
}

} // namespace vizrack::builtin
