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

// Maps the chunk's RMS to the 0..1 level the silence gate looks at.
constexpr float kOnsetGain = 5.5f;

// Silence gate, same shape as StarGuitarEngine's: below this the source has
// effectively stopped, so no ripple spawns.
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

// A click's ripple: a firm, mid-sized hit.
constexpr float kClickMagnitude = 0.8f;

// Per band: a rectified envelope follower (attack/release in seconds), an
// absolute envelope floor so near-silence never fires, a refractory time so
// one hit fires once, a gain mapping the envelope to a 0..1 magnitude, and
// the ripple's size relative to a mid-band ripple. The low band is slowest
// because a 50Hz wave is 20ms long and a faster follower would just trace its
// cycles.
struct BandTuning {
    float attackSeconds;
    float releaseSeconds;
    float envelopeFloor;
    float refractorySeconds;
    float magnitudeGain;
    float radiusScale;
};
constexpr std::array<BandTuning, 3> kBandTuning{{
    {0.0030f, 0.050f, 0.010f, 0.100f, 3.0f, 1.25f}, // low: kick
    {0.0015f, 0.030f, 0.008f, 0.070f, 4.0f, 1.00f}, // mid: snare / body
    {0.0007f, 0.020f, 0.004f, 0.050f, 8.0f, 0.70f}, // high: hats
}};
constexpr float kLowSplitHz = 160.0f;
constexpr float kMidSplitHz = 2200.0f;
// A band fires when its log envelope is this much above where it was
// kFluxLagSeconds ago (natural-log units: 0.69 is a doubling).
constexpr float kFluxLagSeconds = 0.010f;
constexpr float kEnvelopeLogBias = 1.0e-4f;
// A fired ripple's magnitude may still grow for this long, so a hit that
// fires on its first rising block still ends up sized by its real peak.
constexpr float kMagnitudeRefineSeconds = 0.040f;
// Back-dating is capped so a long stall never spawns an already-dead ripple.
constexpr float kMaxBackdateSeconds = 0.10f;

float finiteSample(float value) noexcept {
    return std::isfinite(value) ? value : 0.0f;
}

float clampUnit(float value) noexcept {
    return std::clamp(value, 0.0f, 1.0f);
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

// The log-envelope jump (natural-log units) a band needs to fire. 100 reacts
// to ~+2dB, 0 needs ~+10dB.
float thresholdFromSensitivity(int sensitivity) noexcept {
    const float t = std::clamp(static_cast<float>(sensitivity), 0.0f, 100.0f) / 100.0f;
    return 1.20f + (0.25f - 1.20f) * t;
}

} // namespace

void RhythmRippleEngine::setOptions(RhythmRippleOptions options) noexcept {
    options.sensitivity = std::clamp(options.sensitivity, 0, 100);
    options.syncDelayMs = std::clamp(options.syncDelayMs, 0, kMaxSyncDelayMs);
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

    if (sampleCount > 0) {
        sampleCount_ = std::min(sampleCount, kMaxSamples);
        analyzeSamples(safeSeconds);
    } else {
        overallLevel_ *= std::pow(0.96f, safeSeconds * 60.0f);
    }

    if (overallLevel_ < kSilenceLevel) {
        silenceSeconds_ += safeSeconds;
    } else {
        silenceSeconds_ = 0.0f;
    }

    for (auto& ripple : ripples_) {
        if (!ripple.active) continue;
        ripple.age += safeSeconds;
        if (ripple.age >= kRippleLifetimeSeconds) ripple.active = false;
    }
}

void RhythmRippleEngine::reset() noexcept {
    sampleCount_ = 0;
    overallLevel_ = 0.0f;
    silenceSeconds_ = 0.0f;
    ripples_ = {};
    nextRippleSlot_ = 0;
    spawnSerial_ = 0;
    spawnedCount_ = 0;
    lastSpawnStartAge_ = 0.0f;
    randomState_ = 0xc2b2ae35u;
    lowStage1_ = 0.0f;
    lowStage2_ = 0.0f;
    midLowpass_ = 0.0f;
    blockFill_ = 0;
    bands_ = {};
}

