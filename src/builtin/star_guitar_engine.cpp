#include "builtin/star_guitar_engine.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace vizrack::builtin {
namespace {

constexpr float kPi = std::numbers::pi_v<float>;

// World/reference-pixel grid: object spacing and scroll speed are expressed in
// units that assume a 1920px-wide reference frame, then scaled to the actual
// render size. This keeps the scene feeling the same regardless of window
// size.
constexpr float kReferenceWidth = 1920.0f;
constexpr float kUnitReference = 10.0f; // reference px per "unit" used by the draw helpers

// Every spawn -- peak-triggered or ambient filler alike -- grows in over the
// same real-time window, so there is exactly one animation rule in the whole
// engine rather than a per-source special case.
constexpr float kGrowInSeconds = 0.22f;

// A filler (non-peak) spawn always gets this fixed size: smaller than a
// typical real hit, so it reads as background rather than competing with one.
constexpr float kAmbientSizeScale = 0.6f;

// Eased 0..1 growth curve: a quick rise that slightly overshoots past 1 then
// settles back, so an object's arrival reads as a snappy "struck" motion
// (like a needle jumping up) rather than a slow, mushy fade-in.
float growEase(float t) noexcept {
    t = std::clamp(t, 0.0f, 1.0f);
    const float overshoot = 1.70158f;
    const float shifted = t - 1.0f;
    return 1.0f + shifted * shifted * ((overshoot + 1.0f) * shifted + overshoot);
}

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

// Maps a band's raw 0..1 magnitude at the moment it peaked to the visual size
// scale an object spawned from it should grow to. Every peak-driven spawn in
// the engine goes through this one mapping, so "how hard was the hit" reads
// consistently as "how big is the object" everywhere.
float sizeFromMagnitude(float magnitude) noexcept {
    return 0.65f + clampUnit(magnitude) * 1.15f;
}

} // namespace

StarGuitarEngine::StarGuitarEngine() {
    scratch_.reserve(16);

    // Far: small, sparse -- water towers and buildings loom and linger.
    layers_[kFarLayer].speed = 110.0f;
    layers_[kFarLayer].depthScale = 0.55f;
    layers_[kFarLayer].baseIntervalSeconds = 2.6f;
    layers_[kFarLayer].jitterSeconds = 1.2f;

    // Mid: telephone-pole / tree cadence, jittered rather than metronomic,
    // plus a presence-band marker so a second rhythmic voice (e.g. a snare
    // alongside a kick) is visually distinguishable from the kick-driven far
    // layer instead of blending into it.
    layers_[kMidLayer].speed = 190.0f;
    layers_[kMidLayer].depthScale = 0.85f;
    layers_[kMidLayer].baseIntervalSeconds = 1.6f;
    layers_[kMidLayer].jitterSeconds = 0.9f;

    // Near: fast, large-relative, brief -- cymbal-like transients flash by.
    layers_[kNearLayer].speed = 430.0f;
    layers_[kNearLayer].depthScale = 1.35f;
    layers_[kNearLayer].baseIntervalSeconds = 1.8f;
    layers_[kNearLayer].jitterSeconds = 1.2f;

    // Sky: same air band as the near layer's cymbal marker, but weighted
    // toward stars so it reads as a light, frequent twinkle rather than a
    // rare flourish -- see spawnSky().
    layers_[kSkyLayer].speed = 55.0f;
    layers_[kSkyLayer].depthScale = 0.5f;

    for (auto& layer : layers_) {
        layer.idleTimer = layer.baseIntervalSeconds;
    }
}

void StarGuitarEngine::setOptions(StarGuitarOptions options) noexcept {
    if (options.algorithmMode != StarGuitarAlgorithmMode::reactive &&
        options.algorithmMode != StarGuitarAlgorithmMode::predictive) {
        options.algorithmMode = StarGuitarAlgorithmMode::reactive;
    }
    options_ = options;
}

void StarGuitarEngine::setSampleRate(uint32_t sampleRate) noexcept {
    if (sampleRate >= 8000 && sampleRate <= 768000) {
        sampleRate_.store(sampleRate, std::memory_order_release);
    }
}

