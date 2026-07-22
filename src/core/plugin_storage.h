#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace vizrack {

struct PluginStoragePaths {
    std::filesystem::path directory;
    std::filesystem::path location;
    std::filesystem::path state;
};

PluginStoragePaths pluginStoragePaths(const std::filesystem::path& pluginsRoot,
                                      std::string_view pluginId);
std::string loadPluginLocation(const std::filesystem::path& path, std::string& warning);
bool savePluginLocation(const std::filesystem::path& path,
                        const std::filesystem::path& modulePath, std::string& error);

} // namespace vizrack