void RhythmRippleEngine::dropRippleAt(float x, float y, float width, float height) noexcept {
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(width) || !std::isfinite(height) ||
        width <= 0.0f || height <= 0.0f) {
        return;
    }
    spawnRippleAt(std::clamp(x / width, 0.0f, 1.0f) * kReferenceWidth,
                  std::clamp(y / height, 0.0f, 1.0f) * kReferenceHeight, kClickMagnitude);
}

void RhythmRippleEngine::analyzeSamples(float safeSeconds) noexcept {
    const float rate = static_cast<float>(sampleRate_.load(std::memory_order_acquire));
    const float lowCoefficient = 1.0f - std::exp(-2.0f * kPi * kLowSplitHz / rate);
    const float midCoefficient = 1.0f - std::exp(-2.0f * kPi * kMidSplitHz / rate);
    const float blockSeconds = static_cast<float>(kBlockSamples) / rate;
    const size_t lagBlocks = std::clamp<size_t>(
        static_cast<size_t>(kFluxLagSeconds / blockSeconds + 0.5f), 2, kHistoryBlocks - 1);
    const float threshold = thresholdFromSensitivity(options_.sensitivity);
    const float syncDelaySeconds = static_cast<float>(options_.syncDelayMs) * 0.001f;

    std::array<float, kBandCount> attack{};
    std::array<float, kBandCount> release{};
    for (size_t band = 0; band < kBandCount; ++band) {
        attack[band] = 1.0f - std::exp(-1.0f / (kBandTuning[band].attackSeconds * rate));
        release[band] = 1.0f - std::exp(-1.0f / (kBandTuning[band].releaseSeconds * rate));
    }

    // The silence gate is decided up front from the whole chunk so the first
    // hit after a silent stretch is not swallowed by the previous frame's
    // silence counter.
    double energy = 0.0;
    for (size_t index = 0; index < sampleCount_; ++index) {
        const float mono = (finiteSample(left_[index]) + finiteSample(right_[index])) * 0.5f;
        energy += static_cast<double>(mono) * mono;
    }
    overallLevel_ = clampUnit(
        std::sqrt(static_cast<float>(energy / static_cast<double>(std::max<size_t>(1, sampleCount_)))) *
        kOnsetGain);
    const bool gateOpen =
        overallLevel_ >= kSilenceLevel || silenceSeconds_ + safeSeconds < kSilenceHoldSeconds;

    for (size_t index = 0; index < sampleCount_; ++index) {
        const float mono = (finiteSample(left_[index]) + finiteSample(right_[index])) * 0.5f;
        lowStage1_ += lowCoefficient * (mono - lowStage1_);
        lowStage2_ += lowCoefficient * (lowStage1_ - lowStage2_);
        midLowpass_ += midCoefficient * (mono - midLowpass_);
        const std::array<float, kBandCount> signal{lowStage2_, midLowpass_ - lowStage1_,
                                                   mono - midLowpass_};
        for (size_t band = 0; band < kBandCount; ++band) {
            const float rectified = std::fabs(signal[band]);
            float& envelope = bands_[band].envelope;
            envelope += (rectified > envelope ? attack[band] : release[band]) * (rectified - envelope);
        }

        if (++blockFill_ < kBlockSamples) continue;
        blockFill_ = 0;

        // How long ago (seconds) this block ended, relative to the end of the
        // chunk -- i.e. to "now". It becomes the ripple's starting age.
        const float secondsAgo = static_cast<float>(sampleCount_ - 1 - index) / rate;

        for (size_t bandIndex = 0; bandIndex < kBandCount; ++bandIndex) {
            Band& band = bands_[bandIndex];
            const BandTuning& tuning = kBandTuning[bandIndex];
            const float level = std::log(band.envelope + kEnvelopeLogBias);
            band.refractorySeconds = std::max(0.0f, band.refractorySeconds - blockSeconds);
            band.refineSeconds = std::max(0.0f, band.refineSeconds - blockSeconds);

            if (band.refineSeconds > 0.0f) {
                Ripple& refined = ripples_[band.refineSlot];
                if (refined.active && refined.seed == band.refineSeed) {
                    const float grown =
                        clampUnit(0.55f + clampUnit(band.envelope * tuning.magnitudeGain) * 0.6f);
                    refined.magnitude = std::max(refined.magnitude, grown);
                }
            }

            float flux = 0.0f;
            if (band.historyCount >= lagBlocks) {
                const size_t reference =
                    (band.historyNext + kHistoryBlocks - lagBlocks) % kHistoryBlocks;
                flux = level - band.history[reference];
            }
            band.history[band.historyNext] = level;
            band.historyNext = (band.historyNext + 1) % kHistoryBlocks;
            band.historyCount = std::min(band.historyCount + 1, kHistoryBlocks);

            if (flux < threshold * 0.5f) band.armed = true;
            if (!gateOpen || !band.armed || band.refractorySeconds > 0.0f ||
                flux <= threshold || band.envelope <= tuning.envelopeFloor) {
                continue;
            }

            band.armed = false;
            band.refractorySeconds = tuning.refractorySeconds;
            const float x = kEdgeMargin + nextRandom() * (kReferenceWidth - kEdgeMargin * 2.0f);
            const float y = kEdgeMargin + nextRandom() * (kReferenceHeight - kEdgeMargin * 2.0f);
            const float magnitude = clampUnit(band.envelope * tuning.magnitudeGain);
            const float startAge = std::min(secondsAgo, kMaxBackdateSeconds) - syncDelaySeconds;
            const size_t slot = spawnRippleAt(x, y, magnitude, startAge, tuning.radiusScale);
            if (slot < kMaxRipples) {
                band.refineSlot = slot;
                band.refineSeed = ripples_[slot].seed;
                band.refineSeconds = kMagnitudeRefineSeconds;
            }
        }
    }
}

