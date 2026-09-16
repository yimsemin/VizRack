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

// Eight sub-band cutoffs (Hz), low to high -- see
// docs/STAR_GUITAR_FREQUENCY_BANDS.md. Band i is the residual between
// filter i-1 and filter i (band 0 is everything below the first cutoff;
// the last band is everything above the last cutoff).
constexpr std::array<float, StarGuitarEngine::kBandCount - 1> kBandCutoffsHz{
    80.0f, 300.0f, 1000.0f, 4000.0f, 6000.0f, 8000.0f, 12000.0f};
// Per-band RMS-to-0..1 gain: higher bands carry less raw energy in a typical
// mix, so they get progressively more gain to land in a comparable range.
constexpr std::array<float, StarGuitarEngine::kBandCount> kBandGain{
    3.6f, 4.6f, 5.0f, 5.4f, 5.8f, 6.2f, 7.4f, 8.8f};
// Per-band minimum spacing between peaks: higher bands can legitimately
// retrigger faster (a hi-hat pattern is quicker than a kick pattern).
constexpr std::array<float, StarGuitarEngine::kBandCount> kBandCooldownSeconds{
    0.32f, 0.26f, 0.22f, 0.20f, 0.18f, 0.16f, 0.13f, 0.10f};
// Per-band noise floor: a peak below this absolute level is ignored even if
// its relative rise would otherwise qualify (avoids firing on near-silence).
constexpr std::array<float, StarGuitarEngine::kBandCount> kBandFloor{
    0.10f, 0.09f, 0.08f, 0.07f, 0.06f, 0.06f, 0.05f, 0.05f};

// Eight sub-bands group into four coarse controls (see
// docs/STAR_GUITAR_FREQUENCY_BANDS.md): low = bands {0,1}, mid = {2,3},
// treble = {4,5}, air = {6,7}. Each pair shares one sensitivity setting and
// spawns into one depth layer.
constexpr size_t kBandLow0 = 0, kBandLow1 = 1;
constexpr size_t kBandMid0 = 2, kBandMid1 = 3;
constexpr size_t kBandTreble0 = 4, kBandTreble1 = 5;
constexpr size_t kBandAir0 = 6, kBandAir1 = 7;

// The raw (uncalibrated) 0-100 -> threshold curve: 0 is very insensitive
// (1.10, a huge relative jump needed), 100 is very sensitive (0.15, a small
// jump is enough).
float rawThresholdFromSensitivity(int sensitivity) noexcept {
    const float t = std::clamp(static_cast<float>(sensitivity), 0.0f, 100.0f) / 100.0f;
    return 1.10f + (0.15f - 1.10f) * t;
}

// Values found good by ear during tuning, one per coarse band group (low,
// mid, treble, air), on the *raw* 0-100 scale above. thresholdFromSensitivity
// below recentres each group's curve so its own UI slider's midpoint (50)
// lands exactly on this value, while keeping the endpoints (0 and 100) at
// the same raw insensitive/sensitive extremes -- i.e. a two-segment
// piecewise-linear remap, not a shift of the whole range.
constexpr std::array<int, 4> kBandReferenceSensitivity{70, 80, 90, 80}; // low, mid, treble, air

// Maps a sensitivity setting (0-100, see StarGuitarOptions) to the relative-
// rise threshold detectPeak() requires, recentred per `groupIndex` (0=low,
// 1=mid, 2=treble, 3=air) so that setting 50 reproduces the reference value
// above under the raw curve.
float thresholdFromSensitivity(int sensitivity, size_t groupIndex) noexcept {
    const int reference = kBandReferenceSensitivity[groupIndex];
    const float low = rawThresholdFromSensitivity(0);
    const float mid = rawThresholdFromSensitivity(reference);
    const float high = rawThresholdFromSensitivity(100);
    const float clamped = std::clamp(static_cast<float>(sensitivity), 0.0f, 100.0f);
    if (clamped <= 50.0f) {
        return low + (mid - low) * (clamped / 50.0f);
    }
    return mid + (high - mid) * ((clamped - 50.0f) / 50.0f);
}

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