void StarGuitarEngine::update(size_t sampleCount, float frameSeconds) noexcept {
    const float safeSeconds = std::isfinite(frameSeconds)
                                  ? std::clamp(frameSeconds, 1.0f / 240.0f, 1.0f / 10.0f)
                                  : 1.0f / 60.0f;
    const float frameScale = safeSeconds * 60.0f;

    lowPeakCooldown_ = std::max(0.0f, lowPeakCooldown_ - safeSeconds);
    presencePeakCooldown_ = std::max(0.0f, presencePeakCooldown_ - safeSeconds);
    airPeakCooldown_ = std::max(0.0f, airPeakCooldown_ - safeSeconds);

    if (sampleCount > 0) {
        sampleCount_ = std::min(sampleCount, kMaxSamples);
        analyzeSamples(frameScale);
    } else {
        lowLevel_ *= std::pow(0.965f, frameScale);
        midLevel_ *= std::pow(0.955f, frameScale);
        presenceLevel_ *= std::pow(0.95f, frameScale);
        airLevel_ *= std::pow(0.945f, frameScale);
        lowBaseline_ *= std::pow(0.985f, frameScale);
        presenceBaseline_ *= std::pow(0.98f, frameScale);
        airBaseline_ *= std::pow(0.985f, frameScale);
        lowPeak_ = {};
        presencePeak_ = {};
        airPeak_ = {};
    }

    songClock_ += safeSeconds;
    // Keep the clock bounded across a long-running session; only differences
    // between peak timestamps matter, and a wrap can only ever cost one
    // discarded interval sample.
    if (songClock_ > 1.0e6f) {
        songClock_ = 0.0f;
        lastLowPeakTime_ = -1.0f;
    }
    updateTempoTracker(lowPeak_.fired, safeSeconds);

    const float overallLevel = (lowLevel_ + midLevel_ + presenceLevel_ + airLevel_) / 4.0f;
    songEnergy_ = frameFollow(songEnergy_, clampUnit(overallLevel), 0.02f, 0.015f, frameScale);

    // Silence gate: below this the track has effectively stopped (or a long
    // gap is playing), so every spawn source -- including the tempo-locked
    // fill-in, which otherwise has no idea whether the song is still
    // playing -- goes quiet instead of continuing to produce scenery.
    constexpr float kSilenceLevel = 0.025f;
    constexpr float kSilenceHoldSeconds = 0.4f;
    // A longer silence also invalidates the tempo lock and peak history so a
    // new song, or a new section after a real pause, re-acquires cleanly
    // instead of inheriting a stale tempo.
    constexpr float kTempoResetSeconds = 1.5f;
    if (overallLevel < kSilenceLevel) {
        silenceSeconds_ += safeSeconds;
    } else {
        silenceSeconds_ = 0.0f;
    }
    const bool audible = silenceSeconds_ < kSilenceHoldSeconds;
    if (silenceSeconds_ > kTempoResetSeconds) {
        tempoLocked_ = false;
        peakIntervalCount_ = 0;
        peakIntervalCursor_ = 0;
        lastLowPeakTime_ = -1.0f;
    }

    for (auto& layer : layers_) {
        for (auto& instance : layer.objects) {
            if (!instance.active) continue;
            instance.traveled += layer.speed * safeSeconds;
            instance.age += safeSeconds;
            // Objects are reclaimed lazily in buildFrame once they scroll
            // fully off screen (screen width varies per call), so no
            // fixed-lifetime cutoff is needed here.
        }
    }

    if (!audible) {
        // Pin every idle timer at its base interval instead of letting it
        // run down during silence -- otherwise the instant the song resumes,
        // every ambient fallback that "should" have fired during the gap
        // fires all at once.
        for (auto& layer : layers_) {
            layer.idleTimer = layer.baseIntervalSeconds;
        }
        return;
    }

    // Low-band peak (kick-like, the "쿵") -> far layer: a water tower for a
    // strong hit, a building otherwise, sized by how hard it hit.
    if (lowPeak_.fired) {
        const auto type = lowPeak_.magnitude > 0.62f
                              ? StarGuitarObjectType::waterTower
                              : (nextRandom() < 0.5f ? StarGuitarObjectType::buildingA
                                                      : StarGuitarObjectType::buildingC);
        spawnInstance(layers_[kFarLayer], type, sizeFromMagnitude(lowPeak_.magnitude));
    } else {
        spawnAmbient(layers_[kFarLayer], StarGuitarObjectType::buildingB, safeSeconds);
    }

    // The mid layer is the visible "pulse": the same low-band peak that
    // triggers the far layer also spawns a pole/tree here, landing on the
    // same beat.
    updateMidPulse(layers_[kMidLayer], lowPeak_, options_.algorithmMode, StarGuitarObjectType::pole,
                  StarGuitarObjectType::tree, safeSeconds);
    // Presence-band peak (snare/clap-like, the "짝") -> a bright trackside
    // signal marker layered onto the same mid layer, so the two alternating
    // rhythmic voices are both visible without one masking the other.
    if (presencePeak_.fired) {
        spawnInstance(layers_[kMidLayer], StarGuitarObjectType::signalMarker,
                     sizeFromMagnitude(presencePeak_.magnitude));
    }

    // Air-band peak (hi-hat/cymbal/shimmer-like) -> near layer: a quick
    // bright flash close to the viewer.
    if (airPeak_.fired) {
        spawnInstance(layers_[kNearLayer], StarGuitarObjectType::signalMarker,
                     sizeFromMagnitude(airPeak_.magnitude));
    } else {
        spawnAmbient(layers_[kNearLayer], StarGuitarObjectType::buildingC, safeSeconds);
    }

    // Sky: the same air peak, weighted toward stars for a light, frequent
    // twinkle -- see spawnSky().
    spawnSky(layers_[kSkyLayer], airPeak_);
}

