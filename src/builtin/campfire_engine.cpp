#include "builtin/campfire_engine.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace vizrack::builtin {
namespace {

constexpr float kPi = std::numbers::pi_v<float>;

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

float hash01(uint32_t value) noexcept {
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    value *= 0x846ca68bu;
    value ^= value >> 16;
    return static_cast<float>(value & 0x00ffffffu) / 16777215.0f;
}

float cycle(float value) noexcept {
    return value - std::floor(value);
}

float smoothNoise(float coordinate, uint32_t seed) noexcept {
    const float floored = std::floor(coordinate);
    const auto cell = static_cast<uint32_t>(static_cast<int32_t>(floored));
    const float fraction = coordinate - floored;
    const float smooth = fraction * fraction * (3.0f - 2.0f * fraction);
    const float first = hash01(cell ^ seed);
    const float second = hash01((cell + 1u) ^ seed);
    return first + (second - first) * smooth;
}

Point rotatedPoint(float centerX, float centerY, float axisX, float axisY,
                   float normalX, float normalY, float along, float across) noexcept {
    return {centerX + axisX * along + normalX * across,
            centerY + axisY * along + normalY * across};
}

} // namespace

CampfireEngine::CampfireEngine() {
    scratch_.reserve(96);
    meteorCountdown_ = 38.0f + hash01(0x71e3a95du ^ 0xb84f2d17u) * 44.0f;
}

void CampfireEngine::setOptions(CampfireOptions options) noexcept {
    options.flameResponse = std::clamp(options.flameResponse, 0, 100);
    options.starSpeed = std::clamp(options.starSpeed, 0, 100);
    options.starBrightness = std::clamp(options.starBrightness, 0, 100);
    options.starResponse = std::clamp(options.starResponse, 0, 100);
    options.particleAmount = std::clamp(options.particleAmount, 0, 100);
    options.particleIntensity = std::clamp(options.particleIntensity, 0, 100);
    options_ = options;
}

void CampfireEngine::setSampleRate(uint32_t sampleRate) noexcept {
    if (sampleRate >= 8000 && sampleRate <= 768000) {
        sampleRate_.store(sampleRate, std::memory_order_release);
    }
}

void CampfireEngine::update(size_t sampleCount, float frameSeconds) noexcept {
    const float safeSeconds = std::isfinite(frameSeconds)
                                  ? std::clamp(frameSeconds, 1.0f / 240.0f, 1.0f / 10.0f)
                                  : 1.0f / 60.0f;
    const float frameScale = safeSeconds * 60.0f;
    const float previousBeat = beatLevel_;
    if (sampleCount > 0) {
        sampleCount_ = std::min(sampleCount, kMaxSamples);
        analyzeSamples(frameScale);
    } else {
        lowLevel_ *= std::pow(0.965f, frameScale);
        midLevel_ *= std::pow(0.955f, frameScale);
        highLevel_ *= std::pow(0.945f, frameScale);
        stereoLevel_ *= std::pow(0.96f, frameScale);
        lowAverage_ *= std::pow(0.985f, frameScale);
        beatLevel_ *= std::pow(0.84f, frameScale);
        activityLevel_ *= std::pow(0.965f, frameScale);
        signalLevel_ *= std::pow(0.80f, frameScale);
    }
    if (signalLevel_ > 0.018f) {
        quietSeconds_ = 0.0f;
    } else {
        quietSeconds_ = std::min(quietSeconds_ + safeSeconds, 3600.0f);
    }
    const float targetFireScale = quietSeconds_ >= 10.0f ? 0.34f : 1.0f;
    fireScale_ = frameFollow(fireScale_, targetFireScale, 0.16f, 0.035f, frameScale);
    time_ = std::fmod(time_ + safeSeconds, 4096.0f);
    const float starSpeed =
        static_cast<float>(options_.starSpeed) / 100.0f * 0.008f;
    starRotation_ = std::fmod(starRotation_ + safeSeconds * starSpeed, kPi * 2.0f);
    const float beatRise = std::max(0.0f, beatLevel_ - previousBeat);
    updateStarPulses(safeSeconds, beatRise);
    updateMeteor(safeSeconds);
    updateEmbers(safeSeconds, beatRise);
}

void CampfireEngine::reset() noexcept {
    sampleCount_ = 0;
    lowFilter_ = 0.0f;
    midFilter_ = 0.0f;
    lowLevel_ = 0.0f;
    midLevel_ = 0.0f;
    highLevel_ = 0.0f;
    stereoLevel_ = 0.0f;
    lowAverage_ = 0.0f;
    beatLevel_ = 0.0f;
    activityLevel_ = 0.0f;
    signalLevel_ = 0.0f;
    emberSpawnAccumulator_ = 0.0f;
    quietSeconds_ = 0.0f;
    fireScale_ = 1.0f;
    time_ = 0.0f;
    starRotation_ = 0.0f;
    meteorCountdown_ = 38.0f + hash01(0x71e3a95du ^ 0xb84f2d17u) * 44.0f;
    meteorAge_ = 0.0f;
    meteorLifetime_ = 1.0f;
    meteorStartX_ = 0.0f;
    meteorStartY_ = 0.0f;
    meteorVelocityX_ = 0.0f;
    meteorVelocityY_ = 0.0f;
    meteorActive_ = false;
    randomState_ = 0x71e3a95du;
    beatSerial_ = 0;
    meteorSerial_ = 0;
    embers_ = {};
    starPulses_ = {};
    starPulseDelays_ = {};
    starPendingPulses_ = {};
}