// Maps a peak's raw 0..1 magnitude to the visual size scale an object
// spawned from it should grow to. Every peak-driven spawn in the engine goes
// through this one mapping, so "how hard was the hit" reads consistently as
// "how big is the object" everywhere.
float sizeFromMagnitude(float magnitude) noexcept {
    return 0.65f + clampUnit(magnitude) * 1.15f;
}

} // namespace

StarGuitarEngine::StarGuitarEngine() {
    scratch_.reserve(16);

    // Far (slowest/smallest, driven by the treble group 4-8kHz): distant
    // silhouettes that loom and linger the longest.
    layers_[kFarLayer].speed = 90.0f;
    layers_[kFarLayer].depthScale = 0.55f;
    layers_[kFarLayer].baseIntervalSeconds = 2.6f;
    layers_[kFarLayer].jitterSeconds = 1.2f;

    // Mid (driven by the mid group 300Hz-4kHz): the steady middle-distance
    // cadence.
    layers_[kMidLayer].speed = 190.0f;
    layers_[kMidLayer].depthScale = 0.85f;
    layers_[kMidLayer].baseIntervalSeconds = 1.6f;
    layers_[kMidLayer].jitterSeconds = 0.9f;

    // Near (fastest/largest, driven by the low group <300Hz -- the "쿵"):
    // big, close, brief.
    layers_[kNearLayer].speed = 340.0f;
    layers_[kNearLayer].depthScale = 1.25f;
    layers_[kNearLayer].baseIntervalSeconds = 1.8f;
    layers_[kNearLayer].jitterSeconds = 1.2f;

    // Sky (driven by the air group 8kHz+): no ambient fallback -- it should
    // only ever appear on an actual air-band peak.
    layers_[kSkyLayer].speed = 55.0f;
    layers_[kSkyLayer].depthScale = 0.5f;

    for (auto& layer : layers_) {
        layer.idleTimer = layer.baseIntervalSeconds;
    }
}

void StarGuitarEngine::setOptions(StarGuitarOptions options) noexcept {
    options.lowSensitivity = std::clamp(options.lowSensitivity, 0, 100);
    options.midSensitivity = std::clamp(options.midSensitivity, 0, 100);
    options.trebleSensitivity = std::clamp(options.trebleSensitivity, 0, 100);
    options.airSensitivity = std::clamp(options.airSensitivity, 0, 100);
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

    for (auto& cooldown : bandCooldown_) cooldown = std::max(0.0f, cooldown - safeSeconds);

    if (sampleCount > 0) {
        sampleCount_ = std::min(sampleCount, kMaxSamples);
        analyzeSamples(frameScale);
    } else {
        for (auto& baseline : bandBaseline_) baseline *= std::pow(0.985f, frameScale);
        overallLevel_ *= std::pow(0.96f, frameScale);
        bandPeak_ = {};
    }

    // Silence gate: below this the track has effectively stopped (or a long
    // gap is playing), so every spawn source goes quiet instead of
    // continuing to produce scenery.
    constexpr float kSilenceLevel = 0.025f;
    constexpr float kSilenceHoldSeconds = 0.4f;
    if (overallLevel_ < kSilenceLevel) {
        silenceSeconds_ += safeSeconds;
    } else {
        silenceSeconds_ = 0.0f;
    }
    const bool audible = silenceSeconds_ < kSilenceHoldSeconds;

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

    // Near layer <- low group (<300Hz, the "쿵"): the sub-bass sub-band
    // spawns a streetlight/signal marker, the kick-body sub-band a pine tree.
    bool nearGotPeak = false;
    if (bandPeak_[kBandLow0].fired) {
        spawnInstance(layers_[kNearLayer], StarGuitarObjectType::signalMarker,
                     sizeFromMagnitude(bandPeak_[kBandLow0].magnitude));
        nearGotPeak = true;
    }
    if (bandPeak_[kBandLow1].fired) {
        spawnInstance(layers_[kNearLayer], StarGuitarObjectType::pine,
                     sizeFromMagnitude(bandPeak_[kBandLow1].magnitude));
        nearGotPeak = true;
    }
    if (!nearGotPeak) {
        spawnAmbient(layers_[kNearLayer], StarGuitarObjectType::buildingB, safeSeconds);
    }

    // Mid layer <- mid group (300Hz-1kHz spawns a tree, 1-4kHz a pole).
    bool midGotPeak = false;
    if (bandPeak_[kBandMid0].fired) {
        spawnInstance(layers_[kMidLayer], StarGuitarObjectType::tree,
                     sizeFromMagnitude(bandPeak_[kBandMid0].magnitude));
        midGotPeak = true;
    }
    if (bandPeak_[kBandMid1].fired) {
        spawnInstance(layers_[kMidLayer], StarGuitarObjectType::pole,
                     sizeFromMagnitude(bandPeak_[kBandMid1].magnitude));
        midGotPeak = true;
    }
    if (!midGotPeak) {
        spawnAmbient(layers_[kMidLayer], nextRandom() < 0.5f ? StarGuitarObjectType::pole
                                                              : StarGuitarObjectType::tree,
                    safeSeconds);
    }

    // Far layer <- treble group (4-6kHz spawns a stepped building silhouette,
    // 6-8kHz a plain building).
    bool farGotPeak = false;
    if (bandPeak_[kBandTreble0].fired) {
        spawnInstance(layers_[kFarLayer], StarGuitarObjectType::buildingC,
                     sizeFromMagnitude(bandPeak_[kBandTreble0].magnitude));
        farGotPeak = true;
    }
    if (bandPeak_[kBandTreble1].fired) {
        spawnInstance(layers_[kFarLayer], StarGuitarObjectType::buildingA,
                     sizeFromMagnitude(bandPeak_[kBandTreble1].magnitude));
        farGotPeak = true;
    }
    if (!farGotPeak) {
        spawnAmbient(layers_[kFarLayer], StarGuitarObjectType::buildingB, safeSeconds);
    }

    // Sky <- air group (8-12kHz spawns a star, >12kHz a UFO). No ambient
    // fallback: the sky only ever reflects an actual air-band peak.
    if (bandPeak_[kBandAir0].fired) {
        spawnInstance(layers_[kSkyLayer], StarGuitarObjectType::star,
                     sizeFromMagnitude(bandPeak_[kBandAir0].magnitude));
    }
    if (bandPeak_[kBandAir1].fired) {
        spawnInstance(layers_[kSkyLayer], StarGuitarObjectType::ufo,
                     sizeFromMagnitude(bandPeak_[kBandAir1].magnitude));
    }
}

