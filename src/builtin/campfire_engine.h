#pragma once

#include "builtin/draw_list.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace vizrack {

struct CampfireOptions {
    int flameResponse{20};
    int starSpeed{60};
    int starBrightness{60};
    int starResponse{40};
    int particleAmount{60};
    int particleIntensity{50};
};

namespace builtin {

struct CampfireFrameInfo {
    float lowLevel{};
    float midLevel{};
    float highLevel{};
    float stereoLevel{};
    float beatLevel{};
    float particleActivity{};
    float intensity{1.0f};
    float fireScale{1.0f};
    float quietSeconds{};
    float meteorActivity{};
};

class CampfireEngine {
public:
    static constexpr size_t kMaxSamples = 4096;
    static constexpr size_t kMaxEmbers = 48;
    static constexpr size_t kStarCount = 48;

    CampfireEngine();

    void setOptions(CampfireOptions options) noexcept;
    CampfireOptions options() const noexcept { return options_; }
    void setSampleRate(uint32_t sampleRate) noexcept;
    std::span<float> inputLeft() noexcept { return left_; }
    std::span<float> inputRight() noexcept { return right_; }
    void update(size_t sampleCount, float frameSeconds = 1.0f / 60.0f) noexcept;
    void reset() noexcept;

    void buildFrame(float width, float height, DrawList& output);
    CampfireFrameInfo frameInfo() const noexcept;

private:
    struct EmberParticle {
        float age{};
        float lifetime{1.0f};
        float origin{};
        float drift{};
        float lift{1.0f};
        float size{1.0f};
        float phase{};
        float heat{1.0f};
        bool active{};
    };

    void analyzeSamples(float frameScale) noexcept;
    void updateEmbers(float frameSeconds, float beatRise) noexcept;
    void updateStarPulses(float frameSeconds, float beatRise) noexcept;
    void updateMeteor(float frameSeconds) noexcept;
    void spawnEmber() noexcept;
    float nextRandom() noexcept;
    void drawBackground(DrawList& output, float width, float height,
                        float centerX, float baseY, float extent) const;
    void drawStars(DrawList& output, float width, float height, float baseY) const;
    void drawMeteor(DrawList& output, float width, float skyHeight) const;
    void drawSmoke(DrawList& output, float centerX, float baseY,
                   float flameHeight, float extent) const;
    void drawLogs(DrawList& output, float centerX, float baseY, float extent);
    void drawForegroundLogs(DrawList& output, float centerX, float baseY,
                            float extent);
    void drawFlames(DrawList& output, float centerX, float baseY,
                    float flameHeight, float extent);
    void drawEmbers(DrawList& output, float centerX, float baseY,
                    float flameHeight, float extent) const;
    void addFlameTongue(DrawList& output, float centerX, float baseY,
                        float height, float halfWidth, float seed, Color value);
    void addSilhouetteLog(DrawList& output, float centerX, float centerY,
                          float length, float thickness, float angle,
                          uint32_t seed, Color value);
    void addStone(DrawList& output, float centerX, float centerY,
                  float halfWidth, float halfHeight, uint32_t seed, Color value);

    std::array<float, kMaxSamples> left_{};
    std::array<float, kMaxSamples> right_{};
    size_t sampleCount_{};
    std::atomic<uint32_t> sampleRate_{48000};
    float lowFilter_{};
    float midFilter_{};
    float lowLevel_{};
    float midLevel_{};
    float highLevel_{};
    float stereoLevel_{};
    float lowAverage_{};
    float beatLevel_{};
    float activityLevel_{};
    float signalLevel_{};
    float emberSpawnAccumulator_{};
    float quietSeconds_{};
    float fireScale_{1.0f};
    float time_{};
    float starRotation_{};
    float meteorCountdown_{52.0f};
    float meteorAge_{};
    float meteorLifetime_{1.0f};
    float meteorStartX_{};
    float meteorStartY_{};
    float meteorVelocityX_{};
    float meteorVelocityY_{};
    bool meteorActive_{};
    uint32_t randomState_{0x71e3a95du};
    uint32_t beatSerial_{};
    uint32_t meteorSerial_{};
    std::array<EmberParticle, kMaxEmbers> embers_{};
    std::array<float, kStarCount> starPulses_{};
    std::array<float, kStarCount> starPulseDelays_{};
    std::array<float, kStarCount> starPendingPulses_{};
    std::vector<Point> scratch_;
    CampfireOptions options_;
};

} // namespace builtin
} // namespace vizrack
