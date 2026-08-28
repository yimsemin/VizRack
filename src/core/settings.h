#pragma once

#include <filesystem>
#include <string>

namespace vizrack {

struct Settings {
    bool followDefaultDevice{true};
    std::string fixedDeviceId;
    std::string selectedPluginId;
    int windowX{100};
    int windowY{100};
    int windowWidth{640};
    int windowHeight{480};
    bool alwaysOnTop{false};
    bool borderless{false};
    int opacityPercent{100};
    int oscilloscopeFps{60};
    int oscilloscopeScalePercent{70};
    int oscilloscopeSmoothing{1};
    bool oscilloscopeHistoryMode{false};
    int artScene{0};
    int artPalette{0};
    int campfireFlameResponse{20};
    int campfireStarSpeed{60};
    int campfireStarBrightness{60};
    int campfireStarResponse{40};
    int campfireParticleAmount{60};
    int campfireParticleIntensity{50};
};

struct SettingsLoadResult {
    Settings value;
    bool loaded{false};
    std::string warning;
};

SettingsLoadResult loadSettings(const std::filesystem::path& path);
bool saveSettings(const std::filesystem::path& path, const Settings& settings, std::string& error);

} // namespace vizrack
