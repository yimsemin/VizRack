#pragma once

#include <filesystem>
#include <string>

namespace vizrack {

struct PortablePaths {
    std::filesystem::path executable;
    std::filesystem::path root;
    std::filesystem::path data;
    std::filesystem::path logs;
    std::filesystem::path plugins;
    std::filesystem::path settings;
    bool writable{false};
    std::string writeError;

    static PortablePaths discover();
    static PortablePaths fromExecutable(const std::filesystem::path& executablePath);
    void prepare();
};

} // namespace vizrack
