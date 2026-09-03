#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace vizrack {

enum class PluginKind { builtIn, vst3 };

struct PluginDefinition {
    PluginKind kind{PluginKind::vst3};
    std::string id;
    std::string displayName;
    std::string vendorName;
    std::string classNameContains;
    std::string vendorContains;
    std::string binaryMarker;
    std::string editionLabel;
    std::vector<std::filesystem::path> searchLocations;
    std::string installUrl;
    std::string inspiration; // "Inspired by ..." credit for homage built-ins; empty otherwise
};

const std::vector<PluginDefinition>& pluginCatalog();
const PluginDefinition* findPluginDefinition(std::string_view id);

} // namespace vizrack