void CampfireEngine::analyzeSamples(float frameScale) noexcept {
    const float rate = static_cast<float>(sampleRate_.load(std::memory_order_acquire));
    const float lowCoefficient = 1.0f - std::exp(-2.0f * kPi * 170.0f / rate);
    const float midCoefficient = 1.0f - std::exp(-2.0f * kPi * 2100.0f / rate);
    double lowEnergy = 0.0;
    double midEnergy = 0.0;
    double highEnergy = 0.0;
    double sideEnergy = 0.0;
    for (size_t index = 0; index < sampleCount_; ++index) {
        const float left = finiteSample(left_[index]);
        const float right = finiteSample(right_[index]);
        const float mono = (left + right) * 0.5f;
        lowFilter_ += lowCoefficient * (mono - lowFilter_);
        midFilter_ += midCoefficient * (mono - midFilter_);
        const float low = lowFilter_;
        const float mid = midFilter_ - lowFilter_;
        const float high = mono - midFilter_;
        const float side = (left - right) * 0.5f;
        lowEnergy += static_cast<double>(low) * low;
        midEnergy += static_cast<double>(mid) * mid;
        highEnergy += static_cast<double>(high) * high;
        sideEnergy += static_cast<double>(side) * side;
    }
    const float divisor = static_cast<float>(std::max<size_t>(1, sampleCount_));
    const float lowTarget = clampUnit(std::sqrt(static_cast<float>(lowEnergy) / divisor) * 4.3f);
    const float midTarget = clampUnit(std::sqrt(static_cast<float>(midEnergy) / divisor) * 5.7f);
    const float highTarget =
        clampUnit(std::sqrt(static_cast<float>(highEnergy) / divisor) * 7.5f);
    const float sideTarget =
        clampUnit(std::sqrt(static_cast<float>(sideEnergy) / divisor) * 5.5f);
    const float previousAverage = lowAverage_;
    lowAverage_ = frameFollow(lowAverage_, lowTarget, 0.018f, 0.012f, frameScale);
    const float beatTarget = clampUnit(
        std::max(0.0f, lowTarget - previousAverage - 0.018f) * 4.8f +
        std::max(0.0f, lowTarget - lowLevel_) * 1.5f);
    beatLevel_ = frameFollow(beatLevel_, beatTarget, 0.66f, 0.095f, frameScale);
    const float activityTarget =
        clampUnit(lowTarget * 0.52f + midTarget * 0.30f + highTarget * 0.18f);
    signalLevel_ = std::max({lowTarget, midTarget, highTarget});
    activityLevel_ =
        frameFollow(activityLevel_, activityTarget, 0.14f, 0.035f, frameScale);
    lowLevel_ = frameFollow(lowLevel_, lowTarget, 0.20f, 0.045f, frameScale);
    midLevel_ = frameFollow(midLevel_, midTarget, 0.18f, 0.05f, frameScale);
    highLevel_ = frameFollow(highLevel_, highTarget, 0.16f, 0.055f, frameScale);
    stereoLevel_ = frameFollow(stereoLevel_, sideTarget, 0.16f, 0.05f, frameScale);
}

float CampfireEngine::nextRandom() noexcept {
    randomState_ ^= randomState_ << 13;
    randomState_ ^= randomState_ >> 17;
    randomState_ ^= randomState_ << 5;
    return static_cast<float>(randomState_ & 0x00ffffffu) / 16777215.0f;
}

void CampfireEngine::spawnEmber() noexcept {
    const auto found = std::find_if(embers_.begin(), embers_.end(),
                                    [](const EmberParticle& ember) {
                                        return !ember.active;
                                    });
    if (found == embers_.end()) return;
    found->age = 0.0f;
    found->lifetime = 1.35f + nextRandom() * 1.65f;
    found->origin =
        (nextRandom() - 0.5f) * (0.30f + activityLevel_ * 0.16f);
    found->drift =
        (nextRandom() - 0.5f) * (0.32f + stereoLevel_ * 0.08f);
    found->lift = 0.62f + nextRandom() * 0.58f;
    found->size = 0.72f + nextRandom() * 0.78f;
    found->phase = nextRandom() * kPi * 2.0f;
    found->heat = clampUnit(0.56f + highLevel_ * 0.30f + beatLevel_ * 0.48f);
    found->active = true;
}

