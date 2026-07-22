#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace vizrack {

struct PluginStateData {
    std::array<uint8_t, 16> classId{};
    std::vector<uint8_t> component;
    std::vector<uint8_t> controller;
};

std::vector<uint8_t> encodePluginState(const PluginStateData& state);
bool decodePluginState(std::span<const uint8_t> bytes, PluginStateData& state, std::string& error);
bool savePluginStateFile(const std::filesystem::path& path, const PluginStateData& state,
                         std::string& error);
bool loadPluginStateFile(const std::filesystem::path& path, PluginStateData& state,
                         std::string& error);
void quarantineCorruptState(const std::filesystem::path& path);

} // namespace vizrack

