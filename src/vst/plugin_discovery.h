#pragma once

#include "core/plugin_paths.h"
#include "vst/plugin_catalog.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace vizrack {

struct PluginDescriptor {
    std::string definitionId;
    std::filesystem::path modulePath;
    std::array<uint8_t, 16> classId{};
    std::string name;
    std::string vendor;
    std::string version;
};

struct PluginInspection {
    bool compatible{false};
    PluginDescriptor descriptor;
    std::string error;
};

std::vector<std::filesystem::path> defaultSearchLocations(const PluginDefinition& definition);
PluginInspection inspectSupportedPlugin(const PluginDefinition& definition,
                                        const std::filesystem::path& modulePath);
PluginInspection findSupportedPlugin(const PluginDefinition& definition,
                                     const std::string& savedUtf8Path);

} // namespace vizrack