void CampfireEngine::updateEmbers(float frameSeconds, float beatRise) noexcept {
    for (auto& ember : embers_) {
        if (!ember.active) continue;
        ember.age += frameSeconds;
        if (ember.age >= ember.lifetime) ember.active = false;
    }
    const float amountScale = static_cast<float>(options_.particleAmount) / 50.0f;
    const float awakeScale = 0.18f + fireScale_ * 0.82f;
    const float spawnRate = awakeScale *
        (1.8f + activityLevel_ * 5.0f + highLevel_ * 13.0f + beatLevel_ * 27.0f);
    emberSpawnAccumulator_ +=
        amountScale * (frameSeconds * spawnRate + beatRise * 11.0f);
    int spawned = 0;
    while (emberSpawnAccumulator_ >= 1.0f && spawned < 10) {
        spawnEmber();
        emberSpawnAccumulator_ -= 1.0f;
        ++spawned;
    }
    emberSpawnAccumulator_ = std::min(emberSpawnAccumulator_, 10.0f);
}

void CampfireEngine::updateStarPulses(float frameSeconds, float beatRise) noexcept {
    const float frameScale = frameSeconds * 60.0f;
    for (size_t star = 0; star < kStarCount; ++star) {
        if (starPulseDelays_[star] > 0.0f) {
            starPulseDelays_[star] -= frameSeconds;
            if (starPulseDelays_[star] <= 0.0f) {
                starPulses_[star] =
                    std::max(starPulses_[star], starPendingPulses_[star]);
                starPendingPulses_[star] = 0.0f;
            }
        } else {
            starPulses_[star] *= std::pow(0.88f, frameScale);
        }
    }
    if (beatRise < 0.045f) return;
    ++beatSerial_;
    for (size_t star = 0; star < kStarCount; ++star) {
        const auto index = static_cast<uint32_t>(star);
        const float selection =
            hash01(index * 83u + beatSerial_ * 409u + 0x7f4a7c15u);
        if (selection < 0.57f) continue;
        starPulseDelays_[star] =
            hash01(index * 113u + beatSerial_ * 271u + 17u) * 0.42f;
        starPendingPulses_[star] =
            std::max(starPendingPulses_[star],
                     clampUnit(beatLevel_ * (0.62f + selection * 0.38f)));
    }
}

void CampfireEngine::updateMeteor(float frameSeconds) noexcept {
    if (meteorActive_) {
        meteorAge_ += frameSeconds;
        if (meteorAge_ < meteorLifetime_) return;
        meteorActive_ = false;
        meteorAge_ = 0.0f;
        ++meteorSerial_;
        meteorCountdown_ =
            38.0f + hash01(meteorSerial_ * 313u + 0x4f1bbcdcu) * 44.0f;
        return;
    }

    meteorCountdown_ -= frameSeconds;
    if (meteorCountdown_ > 0.0f) return;
    meteorActive_ = true;
    meteorAge_ = 0.0f;
    meteorLifetime_ =
        0.92f + hash01(meteorSerial_ * 197u + 0x8d12e5a3u) * 0.48f;
    meteorStartX_ =
        0.04f + hash01(meteorSerial_ * 239u + 0x3b629c11u) * 0.46f;
    meteorStartY_ =
        0.06f + hash01(meteorSerial_ * 283u + 0xa174f35du) * 0.20f;
    meteorVelocityX_ =
        0.28f + hash01(meteorSerial_ * 337u + 0x672fa891u) * 0.13f;
    meteorVelocityY_ =
        0.20f + hash01(meteorSerial_ * 379u + 0xd5298e47u) * 0.11f;
}

void CampfireEngine::drawBackground(DrawList& output, float width, float height,
                                    float centerX, float baseY, float extent) const {
    const float glowScale = 0.24f + fireScale_ * 0.76f;
    output.addVerticalGradient(0.0f, 0.0f, width, height,
                               color(0x010308), color(0x0b0605));
    drawStars(output, width, height, baseY);
    output.addRadialGradientEllipse(
        centerX - extent * 0.58f, baseY - extent * 0.55f,
        extent * 1.16f, extent * 0.92f,
        color(0xff6a18, static_cast<uint8_t>(
                              (21.0f + lowLevel_ * 11.0f) * glowScale)),
        color(0x120503, 0));
    output.addRadialGradientEllipse(
        centerX - extent * 0.38f, baseY - extent * 0.26f,
        extent * 0.76f, extent * 0.48f,
        color(0xffb52b, static_cast<uint8_t>(
                              (36.0f + lowLevel_ * 17.0f) * glowScale)),
        color(0x291004, 0));
    drawMeteor(output, width, std::min(baseY * 0.82f, height * 0.72f));
}