float RhythmRippleEngine::nextRandom() noexcept {
    randomState_ ^= randomState_ << 13;
    randomState_ ^= randomState_ >> 17;
    randomState_ ^= randomState_ << 5;
    return static_cast<float>(randomState_ & 0x00ffffffu) / 16777215.0f;
}

size_t RhythmRippleEngine::spawnRippleAt(float x, float y, float magnitude, float startAge,
                                         float radiusScale) noexcept {
    for (size_t attempt = 0; attempt < kMaxRipples; ++attempt) {
        const size_t index = (nextRippleSlot_ + attempt) % kMaxRipples;
        Ripple& ripple = ripples_[index];
        if (ripple.active) continue;
        nextRippleSlot_ = (index + 1) % kMaxRipples;
        ++spawnSerial_;
        ripple.active = true;
        ripple.x = x;
        ripple.y = y;
        ripple.age = startAge;
        ripple.magnitude = clampUnit(0.55f + magnitude * 0.6f);
        ripple.radiusScale = radiusScale;
        ripple.seed = hashCombine(spawnSerial_, spawnSerial_ * 2246822519u);
        ++spawnedCount_;
        lastSpawnStartAge_ = startAge;
        return index;
    }
    // Every slot is still occupied by a still-active ripple -- drop this
    // spawn rather than truncating one mid-lifecycle. This does not violate
    // "exactly one ripple per accepted hit": the hit was still accepted and
    // gated exactly once, it simply found no free slot to render into.
    return kMaxRipples;
}

void RhythmRippleEngine::drawBackground(DrawList& output, float width, float height) const {
    // Deliberately minimal: a plain dark wash reading as still, deep water,
    // with no background scenery (this first version skips it by design).
    output.addVerticalGradient(0.0f, 0.0f, width, height, color(0x040912), color(0x01030a));
}

void RhythmRippleEngine::drawRipple(DrawList& output, float screenX, float screenY,
                                    float minDimension, const Ripple& ripple) const {
    const float maxRadius = minDimension * 0.30f * ripple.radiusScale;

    if (ripple.age < kFlashLifetimeSeconds) {
        const float flashT = ripple.age / kFlashLifetimeSeconds;
        const float flashRadius = minDimension * (0.02f + 0.05f * flashT) * ripple.radiusScale;
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
        if (!ripple.active || ripple.age < 0.0f) continue;
        const float screenX = ripple.x * scaleX;
        const float screenY = ripple.y * scaleY;
        drawRipple(output, screenX, screenY, minDimension, ripple);
    }
}

} // namespace vizrack::builtin