void StarGuitarEngine::reset() noexcept {
    sampleCount_ = 0;
    lowFilter_ = 0.0f;
    midFilter_ = 0.0f;
    presenceFilter_ = 0.0f;
    lowLevel_ = 0.0f;
    midLevel_ = 0.0f;
    presenceLevel_ = 0.0f;
    airLevel_ = 0.0f;
    lowBaseline_ = 0.0f;
    presenceBaseline_ = 0.0f;
    airBaseline_ = 0.0f;
    lowPeakCooldown_ = 0.0f;
    presencePeakCooldown_ = 0.0f;
    airPeakCooldown_ = 0.0f;
    lowPeak_ = {};
    presencePeak_ = {};
    airPeak_ = {};
    songEnergy_ = 0.0f;
    silenceSeconds_ = 0.0f;
    songClock_ = 0.0f;
    lastLowPeakTime_ = -1.0f;
    beatPeriodSeconds_ = 0.5f;
    beatPhase_ = 0.0f;
    tempoLocked_ = false;
    peakIntervals_ = {};
    peakIntervalCount_ = 0;
    peakIntervalCursor_ = 0;
    spawnSerial_ = 0;
    randomState_ = 0x9f2c86adu;
    for (auto& layer : layers_) {
        layer.idleTimer = layer.baseIntervalSeconds;
        layer.nextSlot = 0;
        layer.objects = {};
    }
}

void StarGuitarEngine::analyzeSamples(float frameScale) noexcept {
    // Three cascaded one-pole low-pass filters at increasing cutoffs split
    // the signal into four bands (low/mid/presence/air) by taking successive
    // residuals. See docs/STAR_GUITAR_FREQUENCY_BANDS.md for why these
    // specific cutoffs were chosen and what each band is meant to capture.
    const float rate = static_cast<float>(sampleRate_.load(std::memory_order_acquire));
    const float lowCoefficient = 1.0f - std::exp(-2.0f * kPi * 150.0f / rate);
    const float midCoefficient = 1.0f - std::exp(-2.0f * kPi * 2500.0f / rate);
    const float presenceCoefficient = 1.0f - std::exp(-2.0f * kPi * 6000.0f / rate);
    double lowEnergy = 0.0;
    double midEnergy = 0.0;
    double presenceEnergy = 0.0;
    double airEnergy = 0.0;
    for (size_t index = 0; index < sampleCount_; ++index) {
        const float left = finiteSample(left_[index]);
        const float right = finiteSample(right_[index]);
        const float mono = (left + right) * 0.5f;
        lowFilter_ += lowCoefficient * (mono - lowFilter_);
        midFilter_ += midCoefficient * (mono - midFilter_);
        presenceFilter_ += presenceCoefficient * (mono - presenceFilter_);
        const float low = lowFilter_;
        const float mid = midFilter_ - lowFilter_;
        const float presence = presenceFilter_ - midFilter_;
        const float air = mono - presenceFilter_;
        lowEnergy += static_cast<double>(low) * low;
        midEnergy += static_cast<double>(mid) * mid;
        presenceEnergy += static_cast<double>(presence) * presence;
        airEnergy += static_cast<double>(air) * air;
    }
    const float divisor = static_cast<float>(std::max<size_t>(1, sampleCount_));
    const float lowTarget = clampUnit(std::sqrt(static_cast<float>(lowEnergy) / divisor) * 4.3f);
    const float midTarget = clampUnit(std::sqrt(static_cast<float>(midEnergy) / divisor) * 5.7f);
    const float presenceTarget =
        clampUnit(std::sqrt(static_cast<float>(presenceEnergy) / divisor) * 6.4f);
    const float airTarget = clampUnit(std::sqrt(static_cast<float>(airEnergy) / divisor) * 8.6f);

    lowLevel_ = frameFollow(lowLevel_, lowTarget, 0.42f, 0.10f, frameScale);
    // Mid (150Hz-2.5kHz, vocals/guitars/keys) deliberately gets no baseline
    // or peak detector: it's too dense and continuously-present in most
    // mixes for a relative-rise test to mean anything -- it would fire
    // almost constantly. It's tracked only as a sustained "how busy is the
    // song" measure for songEnergy_ and the reactive mode's ambient cadence.
    midLevel_ = frameFollow(midLevel_, midTarget, 0.18f, 0.05f, frameScale);
    presenceLevel_ = frameFollow(presenceLevel_, presenceTarget, 0.40f, 0.10f, frameScale);
    airLevel_ = frameFollow(airLevel_, airTarget, 0.40f, 0.10f, frameScale);

    // Baselines move much more slowly than the levels above -- they
    // represent "what has this band typically been doing the last couple of
    // seconds," which a peak is then measured against. Captured *before*
    // this frame updates them, so the peak test compares against where the
    // baseline already was, not where this frame's own energy just dragged
    // it.
    const float previousLowBaseline = lowBaseline_;
    const float previousPresenceBaseline = presenceBaseline_;
    const float previousAirBaseline = airBaseline_;
    lowBaseline_ = frameFollow(lowBaseline_, lowTarget, 0.03f, 0.02f, frameScale);
    presenceBaseline_ = frameFollow(presenceBaseline_, presenceTarget, 0.05f, 0.03f, frameScale);
    airBaseline_ = frameFollow(airBaseline_, airTarget, 0.05f, 0.03f, frameScale);

    // One rule, three bands: a peak is a sharp rise *relative to the band's
    // own recent baseline*, not an absolute level -- see detectPeak().
    lowPeak_ = detectPeak(lowTarget, previousLowBaseline, lowPeakCooldown_, 0.22f, 0.55f, 0.08f);
    presencePeak_ = detectPeak(presenceTarget, previousPresenceBaseline, presencePeakCooldown_,
                               0.16f, 0.50f, 0.06f);
    airPeak_ = detectPeak(airTarget, previousAirBaseline, airPeakCooldown_, 0.12f, 0.50f, 0.05f);
}

