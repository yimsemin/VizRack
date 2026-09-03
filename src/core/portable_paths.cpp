#include "core/portable_paths.h"

#include "core/utf.h"

#include <windows.h>

#include <fstream>
#include <system_error>
#include <vector>

namespace vizrack {

PortablePaths PortablePaths::discover() {
    std::vector<wchar_t> buffer(512);
    for (;;) {
        const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0) {
            throw std::runtime_error("GetModuleFileNameW failed: " + formatWindowsError(GetLastError()));
        }
        if (length < buffer.size() - 1) {
            return fromExecutable(std::filesystem::path(std::wstring(buffer.data(), length)));
        }
        buffer.resize(buffer.size() * 2);
    }
}

PortablePaths PortablePaths::fromExecutable(const std::filesystem::path& executablePath) {
    PortablePaths paths;
    paths.executable = std::filesystem::absolute(executablePath).lexically_normal();
    paths.root = paths.executable.parent_path();
    paths.data = paths.root / L"data";
    paths.logs = paths.data / L"logs";
    paths.plugins = paths.data / L"plugins";
    paths.settings = paths.data / L"settings.json";
    return paths;
}

void PortablePaths::prepare() {
    writable = false;
    writeError.clear();
    std::error_code error;
    std::filesystem::create_directories(logs, error);
    if (error) {
        writeError = "Could not create the portable data folder: " + error.message();
        return;
    }

    const auto probe = data / L".write-probe.tmp";
    {
        std::ofstream stream(probe, std::ios::binary | std::ios::trunc);
        if (!stream) {
            writeError = "The executable folder is not writable. Settings and logs will not be saved.";
            return;
        }
        stream << "probe";
        if (!stream.good()) {
            writeError = "The executable folder write probe failed.";
            return;
        }
    }
    std::filesystem::remove(probe, error);
    writable = true;
}

} // namespace vizrack