void StarGuitarEngine::reset() noexcept {
    sampleCount_ = 0;
    bandFilters_ = {};
    bandBaseline_ = {};
    bandCooldown_ = {};
    bandPeak_ = {};
    overallLevel_ = 0.0f;
    silenceSeconds_ = 0.0f;
    spawnSerial_ = 0;
    randomState_ = 0x9f2c86adu;
    for (auto& layer : layers_) {
        layer.idleTimer = layer.baseIntervalSeconds;
        layer.nextSlot = 0;
        layer.objects = {};
    }
}

void StarGuitarEngine::analyzeSamples(float frameScale) noexcept {
    // Seven cascaded one-pole low-pass filters at increasing cutoffs split
    // the signal into eight sub-bands by taking successive residuals. See
    // docs/STAR_GUITAR_FREQUENCY_BANDS.md for why these specific cutoffs
    // were chosen and what each sub-band is meant to capture.
    const float rate = static_cast<float>(sampleRate_.load(std::memory_order_acquire));
    std::array<float, kBandCount - 1> coefficients{};
    for (size_t i = 0; i < coefficients.size(); ++i) {
        coefficients[i] = 1.0f - std::exp(-2.0f * kPi * kBandCutoffsHz[i] / rate);
    }

    std::array<double, kBandCount> energy{};
    for (size_t index = 0; index < sampleCount_; ++index) {
        const float left = finiteSample(left_[index]);
        const float right = finiteSample(right_[index]);
        const float mono = (left + right) * 0.5f;
        for (size_t i = 0; i < bandFilters_.size(); ++i) {
            bandFilters_[i] += coefficients[i] * (mono - bandFilters_[i]);
        }
        float previous = 0.0f;
        for (size_t i = 0; i < bandFilters_.size(); ++i) {
            const float value = bandFilters_[i] - previous;
            energy[i] += static_cast<double>(value) * value;
            previous = bandFilters_[i];
        }
        const float topValue = mono - previous;
        energy[kBandCount - 1] += static_cast<double>(topValue) * topValue;
    }

    const float divisor = static_cast<float>(std::max<size_t>(1, sampleCount_));
    std::array<float, kBandCount> target{};
    for (size_t i = 0; i < kBandCount; ++i) {
        target[i] =
            clampUnit(std::sqrt(static_cast<float>(energy[i]) / divisor) * kBandGain[i]);
    }

    // Baselines move much more slowly than the per-frame targets above --
    // they represent "what has this sub-band typically been doing the last
    // couple of seconds," which a peak is then measured against. Captured
    // *before* this frame updates them, so the peak test compares against
    // where the baseline already was, not where this frame's own energy just
    // dragged it.
    const std::array<float, kBandCount> previousBaseline = bandBaseline_;
    for (size_t i = 0; i < kBandCount; ++i) {
        bandBaseline_[i] = frameFollow(bandBaseline_[i], target[i], 0.035f, 0.02f, frameScale);
    }

    const std::array<int, 4> sensitivity{options_.lowSensitivity, options_.midSensitivity,
                                         options_.trebleSensitivity, options_.airSensitivity};
    for (size_t i = 0; i < kBandCount; ++i) {
        const float threshold = thresholdFromSensitivity(sensitivity[i / 2], i / 2);
        bandPeak_[i] = detectPeak(target[i], previousBaseline[i], bandCooldown_[i],
                                  kBandCooldownSeconds[i], threshold, kBandFloor[i]);
    }

    float sum = 0.0f;
    for (float value : target) sum += value;
    overallLevel_ = sum / static_cast<float>(kBandCount);
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

void StarGuitarEngine::drawPine(DrawList& output, float baseX, float groundY, float unit,
                                uint32_t seed, float scale) const {
    // Visually distinct from drawTree: a taller, narrower, more sharply
    // tapered silhouette (five tiers instead of three, each much narrower
    // than the last) reading as a conifer rather than a broad-canopy tree.
    const float trunkWidth = unit * 0.6f;
    const float trunkHeight = unit * 1.8f * scale;
    const Color trunk = color(0x140f0a);
    output.addFillRectangle(baseX - trunkWidth * 0.5f, groundY - trunkHeight, trunkWidth,
                            trunkHeight, trunk);

    const float canopyHeight = unit * (6.5f + hash01(seed) * 2.5f) * scale;
    const float canopyWidth = unit * (3.0f + hash01(seed ^ 0x9e3779b9u) * 1.2f) * scale;
    const Color canopy = color(0x0a1a12);
    constexpr int tiers = 5;
    for (int tier = 0; tier < tiers; ++tier) {
        const float tierFraction = static_cast<float>(tier) / static_cast<float>(tiers);
        const float tierWidth = canopyWidth * (1.0f - tierFraction * 0.8f);
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

void StarGuitarEngine::drawUfo(DrawList& output, float baseX, float baseY, float unit,
                               float scale) const {
    // A flat, wide blocky saucer with a small dome and an under-glow --
    // built entirely from rectangles, kept in the same crisp-block style as
    // the rest of the scene.
    const Color hull = color(0x2a2f3a, 235);
    const float hullWidth = unit * 4.5f * scale;
    const float hullHeight = unit * 0.9f * scale;
    output.addFillRectangle(baseX - hullWidth * 0.5f, baseY - hullHeight * 0.5f, hullWidth,
                            hullHeight, hull);

    const Color dome = color(0x8fd6ff, 210);
    const float domeWidth = unit * 2.0f * scale;
    const float domeHeight = unit * 1.0f * scale;
    output.addFillRectangle(baseX - domeWidth * 0.5f, baseY - hullHeight * 0.5f - domeHeight,
                            domeWidth, domeHeight, dome);

    const Color glow = color(0x8fd6ff, 110);
    output.addFillRectangle(baseX - hullWidth * 0.35f, baseY + hullHeight * 0.5f,
                            hullWidth * 0.7f, unit * 0.3f * scale, glow);
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
                    case StarGuitarObjectType::star:
                        drawStar(output, screenX, skyY, unit, scale);
                        break;
                    case StarGuitarObjectType::ufo:
                        drawUfo(output, screenX, skyY, unit, scale);
                        break;
                    default:
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
                case StarGuitarObjectType::pine:
                    drawPine(output, screenX, groundY, unit, instance.seed, scale);
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