float StarGuitarEngine::nextRandom() noexcept {
    randomState_ ^= randomState_ << 13;
    randomState_ ^= randomState_ >> 17;
    randomState_ ^= randomState_ << 5;
    return static_cast<float>(randomState_ & 0x00ffffffu) / 16777215.0f;
}

void StarGuitarEngine::spawnInstance(Layer& layer, StarGuitarObjectType type,
                                     float sizeScale) noexcept {
    // Search for a free slot starting at the round-robin cursor. If every
    // slot is still occupied by an object that hasn't scrolled off screen
    // yet, drop this spawn instead of overwriting (and visually truncating)
    // one that's still mid-flight.
    for (size_t attempt = 0; attempt < kLayerCapacity; ++attempt) {
        const size_t index = (layer.nextSlot + attempt) % kLayerCapacity;
        Instance& instance = layer.objects[index];
        if (instance.active) continue;
        layer.nextSlot = (index + 1) % kLayerCapacity;
        ++spawnSerial_;
        instance.active = true;
        instance.type = type;
        instance.traveled = 0.0f;
        instance.age = 0.0f;
        instance.sizeScale = sizeScale;
        instance.seed = hashCombine(spawnSerial_, spawnSerial_ * 2246822519u);
        return;
    }
}

StarGuitarEngine::Peak StarGuitarEngine::detectPeak(float level, float baseline, float& cooldown,
                                                    float cooldownSeconds,
                                                    float relativeThreshold,
                                                    float floorLevel) noexcept {
    if (level <= floorLevel || cooldown > 0.0f) return {};
    const float relativeRise = (level - baseline) / (baseline + 0.05f);
    if (relativeRise <= relativeThreshold) return {};
    cooldown = cooldownSeconds + nextRandom() * (cooldownSeconds * 0.4f);
    return {true, level};
}

void StarGuitarEngine::spawnAmbient(Layer& layer, StarGuitarObjectType type,
                                    float frameSeconds) noexcept {
    layer.idleTimer -= frameSeconds;
    if (layer.idleTimer > 0.0f) return;
    spawnInstance(layer, type, kAmbientSizeScale);
    layer.idleTimer = layer.baseIntervalSeconds + nextRandom() * layer.jitterSeconds;
}