void CampfireEngine::drawStars(DrawList& output, float width, float height,
                               float baseY) const {
    const float scale = std::clamp(std::min(width, height) / 820.0f, 0.65f, 1.55f);
    const float skyHeight = std::min(baseY * 0.82f, height * 0.72f);
    const float speedSetting = static_cast<float>(options_.starSpeed) / 100.0f;
    const float brightnessSetting =
        static_cast<float>(options_.starBrightness) / 100.0f;
    const float responseSetting =
        static_cast<float>(options_.starResponse) / 100.0f;
    const float poleX = width * 0.70f;
    const float poleY = skyHeight * 1.16f;
    constexpr float ellipseScale = 0.58f;
    const float angularSpeed = speedSetting * 0.008f;
    const auto rotateFromInitial = [&](float initialX, float initialY,
                                       float angle) {
        const float dx = initialX - poleX;
        const float dy = (initialY - poleY) / ellipseScale;
        const float cosine = std::cos(angle);
        const float sine = std::sin(angle);
        return Point{poleX + dx * cosine - dy * sine,
                     poleY + (dx * sine + dy * cosine) * ellipseScale};
    };
    for (uint32_t star = 0; star < static_cast<uint32_t>(kStarCount); ++star) {
        const float depth = hash01(star * 47u + 3u);
        const float initialX =
            width * (0.025f + hash01(star * 71u + 13u) * 0.95f);
        const float initialY =
            skyHeight * (0.035f + hash01(star * 97u + 31u) * 0.88f);
        const auto positionAt = [&](float secondsFromNow) {
            return rotateFromInitial(initialX, initialY,
                                     starRotation_ + secondsFromNow * angularSpeed);
        };
        const float naturalTwinkle =
            0.72f + 0.26f * std::sin(time_ * (0.48f + depth * 0.82f) +
                                     hash01(star * 19u + 7u) * kPi * 2.0f);
        const float responsePulse =
            starPulses_[star] * responseSetting;
        const float brightness =
            clampUnit(naturalTwinkle + responsePulse * 0.88f);
        const float radius = scale * (0.45f + depth * 0.95f) *
                             (1.0f + responsePulse * 0.38f);
        const float starAlpha = std::clamp(
            (42.0f + brightnessSetting * 178.0f) * brightness +
                responsePulse * 95.0f,
            0.0f, 255.0f);
        constexpr int trailSegments = 5;
        const float trailSeconds = 3.0f + speedSetting * 5.5f;
        Point previous = positionAt(-trailSeconds);
        for (int segment = 0; segment < trailSegments; ++segment) {
            const float progress =
                static_cast<float>(segment + 1) / trailSegments;
            const Point current =
                positionAt(-trailSeconds * (1.0f - progress));
            const float trailAlpha =
                starAlpha * (0.10f + progress * progress * 0.38f);
            output.addLine(previous.x, previous.y, current.x, current.y,
                           color(0xa9caff, static_cast<uint8_t>(trailAlpha)),
                           std::max(0.45f, radius * (0.34f + progress * 0.12f)), true);
            previous = current;
        }
        const Point position = positionAt(0.0f);
        output.addFillEllipse(
            position.x - radius * 2.8f, position.y - radius * 2.8f,
            radius * 5.6f, radius * 5.6f,
            color(0x9fc8ff,
                  static_cast<uint8_t>(starAlpha * (0.10f + responsePulse * 0.08f))));
        const uint32_t tint = depth > 0.78f ? 0xffe4bd : 0xd8e9ff;
        output.addFillEllipse(
            position.x - radius * 0.52f, position.y - radius * 0.52f,
            radius * 1.04f, radius * 1.04f,
            color(tint, static_cast<uint8_t>(starAlpha)));
    }
}

void CampfireEngine::drawMeteor(DrawList& output, float width,
                                float skyHeight) const {
    if (!meteorActive_ || meteorLifetime_ <= 0.0f) return;
    const float progress = clampUnit(meteorAge_ / meteorLifetime_);
    const float visibility =
        std::sin(progress * kPi) * (0.76f + 0.24f * (1.0f - progress));
    const float scale =
        std::clamp(std::min(width, skyHeight) / 760.0f, 0.72f, 1.65f);
    constexpr int segments = 8;
    const float trailSeconds = std::min(meteorAge_, 0.46f);
    Point previous{
        (meteorStartX_ + meteorVelocityX_ * (meteorAge_ - trailSeconds)) * width,
        (meteorStartY_ + meteorVelocityY_ * (meteorAge_ - trailSeconds)) * skyHeight,
    };
    for (int segment = 0; segment < segments; ++segment) {
        const float fraction = static_cast<float>(segment + 1) / segments;
        const float sampleAge = meteorAge_ - trailSeconds * (1.0f - fraction);
        const Point current{
            (meteorStartX_ + meteorVelocityX_ * sampleAge) * width,
            (meteorStartY_ + meteorVelocityY_ * sampleAge) * skyHeight,
        };
        const float alpha =
            visibility * (16.0f + fraction * fraction * 205.0f);
        output.addLine(previous.x, previous.y, current.x, current.y,
                       color(fraction > 0.72f ? 0xfff4cf : 0xb8d4ff,
                             static_cast<uint8_t>(alpha)),
                       scale * (0.55f + fraction * 1.10f), true);
        previous = current;
    }
    const float headRadius = scale * (1.15f + visibility * 0.75f);
    output.addFillEllipse(
        previous.x - headRadius * 2.4f, previous.y - headRadius * 2.4f,
        headRadius * 4.8f, headRadius * 4.8f,
        color(0xc9ddff, static_cast<uint8_t>(visibility * 52.0f)));
    output.addFillEllipse(
        previous.x - headRadius * 0.55f, previous.y - headRadius * 0.55f,
        headRadius * 1.10f, headRadius * 1.10f,
        color(0xfff8dc, static_cast<uint8_t>(visibility * 245.0f)));
}

