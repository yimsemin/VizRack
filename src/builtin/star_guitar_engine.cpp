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

} // namespace

StarGuitarEngine::StarGuitarEngine() {
    scratch_.reserve(16);

    // Far: small, sparse — water towers and buildings loom and linger.
    layers_[kFarLayer].speed = 90.0f;
    layers_[kFarLayer].depthScale = 0.55f;
    layers_[kFarLayer].baseIntervalSeconds = 2.4f;
    layers_[kFarLayer].jitterSeconds = 1.1f;

    // Mid: telephone-pole / tree cadence, jittered rather than metronomic,
    // plus a secondary onset for a distinct rhythmic marker (see
    // updateSecondaryOnset) so a second beat voice (e.g. a snare/clap
    // alongside a kick) is visually distinguishable from the kick-driven far
    // layer instead of blending into it.
    layers_[kMidLayer].speed = 190.0f;
    layers_[kMidLayer].depthScale = 0.85f;
    layers_[kMidLayer].baseIntervalSeconds = 1.5f;
    layers_[kMidLayer].jitterSeconds = 0.9f;

    // Near: fast, large-relative, brief — cymbal-like transients flash by.
    layers_[kNearLayer].speed = 430.0f;
    layers_[kNearLayer].depthScale = 1.35f;
    layers_[kNearLayer].baseIntervalSeconds = 1.6f;
    layers_[kNearLayer].jitterSeconds = 1.2f;

    // Sky: deliberately rare, audio-independent — a bird/plane/star should
    // read as an occasional flourish, not a steady stream.
    layers_[kSkyLayer].speed = 55.0f;
    layers_[kSkyLayer].depthScale = 0.5f;
    layers_[kSkyLayer].baseIntervalSeconds = 11.0f;
    layers_[kSkyLayer].jitterSeconds = 7.0f;

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
    const float previousLowBeat = lowBeatLevel_;
    const float previousPresenceBeat = presenceBeatLevel_;
    const float previousAirBeat = airBeatLevel_;
    if (sampleCount > 0) {
        sampleCount_ = std::min(sampleCount, kMaxSamples);
        analyzeSamples(frameScale);
    } else {
        lowLevel_ *= std::pow(0.965f, frameScale);
        midLevel_ *= std::pow(0.955f, frameScale);
        presenceLevel_ *= std::pow(0.95f, frameScale);
        airLevel_ *= std::pow(0.945f, frameScale);
        lowAverage_ *= std::pow(0.985f, frameScale);
        presenceAverage_ *= std::pow(0.98f, frameScale);
        airAverage_ *= std::pow(0.985f, frameScale);
        lowBeatLevel_ *= std::pow(0.84f, frameScale);
        presenceBeatLevel_ *= std::pow(0.68f, frameScale);
        airBeatLevel_ *= std::pow(0.62f, frameScale);
    }
    const float lowRise = std::max(0.0f, lowBeatLevel_ - previousLowBeat);
    const float presenceRise = std::max(0.0f, presenceBeatLevel_ - previousPresenceBeat);
    const float airRise = std::max(0.0f, airBeatLevel_ - previousAirBeat);

    songClock_ += safeSeconds;
    // Keep the clock bounded across a long-running session; only differences
    // between onset timestamps matter, and a wrap can only ever cost one
    // discarded interval sample.
    if (songClock_ > 1.0e6f) {
        songClock_ = 0.0f;
        lastLowOnsetTime_ = -1.0f;
    }
    const bool confirmedLowOnset = updateTempoTracker(lowRise, safeSeconds);

    const float overallLevel = (lowLevel_ + midLevel_ + presenceLevel_ + airLevel_) / 4.0f;
    songEnergy_ = frameFollow(songEnergy_, clampUnit(overallLevel), 0.02f, 0.015f, frameScale);

    // Silence gate: below this the track has effectively stopped (or a long
    // gap is playing), so every spawn source -- including the tempo-locked
    // fill-in, which otherwise has no idea whether the song is still
    // playing -- goes quiet instead of continuing to produce scenery.
    constexpr float kSilenceLevel = 0.025f;
    constexpr float kSilenceHoldSeconds = 0.4f;
    // A longer silence also invalidates the tempo lock and onset history so
    // a new song, or a new section after a real pause, re-acquires cleanly
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
        onsetIntervalCount_ = 0;
        onsetIntervalCursor_ = 0;
        lastLowOnsetTime_ = -1.0f;
    }

    for (auto& layer : layers_) {
        for (auto& instance : layer.objects) {
            if (!instance.active) continue;
            instance.traveled += layer.speed * safeSeconds;
            // Objects are reclaimed lazily in buildFrame once they scroll
            // fully off screen (screen width varies per call), so no
            // fixed-lifetime cutoff is needed here.
        }
        layer.spawnCooldown = std::max(0.0f, layer.spawnCooldown - safeSeconds);
        layer.secondaryCooldown = std::max(0.0f, layer.secondaryCooldown - safeSeconds);
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

    // Confirmed low-band onset (kick-like, the "쿵") -> far layer: water
    // towers and buildings that loom and linger.
    updateEdgeTriggeredLayer(layers_[kFarLayer], confirmedLowOnset,
                             (lowLevel_ > 0.55f) ? StarGuitarObjectType::waterTower
                                                  : StarGuitarObjectType::buildingA,
                             StarGuitarObjectType::buildingB, safeSeconds);

    // The mid layer is the visible "pulse": it spawns a pole/tree directly on
    // every confirmed low-band onset (landing alongside the far layer's own
    // kick reaction); in predictive mode it also fills in on the estimated
    // tempo between onsets once a lock exists (see StarGuitarAlgorithmMode).
    updateMidLayerBeatLocked(layers_[kMidLayer], confirmedLowOnset, options_.algorithmMode,
                             StarGuitarObjectType::pole, StarGuitarObjectType::tree, safeSeconds);
    // Presence-band onset (snare/clap-like, the "짝") -> a bright trackside
    // signal marker layered onto the same mid layer, so the two alternating
    // rhythmic voices are both visible without one masking the other.
    updateSecondaryOnset(layers_[kMidLayer], presenceRise, 0.05f,
                         StarGuitarObjectType::signalMarker, safeSeconds);

    // Air-band onset (hi-hat/cymbal/shimmer-like) -> near layer: a quick
    // bright flash close to the viewer.
    const bool nearOnset = detectOnset(airRise, 0.08f, layers_[kNearLayer].spawnCooldown, 0.16f);
    updateEdgeTriggeredLayer(layers_[kNearLayer], nearOnset, StarGuitarObjectType::signalMarker,
                             StarGuitarObjectType::buildingC, safeSeconds);

    // Sky: must be driven by the air band (the highest band) and nothing
    // lower, and stays rare regardless.
    updateSkyLayer(layers_[kSkyLayer], airRise, safeSeconds);
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
    lowAverage_ = 0.0f;
    presenceAverage_ = 0.0f;
    airAverage_ = 0.0f;
    lowBeatLevel_ = 0.0f;
    presenceBeatLevel_ = 0.0f;
    airBeatLevel_ = 0.0f;
    songEnergy_ = 0.0f;
    silenceSeconds_ = 0.0f;
    songClock_ = 0.0f;
    tempoOnsetCooldown_ = 0.0f;
    lastLowOnsetTime_ = -1.0f;
    beatPeriodSeconds_ = 0.5f;
    beatPhase_ = 0.0f;
    tempoLocked_ = false;
    onsetIntervals_ = {};
    onsetIntervalCount_ = 0;
    onsetIntervalCursor_ = 0;
    spawnSerial_ = 0;
    randomState_ = 0x9f2c86adu;
    for (auto& layer : layers_) {
        layer.spawnCooldown = 0.0f;
        layer.secondaryCooldown = 0.0f;
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

    const float previousLowAverage = lowAverage_;
    lowAverage_ = frameFollow(lowAverage_, lowTarget, 0.018f, 0.012f, frameScale);
    // The excess-over-average margin (0.03) and cooldown floor (set where
    // this feeds the tempo tracker) are deliberately not too sensitive: a
    // busy, continuously-present bassline can drift the low band's level up
    // and down on its own just from pitch/note changes, which otherwise gets
    // misread as a stream of kick onsets and produces bursts of far/mid
    // scenery in sections that don't actually have a stronger beat.
    const float lowBeatTarget = clampUnit(
        std::max(0.0f, lowTarget - previousLowAverage - 0.03f) * 4.8f +
        std::max(0.0f, lowTarget - lowLevel_) * 1.5f);
    lowBeatLevel_ = frameFollow(lowBeatLevel_, lowBeatTarget, 0.66f, 0.095f, frameScale);

    // Presence-band envelope (2.5-6kHz): a snare/clap's identifying snap and
    // vocal sibilance live here, distinct from both the kick (low) and the
    // hi-hat/cymbal shimmer (air, see below).
    const float previousPresenceAverage = presenceAverage_;
    presenceAverage_ = frameFollow(presenceAverage_, presenceTarget, 0.07f, 0.04f, frameScale);
    const float presenceBeatTarget = clampUnit(
        std::max(0.0f, presenceTarget - previousPresenceAverage - 0.03f) * 5.2f);
    presenceBeatLevel_ =
        frameFollow(presenceBeatLevel_, presenceBeatTarget, 0.78f, 0.42f, frameScale);

    // Air-band envelope (>6kHz) uses a much faster attack/release than the
    // low band: it should register a rising edge on a brief transient
    // (hi-hat, cymbal, shimmer) and decay again almost immediately, rather
    // than sustain. This is the only band the sky layer may use.
    const float previousAirAverage = airAverage_;
    airAverage_ = frameFollow(airAverage_, airTarget, 0.05f, 0.03f, frameScale);
    const float airBeatTarget =
        clampUnit(std::max(0.0f, airTarget - previousAirAverage - 0.02f) * 5.5f);
    airBeatLevel_ = frameFollow(airBeatLevel_, airBeatTarget, 0.85f, 0.5f, frameScale);

    lowLevel_ = frameFollow(lowLevel_, lowTarget, 0.20f, 0.045f, frameScale);
    // Mid (150Hz-2.5kHz, vocals/guitars/keys) is deliberately not given an
    // onset envelope: it's too dense and continuously-present in most mixes
    // to make a clean rhythm trigger, so it's tracked only as a sustained
    // "how busy is the song" measure for songEnergy_ and the reactive mode's
    // ambient cadence.
    midLevel_ = frameFollow(midLevel_, midTarget, 0.18f, 0.05f, frameScale);
    presenceLevel_ = frameFollow(presenceLevel_, presenceTarget, 0.16f, 0.055f, frameScale);
    airLevel_ = frameFollow(airLevel_, airTarget, 0.16f, 0.055f, frameScale);
}

float StarGuitarEngine::nextRandom() noexcept {
    randomState_ ^= randomState_ << 13;
    randomState_ ^= randomState_ >> 17;
    randomState_ ^= randomState_ << 5;
    return static_cast<float>(randomState_ & 0x00ffffffu) / 16777215.0f;
}

void StarGuitarEngine::spawnInstance(Layer& layer, StarGuitarObjectType type) noexcept {
    // Search for a free slot starting at the round-robin cursor. If every
    // slot is still occupied by an object that hasn't scrolled off screen
    // yet, drop this spawn instead of overwriting (and visually truncating)
    // one that's still mid-flight -- that overwrite was the bug behind far
    // objects appearing to vanish before reaching the left edge.
    for (size_t attempt = 0; attempt < kLayerCapacity; ++attempt) {
        const size_t index = (layer.nextSlot + attempt) % kLayerCapacity;
        Instance& instance = layer.objects[index];
        if (instance.active) continue;
        layer.nextSlot = (index + 1) % kLayerCapacity;
        ++spawnSerial_;
        instance.active = true;
        instance.type = type;
        instance.traveled = 0.0f;
        instance.seed = hashCombine(spawnSerial_, spawnSerial_ * 2246822519u);
        return;
    }
}

bool StarGuitarEngine::detectOnset(float rise, float threshold, float& cooldown,
                                   float cooldownSeconds) noexcept {
    if (rise > threshold && cooldown <= 0.0f) {
        cooldown = cooldownSeconds + nextRandom() * (cooldownSeconds * 0.4f);
        return true;
    }
    return false;
}

void StarGuitarEngine::updateEdgeTriggeredLayer(Layer& layer, bool onset,
                                                StarGuitarObjectType onsetType,
                                                StarGuitarObjectType ambientType,
                                                float frameSeconds) noexcept {
    layer.idleTimer -= frameSeconds;
    if (onset) {
        spawnInstance(layer, onsetType);
        layer.idleTimer = layer.baseIntervalSeconds + nextRandom() * layer.jitterSeconds;
        return;
    }
    if (layer.idleTimer <= 0.0f) {
        // Ambient fallback: keeps the layer from sitting empty through a
        // quiet-but-audible passage, on a randomized (not fixed-grid)
        // cadence. Suspended entirely during silence by the caller.
        spawnInstance(layer, ambientType);
        layer.idleTimer = layer.baseIntervalSeconds + nextRandom() * layer.jitterSeconds;
    }
}

void StarGuitarEngine::updateIntervalLayer(Layer& layer, float sustainedLevel,
                                           StarGuitarObjectType primaryType,
                                           StarGuitarObjectType secondaryType,
                                           float frameSeconds) noexcept {
    layer.idleTimer -= frameSeconds;
    if (layer.idleTimer > 0.0f) return;
    // Mixing in the secondary type (trees alongside poles) keeps the cadence
    // from reading as one repeating shape even though the timing itself is
    // still driven by a single interval.
    spawnInstance(layer, nextRandom() < 0.55f ? primaryType : secondaryType);
    // Higher sustained energy shortens the average interval (busier cadence)
    // while jitter keeps consecutive spawns asymmetric instead of a strict
    // metronome grid.
    const float energyFactor = 1.0f + sustainedLevel * 1.8f;
    layer.idleTimer =
        (layer.baseIntervalSeconds / energyFactor) + nextRandom() * layer.jitterSeconds;
}

void StarGuitarEngine::updateSecondaryOnset(Layer& layer, float rise, float threshold,
                                            StarGuitarObjectType type,
                                            float frameSeconds) noexcept {
    (void)frameSeconds;
    if (detectOnset(rise, threshold, layer.secondaryCooldown, 0.22f)) {
        spawnInstance(layer, type);
    }
}

bool StarGuitarEngine::updateTempoTracker(float lowRise, float frameSeconds) noexcept {
    tempoOnsetCooldown_ = std::max(0.0f, tempoOnsetCooldown_ - frameSeconds);
    // A tempo lock that never confirms another onset for a long stretch is
    // stale -- either the song stopped, or moved to a section without a
    // clear kick -- so stop trusting it rather than letting the mid layer's
    // fill-in free-run on an old estimate. This is the fix for objects
    // continuing to scroll on after the audio has actually stopped.
    if (tempoLocked_ && lastLowOnsetTime_ >= 0.0f &&
        (songClock_ - lastLowOnsetTime_) > beatPeriodSeconds_ * 3.0f) {
        tempoLocked_ = false;
    }
    // The threshold and cooldown floor here are deliberately conservative: a
    // busy, continuously-present bassline can wobble the low band enough on
    // its own to look like a stream of kicks, which was producing bursts of
    // far/mid scenery clustered wherever the bassline happened to be active
    // rather than tracking the actual beat.
    const bool confirmed = detectOnset(lowRise, 0.07f, tempoOnsetCooldown_, 0.28f);
    if (confirmed) {
        if (lastLowOnsetTime_ >= 0.0f) {
            const float interval = songClock_ - lastLowOnsetTime_;
            // Only trust intervals inside a plausible tempo range (roughly
            // 45-215 BPM, matching the cooldown floor above); anything
            // outside that is almost certainly a missed or doubled detection
            // rather than a real beat-to-beat gap, and would otherwise drag
            // the median estimate off in one step.
            if (interval > 0.27f && interval < 1.35f) {
                onsetIntervals_[onsetIntervalCursor_] = interval;
                onsetIntervalCursor_ = (onsetIntervalCursor_ + 1) % kOnsetHistory;
                onsetIntervalCount_ = std::min(onsetIntervalCount_ + 1, kOnsetHistory);
            }
        }
        lastLowOnsetTime_ = songClock_;

        if (onsetIntervalCount_ >= 3) {
            std::array<float, kOnsetHistory> sorted{};
            std::copy_n(onsetIntervals_.begin(), onsetIntervalCount_, sorted.begin());
            std::sort(sorted.begin(), sorted.begin() + static_cast<ptrdiff_t>(onsetIntervalCount_));
            const float median = sorted[onsetIntervalCount_ / 2];
            // Blend toward the new median rather than snapping to it, so one
            // odd interval (a fill, a skipped beat) nudges the estimate
            // instead of yanking it.
            beatPeriodSeconds_ = tempoLocked_ ? beatPeriodSeconds_ * 0.75f + median * 0.25f
                                              : median;
            tempoLocked_ = true;
        }
        // Re-sync phase to the real hit every time -- this is what keeps the
        // visual pulse from ever drifting away from the audible one.
        beatPhase_ = 0.0f;
    }
    return confirmed;
}

void StarGuitarEngine::updateMidLayerBeatLocked(Layer& layer, bool confirmedOnset,
                                                StarGuitarAlgorithmMode mode,
                                                StarGuitarObjectType primaryType,
                                                StarGuitarObjectType secondaryType,
                                                float frameSeconds) noexcept {
    const auto spawnPulse = [&] {
        spawnInstance(layer, nextRandom() < 0.55f ? primaryType : secondaryType);
    };
    // Both modes react to a confirmed kick the same way -- that's a direct
    // reaction, not a prediction, so it belongs in both. Only the fill-in
    // between confirmed onsets differs by mode.
    if (confirmedOnset) {
        spawnPulse();
        return;
    }
    if (mode == StarGuitarAlgorithmMode::predictive && tempoLocked_) {
        // Fill in on the estimated beat phase so the cadence stays steady
        // even through a soft hit the onset detector misses.
        beatPhase_ += frameSeconds / std::max(beatPeriodSeconds_, 0.05f);
        if (beatPhase_ >= 1.0f) {
            beatPhase_ -= 1.0f;
            spawnPulse();
        }
        return;
    }
    // Reactive mode, or predictive mode before a tempo lock is acquired:
    // fall back to the original jittered interval timer keyed to general
    // mid-band busyness, with no tempo estimate involved at all.
    updateIntervalLayer(layer, midLevel_, primaryType, secondaryType, frameSeconds);
}

void StarGuitarEngine::updateSkyLayer(Layer& layer, float airRise, float frameSeconds) noexcept {
    // The sky must be driven by the air band (hi-hats/cymbals/shimmer, the
    // highest band) and nothing lower -- a song with no air-band presence at
    // all simply never spawns anything here, which is the correct behaviour
    // for "the sky reflects the song" rather than "the sky is a timer."
    // (layer.spawnCooldown is already decremented once per frame in update().)
    (void)frameSeconds;
    if (airRise <= 0.05f || layer.spawnCooldown > 0.0f) return;

    const float pick = nextRandom();
    const StarGuitarObjectType type =
        pick < 0.5f ? StarGuitarObjectType::bird
                    : pick < 0.8f ? StarGuitarObjectType::star : StarGuitarObjectType::plane;
    spawnInstance(layer, type);

    // The minimum spacing shortens a little as the song's overall energy
    // rises, but stays within a several-second-plus range either way, so a
    // hi-hat-heavy mix still reads as an occasional flourish rather than a
    // stream even though air-band onsets themselves can be frequent.
    const float energeticInterval = layer.baseIntervalSeconds * 0.55f;
    const float interval =
        layer.baseIntervalSeconds - songEnergy_ * (layer.baseIntervalSeconds - energeticInterval);
    layer.spawnCooldown = interval + nextRandom() * layer.jitterSeconds;
}

void StarGuitarEngine::drawGround(DrawList& output, float width, float height,
                                  float groundY) const {
    output.addVerticalGradient(0.0f, 0.0f, width, groundY, color(0x0b1424), color(0x2a3f5c));
    output.addFillRectangle(0.0f, groundY, width, height - groundY, color(0x05070c));
    // A thin lit strip along the horizon reads as a rail/road line.
    output.addFillRectangle(0.0f, groundY - 2.0f, width, 2.0f, color(0x40597a));
}

void StarGuitarEngine::drawPole(DrawList& output, float baseX, float groundY,
                                float unit) const {
    const float poleWidth = unit * 0.9f;
    const float poleHeight = unit * 11.0f;
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
                                uint32_t seed) const {
    const float trunkWidth = unit * 0.8f;
    const float trunkHeight = unit * 2.6f;
    const Color trunk = color(0x120e0a);
    output.addFillRectangle(baseX - trunkWidth * 0.5f, groundY - trunkHeight, trunkWidth,
                            trunkHeight, trunk);

    // A stepped, blocky canopy: three shrinking tiers instead of a smooth
    // circle, keeping the silhouette axis-aligned like the rest of the scene.
    const float canopyHeight = unit * (4.5f + hash01(seed) * 2.0f);
    const float canopyWidth = unit * (4.0f + hash01(seed ^ 0x27d4eb2fu) * 2.2f);
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

void StarGuitarEngine::drawBuilding(DrawList& output, float baseX, float groundY,
                                    float unit, StarGuitarObjectType variant,
                                    uint32_t seed) const {
    const float heightUnits = 8.0f + hash01(seed) * 10.0f;
    const float widthUnits = 5.0f + hash01(seed ^ 0x51ed270bu) * 3.0f;
    const float bodyWidth = unit * widthUnits;
    const float bodyHeight = unit * heightUnits;
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

void StarGuitarEngine::drawWaterTower(DrawList& output, float baseX, float groundY,
                                      float unit, uint32_t seed) {
    const Color body = color(0x0d1218);
    const float legHeight = unit * 6.0f;
    const float tankHalfWidth = unit * 3.4f;
    const float tankHeight = unit * 3.4f;
    const float tankY = groundY - legHeight - tankHeight;

    // Four blocky support legs.
    constexpr std::array<float, 4> legOffsets{-2.6f, -1.1f, 1.1f, 2.6f};
    for (const float offset : legOffsets) {
        output.addFillRectangle(baseX + offset * unit - unit * 0.22f, groundY - legHeight,
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
    output.addFillRectangle(baseX - tankHalfWidth * 0.6f, tankY - unit * 0.7f,
                            tankHalfWidth * 1.2f, unit * 0.7f, body);
    output.addFillRectangle(baseX - tankHalfWidth * 0.28f, tankY - unit * 1.2f,
                            tankHalfWidth * 0.56f, unit * 0.5f, body);
    (void)seed;
}

void StarGuitarEngine::drawSignalMarker(DrawList& output, float baseX, float groundY,
                                        float unit) const {
    // A bright, high-contrast trackside signal: deliberately unlike the dark
    // silhouettes around it so a rhythmic onset reads as a visible flash
    // rather than blending into the scenery.
    const float postWidth = unit * 0.5f;
    const float postHeight = unit * 3.2f;
    const Color post = color(0x0a0e16);
    output.addFillRectangle(baseX - postWidth * 0.5f, groundY - postHeight, postWidth,
                            postHeight, post);
    const float lampSize = unit * 1.4f;
    const Color lamp = color(0xe0a63a, 235);
    output.addFillRectangle(baseX - lampSize * 0.5f, groundY - postHeight - lampSize * 0.85f,
                            lampSize, lampSize, lamp);
}

void StarGuitarEngine::drawBird(DrawList& output, float baseX, float baseY,
                                float unit) const {
    // A small chevron silhouette built from two blocky wing rectangles.
    const Color body = color(0x0c1017, 220);
    const float wingWidth = unit * 1.6f;
    const float wingHeight = unit * 0.5f;
    output.addFillRectangle(baseX - wingWidth, baseY - wingHeight * 0.5f, wingWidth,
                            wingHeight, body);
    output.addFillRectangle(baseX, baseY - wingHeight * 0.5f, wingWidth, wingHeight, body);
}

void StarGuitarEngine::drawPlane(DrawList& output, float baseX, float baseY,
                                 float unit) const {
    const Color body = color(0x0c1017, 230);
    const float fuselageLength = unit * 5.0f;
    const float fuselageHeight = unit * 0.6f;
    output.addFillRectangle(baseX - fuselageLength * 0.5f, baseY - fuselageHeight * 0.5f,
                            fuselageLength, fuselageHeight, body);
    const float wingWidth = unit * 1.0f;
    const float wingHeight = unit * 2.2f;
    output.addFillRectangle(baseX - wingWidth * 0.5f, baseY - wingHeight * 0.5f, wingWidth,
                            wingHeight, body);
}

void StarGuitarEngine::drawStar(DrawList& output, float baseX, float baseY,
                                float unit) const {
    const Color glow = color(0xdce8ff, 220);
    const float armThickness = unit * 0.35f;
    const float armLength = unit * 1.6f;
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

            if (isSky) {
                // Deterministic per-instance vertical placement within the
                // upper part of the sky, kept well clear of the skyline.
                const float skyY = height * (0.06f + hash01(instance.seed) * 0.30f);
                switch (instance.type) {
                    case StarGuitarObjectType::plane:
                        drawPlane(output, screenX, skyY, unit);
                        break;
                    case StarGuitarObjectType::star:
                        drawStar(output, screenX, skyY, unit);
                        break;
                    default:
                        drawBird(output, screenX, skyY, unit);
                        break;
                }
                continue;
            }

            switch (instance.type) {
                case StarGuitarObjectType::pole:
                    drawPole(output, screenX, groundY, unit);
                    break;
                case StarGuitarObjectType::tree:
                    drawTree(output, screenX, groundY, unit, instance.seed);
                    break;
                case StarGuitarObjectType::waterTower:
                    drawWaterTower(output, screenX, groundY, unit, instance.seed);
                    break;
                case StarGuitarObjectType::signalMarker:
                    drawSignalMarker(output, screenX, groundY, unit);
                    break;
                default:
                    drawBuilding(output, screenX, groundY, unit, instance.type, instance.seed);
                    break;
            }
        }
    }
}

} // namespace vizrack::builtin