void StarGuitarEngine::updateTempoTracker(bool lowPeakFired, float frameSeconds) noexcept {
    (void)frameSeconds;
    // A tempo lock that never confirms another peak for a long stretch is
    // stale -- either the song stopped, or moved to a section without a
    // clear kick -- so stop trusting it rather than letting the mid layer's
    // fill-in free-run on an old estimate.
    if (tempoLocked_ && lastLowPeakTime_ >= 0.0f &&
        (songClock_ - lastLowPeakTime_) > beatPeriodSeconds_ * 3.0f) {
        tempoLocked_ = false;
    }
    if (!lowPeakFired) return;

    if (lastLowPeakTime_ >= 0.0f) {
        const float interval = songClock_ - lastLowPeakTime_;
        // Only trust intervals inside a plausible tempo range (roughly
        // 45-215 BPM); anything outside that is almost certainly a missed or
        // doubled detection rather than a real beat-to-beat gap, and would
        // otherwise drag the median estimate off in one step.
        if (interval > 0.27f && interval < 1.35f) {
            peakIntervals_[peakIntervalCursor_] = interval;
            peakIntervalCursor_ = (peakIntervalCursor_ + 1) % kPeakHistory;
            peakIntervalCount_ = std::min(peakIntervalCount_ + 1, kPeakHistory);
        }
    }
    lastLowPeakTime_ = songClock_;

    if (peakIntervalCount_ >= 3) {
        std::array<float, kPeakHistory> sorted{};
        std::copy_n(peakIntervals_.begin(), peakIntervalCount_, sorted.begin());
        std::sort(sorted.begin(), sorted.begin() + static_cast<ptrdiff_t>(peakIntervalCount_));
        const float median = sorted[peakIntervalCount_ / 2];
        // Blend toward the new median rather than snapping to it, so one odd
        // interval (a fill, a skipped beat) nudges the estimate instead of
        // yanking it.
        beatPeriodSeconds_ = tempoLocked_ ? beatPeriodSeconds_ * 0.75f + median * 0.25f : median;
        tempoLocked_ = true;
    }
    // Re-sync phase to the real hit every time -- this is what keeps the
    // visual pulse from ever drifting away from the audible one.
    beatPhase_ = 0.0f;
}

void StarGuitarEngine::updateMidPulse(Layer& layer, Peak lowPeak, StarGuitarAlgorithmMode mode,
                                      StarGuitarObjectType primaryType,
                                      StarGuitarObjectType secondaryType,
                                      float frameSeconds) noexcept {
    const auto spawnPulse = [&](float sizeScale) {
        spawnInstance(layer, nextRandom() < 0.55f ? primaryType : secondaryType, sizeScale);
    };
    // Both modes react to a confirmed low-band peak the same way -- that's a
    // direct reaction, not a prediction. Only the gap-filling between peaks
    // differs by mode.
    if (lowPeak.fired) {
        spawnPulse(sizeFromMagnitude(lowPeak.magnitude));
        return;
    }
    if (mode == StarGuitarAlgorithmMode::predictive && tempoLocked_) {
        // Fill in on the estimated beat phase so the cadence stays steady
        // even through a soft hit the peak detector misses.
        beatPhase_ += frameSeconds / std::max(beatPeriodSeconds_, 0.05f);
        if (beatPhase_ >= 1.0f) {
            beatPhase_ -= 1.0f;
            spawnPulse(kAmbientSizeScale);
        }
        return;
    }
    // Reactive mode, or predictive mode before a tempo lock is acquired:
    // fall back to a jittered interval timer keyed to general mid-band
    // busyness, with no tempo estimate involved.
    layer.idleTimer -= frameSeconds;
    if (layer.idleTimer > 0.0f) return;
    spawnPulse(kAmbientSizeScale);
    // Higher sustained mid-band energy shortens the average interval (busier
    // cadence) while jitter keeps consecutive spawns asymmetric instead of a
    // strict metronome grid.
    const float energyFactor = 1.0f + midLevel_ * 1.8f;
    layer.idleTimer =
        (layer.baseIntervalSeconds / energyFactor) + nextRandom() * layer.jitterSeconds;
}

void StarGuitarEngine::spawnSky(Layer& layer, Peak airPeak) noexcept {
    if (!airPeak.fired) return;
    // Heavily weighted toward stars: a light, frequent twinkle rather than a
    // rare flourish. Birds and planes stay uncommon.
    const float pick = nextRandom();
    const StarGuitarObjectType type = pick < 0.70f  ? StarGuitarObjectType::star
                                      : pick < 0.90f ? StarGuitarObjectType::bird
                                                      : StarGuitarObjectType::plane;
    spawnInstance(layer, type, sizeFromMagnitude(airPeak.magnitude));
}