void CampfireEngine::drawSmoke(DrawList& output, float centerX, float baseY,
                               float flameHeight, float extent) const {
    for (uint32_t puff = 0; puff < 7; ++puff) {
        const float seed = hash01(puff * 37u + 11u);
        const float life = cycle(seed + time_ * (0.035f + hash01(puff + 91u) * 0.018f));
        const float spread = extent * (0.015f + life * 0.12f);
        const float drift = std::sin(time_ * (0.34f + seed * 0.2f) + seed * 19.0f) *
                            extent * (0.025f + life * 0.07f);
        const float x = centerX + drift + (seed - 0.5f) * extent * 0.10f;
        const float y = baseY - flameHeight * (0.58f + life * 0.95f);
        const float diameter = spread * (1.25f + hash01(puff + 3u));
        const float fade = std::sin(life * kPi);
        output.addRadialGradientEllipse(
            x - diameter, y - diameter * 0.55f, diameter * 2.0f, diameter * 1.1f,
            color(0x6e5548, static_cast<uint8_t>(fade * (8.0f + midLevel_ * 5.0f))),
            color(0x221b19, 0));
    }
}

void CampfireEngine::addSilhouetteLog(DrawList& output, float centerX,
                                      float centerY, float length,
                                      float thickness, float angle,
                                      uint32_t seed, Color value) {
    const float axisX = std::cos(angle);
    const float axisY = std::sin(angle);
    const float normalX = -axisY;
    const float normalY = axisX;
    const float halfLength = length * 0.5f;
    const float halfThickness = thickness * 0.5f;
    constexpr int sections = 7;
    scratch_.clear();
    for (int section = 0; section <= sections; ++section) {
        const float unit = static_cast<float>(section) / sections;
        const float along = (-1.0f + unit * 2.0f) * halfLength;
        const float endShape =
            0.72f +
            std::pow(std::max(0.0f, std::sin(unit * kPi)), 0.36f) * 0.28f;
        const float roughness =
            (hash01(seed + static_cast<uint32_t>(section) * 41u) - 0.5f) *
            halfThickness * 0.18f;
        scratch_.push_back(rotatedPoint(
            centerX, centerY, axisX, axisY, normalX, normalY, along,
            -halfThickness * endShape + roughness));
    }
    for (int section = sections; section >= 0; --section) {
        const float unit = static_cast<float>(section) / sections;
        const float along = (-1.0f + unit * 2.0f) * halfLength;
        const float endShape =
            0.72f +
            std::pow(std::max(0.0f, std::sin(unit * kPi)), 0.36f) * 0.28f;
        const float roughness =
            (hash01(seed + 307u + static_cast<uint32_t>(section) * 37u) - 0.5f) *
            halfThickness * 0.18f;
        scratch_.push_back(rotatedPoint(
            centerX, centerY, axisX, axisY, normalX, normalY, along,
            halfThickness * endShape + roughness));
    }
    output.addFillPolygon(output.appendPoints(scratch_), value);
}

void CampfireEngine::addStone(DrawList& output, float centerX, float centerY,
                              float halfWidth, float halfHeight,
                              uint32_t seed, Color value) {
    scratch_.clear();
    constexpr int points = 7;
    const float rotation = (hash01(seed + 17u) - 0.5f) * 0.34f;
    for (int point = 0; point < points; ++point) {
        const float angle = static_cast<float>(point) / points * kPi * 2.0f;
        const float roughness =
            0.84f + hash01(seed + static_cast<uint32_t>(point) * 29u) * 0.19f;
        const float localX = std::cos(angle) * halfWidth * roughness;
        const float localY = std::sin(angle) * halfHeight * roughness;
        scratch_.push_back(
            {centerX + localX * std::cos(rotation) - localY * std::sin(rotation),
             centerY + localX * std::sin(rotation) + localY * std::cos(rotation)});
    }
    output.addFillPolygon(output.appendPoints(scratch_), value);
}