void StarGuitarEngine::drawGround(DrawList& output, float width, float height,
                                  float groundY) const {
    output.addVerticalGradient(0.0f, 0.0f, width, groundY, color(0x0b1424), color(0x2a3f5c));
    output.addFillRectangle(0.0f, groundY, width, height - groundY, color(0x05070c));
    // A thin lit strip along the horizon reads as a rail/road line.
    output.addFillRectangle(0.0f, groundY - 2.0f, width, 2.0f, color(0x40597a));
}

void StarGuitarEngine::drawPole(DrawList& output, float baseX, float groundY, float unit,
                                float scale) const {
    const float poleWidth = unit * 0.9f;
    const float poleHeight = unit * 11.0f * scale;
    const Color body = color(0x0a0e16);
    output.addFillRectangle(baseX - poleWidth * 0.5f, groundY - poleHeight, poleWidth,
                            poleHeight, body);
    // Crossbar near the top, composed of aligned blocks for a blocky silhouette.
    const float crossWidth = unit * 5.0f;
    const float crossY = groundY - poleHeight + unit * 1.2f;
    output.addFillRectangle(baseX - crossWidth * 0.5f, crossY, crossWidth, unit * 0.7f, body);
    // Insulator studs.
    output.addFillRectangle(baseX - crossWidth * 0.42f, crossY - unit * 0.6f, unit * 0.6f,
                            unit * 0.6f, body);
    output.addFillRectangle(baseX + crossWidth * 0.42f - unit * 0.6f, crossY - unit * 0.6f,
                            unit * 0.6f, unit * 0.6f, body);
}

void StarGuitarEngine::drawTree(DrawList& output, float baseX, float groundY, float unit,
                                uint32_t seed, float scale) const {
    const float trunkWidth = unit * 0.8f;
    const float trunkHeight = unit * 2.6f * scale;
    const Color trunk = color(0x120e0a);
    output.addFillRectangle(baseX - trunkWidth * 0.5f, groundY - trunkHeight, trunkWidth,
                            trunkHeight, trunk);

    // A stepped, blocky canopy: three shrinking tiers instead of a smooth
    // circle, keeping the silhouette axis-aligned like the rest of the scene.
    const float canopyHeight = unit * (4.5f + hash01(seed) * 2.0f) * scale;
    const float canopyWidth = unit * (4.0f + hash01(seed ^ 0x27d4eb2fu) * 2.2f) * scale;
    const Color canopy = color(0x0e1c12);
    constexpr int tiers = 3;
    for (int tier = 0; tier < tiers; ++tier) {
        const float tierFraction = static_cast<float>(tier) / static_cast<float>(tiers);
        const float tierWidth = canopyWidth * (1.0f - tierFraction * 0.55f);
        const float tierHeight = canopyHeight / static_cast<float>(tiers);
        const float tierY =
            groundY - trunkHeight - canopyHeight + tierHeight * static_cast<float>(tier);
        output.addFillRectangle(baseX - tierWidth * 0.5f, tierY, tierWidth, tierHeight + 0.5f,
                                canopy);
    }
}

void StarGuitarEngine::drawBuilding(DrawList& output, float baseX, float groundY, float unit,
                                    StarGuitarObjectType variant, uint32_t seed,
                                    float scale) const {
    const float heightUnits = 8.0f + hash01(seed) * 10.0f;
    const float widthUnits = (5.0f + hash01(seed ^ 0x51ed270bu) * 3.0f) * scale;
    const float bodyWidth = unit * widthUnits;
    const float bodyHeight = unit * heightUnits * scale;
    const Color body = color(0x0c1017);
    const Color window = color(0x1d3a52, 200);
    output.addFillRectangle(baseX - bodyWidth * 0.5f, groundY - bodyHeight, bodyWidth,
                            bodyHeight, body);

    if (variant == StarGuitarObjectType::buildingB) {
        // A setback rooftop block.
        const float topWidth = bodyWidth * 0.5f;
        output.addFillRectangle(baseX - topWidth * 0.5f, groundY - bodyHeight - unit * 2.0f,
                                topWidth, unit * 2.0f, body);
    } else if (variant == StarGuitarObjectType::buildingC) {
        // A stepped, art-deco style pixel pyramid roof: successive rectangles
        // shrinking in width, always axis aligned to stay blocky.
        constexpr int steps = 3;
        for (int step = 0; step < steps; ++step) {
            const float shrink = static_cast<float>(step + 1) / (steps + 1);
            const float stepWidth = bodyWidth * (1.0f - shrink);
            output.addFillRectangle(baseX - stepWidth * 0.5f,
                                    groundY - bodyHeight - unit * static_cast<float>(step + 1),
                                    stepWidth, unit, body);
        }
    }

    // A handful of lit windows in a fixed small grid: enough for the pixel-art
    // read without a full per-floor catalog.
    const int columns = std::max(1, static_cast<int>(widthUnits / 1.6f));
    const int rows = std::max(1, static_cast<int>(heightUnits / 2.0f));
    const float cellWidth = bodyWidth / static_cast<float>(columns);
    const float cellHeight = bodyHeight / static_cast<float>(rows);
    for (int row = 0; row < rows; ++row) {
        for (int column = 0; column < columns; ++column) {
            const uint32_t litHash =
                hashCombine(seed, static_cast<uint32_t>(row * 17 + column));
            if (hash01(litHash) > 0.42f) continue;
            const float windowX =
                baseX - bodyWidth * 0.5f + cellWidth * (static_cast<float>(column) + 0.28f);
            const float windowY =
                groundY - bodyHeight + cellHeight * (static_cast<float>(row) + 0.22f);
            output.addFillRectangle(windowX, windowY, cellWidth * 0.44f, cellHeight * 0.44f,
                                    window);
        }
    }
}