void CampfireEngine::drawLogs(DrawList& output, float centerX, float baseY, float extent) {
    constexpr std::array<float, 5> backX{-0.145f, -0.075f, 0.0f, 0.078f, 0.145f};
    constexpr std::array<float, 5> backY{0.036f, 0.018f, 0.012f, 0.019f, 0.037f};
    for (size_t stone = 0; stone < backX.size(); ++stone) {
        const float width =
            extent * (0.040f + hash01(static_cast<uint32_t>(stone) * 41u + 5u) *
                                   0.008f);
        const float height =
            extent * (0.016f + hash01(static_cast<uint32_t>(stone) * 37u + 11u) *
                                   0.006f);
        addStone(output, centerX + backX[stone] * extent,
                 baseY + backY[stone] * extent, width, height,
                 static_cast<uint32_t>(stone) * 79u + 17u,
                 color(stone == 2 ? 0x0a0806 : 0x070707));
    }

    addSilhouetteLog(output, centerX - extent * 0.010f,
                     baseY + extent * 0.020f, extent * 0.28f,
                     extent * 0.033f, 0.14f, 0x45a13f21u, color(0x020202));
    addSilhouetteLog(output, centerX + extent * 0.006f,
                     baseY + extent * 0.014f, extent * 0.25f,
                     extent * 0.030f, -0.34f, 0x91d83b57u, color(0x010101));
    addSilhouetteLog(output, centerX + extent * 0.004f,
                     baseY + extent * 0.010f, extent * 0.23f,
                     extent * 0.028f, 0.47f, 0xa5739bc1u, color(0x030202));
}

void CampfireEngine::drawForegroundLogs(DrawList& output, float centerX,
                                         float baseY, float extent) {
    addSilhouetteLog(output, centerX + extent * 0.040f,
                     baseY - extent * 0.068f, extent * 0.31f,
                     extent * 0.025f, -1.99f, 0x6ad21583u, color(0x000000));
    addSilhouetteLog(output, centerX - extent * 0.060f,
                     baseY - extent * 0.071f, extent * 0.34f,
                     extent * 0.027f, -1.16f, 0x18d94731u, color(0x000000));

    for (uint32_t coal = 0; coal < 7; ++coal) {
        const float phase =
            cycle(hash01(coal * 43u + 5u) + time_ * (0.022f + coal * 0.0012f));
        const float wander =
            (smoothNoise(time_ * 0.35f + coal, coal * 97u + 3u) - 0.5f) *
            extent * 0.018f;
        const float x =
            centerX + (hash01(coal * 71u + 13u) - 0.5f) * extent * 0.20f + wander;
        const float y =
            baseY + extent * (0.004f + hash01(coal * 31u + 19u) * 0.020f);
        const float size =
            extent * (0.0045f + hash01(coal * 53u + 7u) * 0.0055f);
        const float pulse =
            std::pow(std::max(0.0f, std::sin(phase * kPi)), 1.4f) *
            (0.24f + fireScale_ * 0.76f);
        output.addRadialGradientEllipse(
            x - size * 1.7f, y - size, size * 3.4f, size * 2.0f,
            color(0xff9a21, static_cast<uint8_t>(pulse * 105.0f)),
            color(0x361004, 0));
        output.addFillEllipse(
            x - size * 0.45f, y - size * 0.30f, size * 0.90f, size * 0.60f,
            color(0xffd15a, static_cast<uint8_t>(pulse * 180.0f)));
    }

    constexpr std::array<float, 5> frontX{-0.142f, -0.073f, 0.0f, 0.074f, 0.142f};
    constexpr std::array<float, 5> frontY{0.068f, 0.083f, 0.090f, 0.082f, 0.067f};
    for (size_t stone = 0; stone < frontX.size(); ++stone) {
        const float width =
            extent * (0.046f + hash01(static_cast<uint32_t>(stone) * 47u + 29u) *
                                   0.007f);
        const float height =
            extent * (0.021f + hash01(static_cast<uint32_t>(stone) * 61u + 37u) *
                                   0.006f);
        addStone(output, centerX + frontX[stone] * extent,
                 baseY + frontY[stone] * extent, width, height,
                 static_cast<uint32_t>(stone) * 83u + 41u,
                 color(stone == 2 ? 0x0b0907 : 0x070707));
    }
}

void CampfireEngine::addFlameTongue(DrawList& output, float centerX, float baseY,
                                    float height, float halfWidth, float seed,
                                    Color value) {
    constexpr int segments = 18;
    const auto seedKey = static_cast<uint32_t>(seed * 8191.0f) + 0x9e3779b9u;
    const float individualFlicker =
        0.94f + smoothNoise(time_ * 2.8f + seed * 3.1f, seedKey + 31u) * 0.09f;
    const float leftBase =
        baseY + height * (hash01(seedKey + 1481u) - 0.48f) * 0.030f;
    const float rightBase =
        baseY + height * (hash01(seedKey + 1597u) - 0.48f) * 0.030f;
    const float turbulentResponse = 1.0f + midLevel_ * 0.20f + stereoLevel_ * 0.08f;
    const auto centerOffset = [&](float unit) {
        const float anchor = unit * unit * (3.0f - 2.0f * unit);
        const float risingEddy =
            smoothNoise(unit * 3.4f - time_ * 2.9f, seedKey + 101u) - 0.5f;
        const float fineEddy =
            smoothNoise(unit * 7.7f - time_ * 5.8f, seedKey + 307u) - 0.5f;
        return halfWidth * anchor * turbulentResponse *
               (risingEddy * 0.72f + fineEddy * 0.23f);
    };
    const auto edgeWidth = [&](float unit, uint32_t sideSeed) {
        const float broad =
            smoothNoise(unit * 5.2f - time_ * 4.1f, seedKey + sideSeed) - 0.5f;
        const float detail =
            smoothNoise(unit * 10.9f - time_ * 7.4f, seedKey + sideSeed + 401u) - 0.5f;
        return 0.86f + broad * 0.30f + detail * 0.11f;
    };
    scratch_.clear();
    for (int segment = 0; segment <= segments; ++segment) {
        const float unit = static_cast<float>(segment) / segments;
        const float taper = std::pow(std::max(0.0f, 1.0f - unit), 0.50f);
        scratch_.push_back(
            {centerX + centerOffset(unit) -
                 halfWidth * taper * edgeWidth(unit, 719u),
             leftBase -
                 height * individualFlicker * unit * (0.86f + unit * 0.14f)});
    }
    for (int segment = segments - 1; segment >= 0; --segment) {
        const float unit = static_cast<float>(segment) / segments;
        const float taper = std::pow(std::max(0.0f, 1.0f - unit), 0.50f);
        scratch_.push_back(
            {centerX + centerOffset(unit) +
                 halfWidth * taper * edgeWidth(unit, 1297u),
             rightBase -
                 height * individualFlicker * unit * (0.86f + unit * 0.14f)});
    }
    scratch_.push_back(
        {centerX + halfWidth * 0.34f,
         baseY + height * (hash01(seedKey + 1699u) - 0.32f) * 0.026f});
    scratch_.push_back(
        {centerX - halfWidth * 0.28f,
         baseY + height * (hash01(seedKey + 1789u) - 0.32f) * 0.026f});
    output.addFillPolygon(output.appendPoints(scratch_), value);
}

void CampfireEngine::drawFlames(DrawList& output, float centerX, float baseY,
                                float flameHeight, float extent) {
    const float naturalPulse =
        0.96f + smoothNoise(time_ * 2.15f, 0x4a39b70du) * 0.06f;
    const float responseScale =
        static_cast<float>(options_.flameResponse) / 80.0f;
    const float audioHeight =
        1.0f + (lowLevel_ * 0.12f + midLevel_ * 0.04f + beatLevel_ * 0.22f) *
                   responseScale;
    const float height =
        flameHeight * naturalPulse * audioHeight * fireScale_;
    const float halfWidth =
        extent * 0.115f * (0.35f + fireScale_ * 0.65f);

    output.addRadialGradientEllipse(centerX - halfWidth * 1.85f,
                                    baseY - height * 0.70f,
                                    halfWidth * 3.70f, height * 1.18f,
                                    color(0xff8a12, 72), color(0x742104, 0));
    output.addRadialGradientEllipse(centerX - halfWidth * 1.30f,
                                    baseY - height * 0.48f,
                                    halfWidth * 2.60f, height * 0.72f,
                                    color(0xffc43d, 92), color(0x9a3006, 0));

    addFlameTongue(output, centerX - halfWidth * 0.10f, baseY,
                   height, halfWidth * 0.88f, 0.7f, color(0xf05a08, 175));
    addFlameTongue(output, centerX - halfWidth * 0.72f, baseY,
                   height * 0.70f, halfWidth * 0.54f, 2.3f, color(0xf66b09, 170));
    addFlameTongue(output, centerX + halfWidth * 0.73f, baseY,
                   height * 0.76f, halfWidth * 0.52f, 4.8f, color(0xf8740b, 175));
    addFlameTongue(output, centerX - halfWidth * 1.02f, baseY,
                   height * 0.49f, halfWidth * 0.34f, 6.6f, color(0xe95107, 145));
    addFlameTongue(output, centerX + halfWidth * 1.04f, baseY,
                   height * 0.56f, halfWidth * 0.35f, 8.1f, color(0xf06008, 150));

    addFlameTongue(output, centerX - halfWidth * 0.18f, baseY,
                   height * 0.84f, halfWidth * 0.64f, 1.4f, color(0xff8c12, 220));
    addFlameTongue(output, centerX + halfWidth * 0.43f, baseY,
                   height * 0.63f, halfWidth * 0.43f, 5.9f, color(0xff9d18, 220));
    addFlameTongue(output, centerX - halfWidth * 0.58f, baseY,
                   height * 0.53f, halfWidth * 0.35f, 7.2f, color(0xff9414, 212));
    addFlameTongue(output, centerX + halfWidth * 0.08f, baseY,
                   height * 0.48f, halfWidth * 0.48f, 10.4f, color(0xffb027, 220));

    addFlameTongue(output, centerX + halfWidth * 0.02f, baseY,
                   height * 0.57f, halfWidth * 0.39f, 3.1f, color(0xffcd48, 235));
    addFlameTongue(output, centerX - halfWidth * 0.33f, baseY,
                   height * 0.39f, halfWidth * 0.27f, 8.4f, color(0xffdf68, 240));
    addFlameTongue(output, centerX + halfWidth * 0.35f, baseY,
                   height * 0.34f, halfWidth * 0.24f, 11.8f, color(0xffe477, 240));
    addFlameTongue(output, centerX + halfWidth * 0.06f, baseY,
                   height * 0.25f, halfWidth * 0.21f, 9.7f, color(0xfff3b0, 245));
}