void StarGuitarEngine::drawWaterTower(DrawList& output, float baseX, float groundY, float unit,
                                      uint32_t seed, float scale) {
    const Color body = color(0x0d1218);
    const float legHeight = unit * 6.0f * scale;
    const float tankHalfWidth = unit * 3.4f * scale;
    const float tankHeight = unit * 3.4f * scale;
    const float tankY = groundY - legHeight - tankHeight;

    // Four blocky support legs.
    constexpr std::array<float, 4> legOffsets{-2.6f, -1.1f, 1.1f, 2.6f};
    for (const float offset : legOffsets) {
        output.addFillRectangle(baseX + offset * unit * scale - unit * 0.22f, groundY - legHeight,
                                unit * 0.44f, legHeight, body);
    }

    // Tank body as an axis-aligned blocky octagon (a stepped silhouette
    // instead of a smooth ellipse), built from a small fixed point set.
    scratch_.clear();
    const float inset = tankHalfWidth * 0.32f;
    scratch_.push_back({baseX - tankHalfWidth + inset, tankY});
    scratch_.push_back({baseX + tankHalfWidth - inset, tankY});
    scratch_.push_back({baseX + tankHalfWidth, tankY + inset});
    scratch_.push_back({baseX + tankHalfWidth, tankY + tankHeight - inset});
    scratch_.push_back({baseX + tankHalfWidth - inset, tankY + tankHeight});
    scratch_.push_back({baseX - tankHalfWidth + inset, tankY + tankHeight});
    scratch_.push_back({baseX - tankHalfWidth, tankY + tankHeight - inset});
    scratch_.push_back({baseX - tankHalfWidth, tankY + inset});
    output.addFillPolygon(output.appendPoints(scratch_), body);

    // A small conical cap made of two shrinking blocks (kept blocky/axis
    // aligned per the pixel-art constraint).
    output.addFillRectangle(baseX - tankHalfWidth * 0.6f, tankY - unit * 0.7f * scale,
                            tankHalfWidth * 1.2f, unit * 0.7f * scale, body);
    output.addFillRectangle(baseX - tankHalfWidth * 0.28f, tankY - unit * 1.2f * scale,
                            tankHalfWidth * 0.56f, unit * 0.5f * scale, body);
    (void)seed;
}

void StarGuitarEngine::drawSignalMarker(DrawList& output, float baseX, float groundY, float unit,
                                        float scale) const {
    // A bright, high-contrast trackside signal: deliberately unlike the dark
    // silhouettes around it so a rhythmic peak reads as a visible flash
    // rather than blending into the scenery.
    const float postWidth = unit * 0.5f;
    const float postHeight = unit * 3.2f * scale;
    const Color post = color(0x0a0e16);
    output.addFillRectangle(baseX - postWidth * 0.5f, groundY - postHeight, postWidth,
                            postHeight, post);
    const float lampSize = unit * 1.4f * scale;
    const Color lamp = color(0xe0a63a, 235);
    output.addFillRectangle(baseX - lampSize * 0.5f, groundY - postHeight - lampSize * 0.85f,
                            lampSize, lampSize, lamp);
}

void StarGuitarEngine::drawBird(DrawList& output, float baseX, float baseY, float unit,
                                float scale) const {
    // A small chevron silhouette built from two blocky wing rectangles.
    const Color body = color(0x0c1017, 220);
    const float wingWidth = unit * 1.6f * scale;
    const float wingHeight = unit * 0.5f * scale;
    output.addFillRectangle(baseX - wingWidth, baseY - wingHeight * 0.5f, wingWidth,
                            wingHeight, body);
    output.addFillRectangle(baseX, baseY - wingHeight * 0.5f, wingWidth, wingHeight, body);
}

void StarGuitarEngine::drawPlane(DrawList& output, float baseX, float baseY, float unit,
                                 float scale) const {
    const Color body = color(0x0c1017, 230);
    const float fuselageLength = unit * 5.0f * scale;
    const float fuselageHeight = unit * 0.6f * scale;
    output.addFillRectangle(baseX - fuselageLength * 0.5f, baseY - fuselageHeight * 0.5f,
                            fuselageLength, fuselageHeight, body);
    const float wingWidth = unit * 1.0f * scale;
    const float wingHeight = unit * 2.2f * scale;
    output.addFillRectangle(baseX - wingWidth * 0.5f, baseY - wingHeight * 0.5f, wingWidth,
                            wingHeight, body);
}

void StarGuitarEngine::drawStar(DrawList& output, float baseX, float baseY, float unit,
                                float scale) const {
    const Color glow = color(0xdce8ff, 220);
    const float armThickness = unit * 0.35f * scale;
    const float armLength = unit * 1.6f * scale;
    output.addFillRectangle(baseX - armLength * 0.5f, baseY - armThickness * 0.5f, armLength,
                            armThickness, glow);
    output.addFillRectangle(baseX - armThickness * 0.5f, baseY - armLength * 0.5f,
                            armThickness, armLength, glow);
}

void StarGuitarEngine::buildFrame(float width, float height, DrawList& output) {
    output.reset();
    if (!std::isfinite(width) || !std::isfinite(height) || width <= 0.0f || height <= 0.0f) {
        return;
    }

    const float renderScale = width / kReferenceWidth;
    const float groundY = height * 0.80f;
    // How far past the right edge an object may travel (in reference px)
    // before it is off-screen and its slot can be silently reused.
    const float offscreenMargin = kReferenceWidth * 0.25f;

    drawGround(output, width, height, groundY);

    // Draw far-to-near so nearer layers correctly occlude farther ones. The
    // sky layer is drawn first of all (it sits behind everything).
    for (size_t layerOrdinal = 0; layerOrdinal < kLayerCount; ++layerOrdinal) {
        auto& layer = layers_[layerOrdinal];
        const bool isSky = layerOrdinal == kSkyLayer;
        const float unit =
            std::clamp(kUnitReference * renderScale * layer.depthScale, 1.5f, 30.0f);
        for (auto& instance : layer.objects) {
            if (!instance.active) continue;
            // World position: spawned at the right edge of the reference
            // frame, moves left as it travels.
            const float worldX = kReferenceWidth - instance.traveled;
            const float screenX = worldX * renderScale;
            if (screenX < -offscreenMargin * renderScale) {
                instance.active = false;
                continue;
            }
            if (screenX > width + offscreenMargin * renderScale) continue;

            // Every instance grows in the same way, scaled to its own
            // sizeScale (a real peak's magnitude, or the fixed ambient
            // size) -- one animation rule, applied uniformly everywhere.
            const float scale = instance.sizeScale * growEase(instance.age / kGrowInSeconds);

            if (isSky) {
                // Deterministic per-instance vertical placement within the
                // upper part of the sky, kept well clear of the skyline.
                const float skyY = height * (0.06f + hash01(instance.seed) * 0.30f);
                switch (instance.type) {
                    case StarGuitarObjectType::plane:
                        drawPlane(output, screenX, skyY, unit, scale);
                        break;
                    case StarGuitarObjectType::star:
                        drawStar(output, screenX, skyY, unit, scale);
                        break;
                    default:
                        drawBird(output, screenX, skyY, unit, scale);
                        break;
                }
                continue;
            }

            switch (instance.type) {
                case StarGuitarObjectType::pole:
                    drawPole(output, screenX, groundY, unit, scale);
                    break;
                case StarGuitarObjectType::tree:
                    drawTree(output, screenX, groundY, unit, instance.seed, scale);
                    break;
                case StarGuitarObjectType::waterTower:
                    drawWaterTower(output, screenX, groundY, unit, instance.seed, scale);
                    break;
                case StarGuitarObjectType::signalMarker:
                    drawSignalMarker(output, screenX, groundY, unit, scale);
                    break;
                default:
                    drawBuilding(output, screenX, groundY, unit, instance.type, instance.seed,
                                scale);
                    break;
            }
        }
    }
}

} // namespace vizrack::builtin