void CampfireEngine::drawEmbers(DrawList& output, float centerX, float baseY,
                                float flameHeight, float extent) const {
    const float intensityScale =
        0.25f + static_cast<float>(options_.particleIntensity) / 100.0f * 1.50f;
    for (const auto& ember : embers_) {
        if (!ember.active || ember.lifetime <= 0.0f) continue;
        const float life = clampUnit(ember.age / ember.lifetime);
        const float ignition = std::min(1.0f, life * 10.0f);
        const float fade = std::pow(std::max(0.0f, 1.0f - life), 0.62f);
        const float wave =
            std::sin(ember.phase + life * 8.0f) * 0.018f * life;
        const float horizontal = ember.origin + ember.drift * life + wave;
        const float x = centerX + horizontal * extent;
        const float y = baseY - flameHeight * (0.10f + ember.lift * life);
        const float size = extent * 0.0031f * ember.size *
                           (0.72f + intensityScale * 0.28f) *
                           (1.0f + beatLevel_ * 0.10f);
        const float brightness =
            std::clamp(ignition * fade * (105.0f + ember.heat * 145.0f) *
                           intensityScale,
                       0.0f, 255.0f);
        const float previousX =
            x - (ember.drift + std::cos(ember.phase + life * 8.0f) * 0.018f) *
                    extent * 0.026f;
        const float trailLength =
            size * (2.6f + ember.heat * 2.8f) * (0.72f + intensityScale * 0.28f);
        output.addLine(previousX, y + trailLength, x, y,
                       color(0xff7a17, static_cast<uint8_t>(brightness * 0.32f)),
                       std::max(0.65f, size * 0.38f), true);
        output.addFillEllipse(
            x - size * 1.5f, y - size * 1.9f, size * 3.0f, size * 3.8f,
            color(0xff6d15, static_cast<uint8_t>(brightness * 0.13f)));
        output.addFillEllipse(
            x - size * 0.36f, y - size * 0.78f, size * 0.72f, size * 1.56f,
            color(ember.heat > 0.78f ? 0xfff1ad : 0xffb12c,
                  static_cast<uint8_t>(brightness)));
    }
}

void CampfireEngine::buildFrame(float width, float height, DrawList& output) {
    output.reset();
    if (!std::isfinite(width) || !std::isfinite(height) ||
        width <= 0.0f || height <= 0.0f) {
        return;
    }
    const float extent = std::min(width, height);
    const float centerX = width * 0.5f;
    const float baseY = height * 0.76f;
    // The stone ring spans baseY + 0.012..0.090 * extent (back rim to front rim).
    // Anchor the flames and embers to the fuel bed near the middle of that ring
    // instead of the back rim, so the fire sits in the pit and the front stones
    // still overlap its base.
    const float flameBaseY = baseY + extent * 0.05f;
    const float flameHeight = extent * 0.39f;
    drawBackground(output, width, height, centerX, baseY, extent);
    drawSmoke(output, centerX, flameBaseY, flameHeight * fireScale_, extent);
    drawLogs(output, centerX, baseY, extent);
    drawFlames(output, centerX, flameBaseY, flameHeight, extent);
    drawForegroundLogs(output, centerX, baseY, extent);
    drawEmbers(output, centerX, flameBaseY, flameHeight, extent);
}

CampfireFrameInfo CampfireEngine::frameInfo() const noexcept {
    const float particleActivity =
        clampUnit(activityLevel_ * 0.35f + highLevel_ * 0.45f + beatLevel_ * 0.65f);
    const float responseScale =
        static_cast<float>(options_.flameResponse) / 80.0f;
    return {lowLevel_, midLevel_, highLevel_, stereoLevel_, beatLevel_,
            particleActivity,
            1.0f + (lowLevel_ * 0.12f + midLevel_ * 0.04f + beatLevel_ * 0.22f) *
                       responseScale,
            fireScale_, quietSeconds_,
            meteorActive_ && meteorLifetime_ > 0.0f
                ? std::sin(clampUnit(meteorAge_ / meteorLifetime_) * kPi)
                : 0.0f};
}

} // namespace vizrack::builtin
