#include "core/plugin_storage.h"

#include "core/atomic_file.h"
#include "core/utf.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <span>
#include <stdexcept>

namespace vizrack {

PluginStoragePaths pluginStoragePaths(const std::filesystem::path& pluginsRoot,
                                      std::string_view pluginId) {
    if (pluginId.empty() || !std::all_of(pluginId.begin(), pluginId.end(), [](unsigned char c) {
            return std::isalnum(c) || c == '-' || c == '_';
        })) {
        throw std::invalid_argument("Invalid plug-in whitelist ID");
    }
    const auto directory = pluginsRoot / std::filesystem::path(std::string(pluginId));
    return {directory, directory / L"location.txt", directory / L"plugin-state.bin"};
}

std::string loadPluginLocation(const std::filesystem::path& path, std::string& warning) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return {};
    std::string value((std::istreambuf_iterator<char>(input)), {});
    if ((!input.good() && !input.eof()) || value.size() > 32768) {
        warning = "Could not read the plug-in location file: " + toUtf8(path.wstring());
        return {};
    }
    while (!value.empty() && (value.back() == '\r' || value.back() == '\n')) value.pop_back();
    return value;
}

bool savePluginLocation(const std::filesystem::path& path,
                        const std::filesystem::path& modulePath, std::string& error) {
    const std::string value = toUtf8(modulePath.wstring()) + "\n";
    const auto bytes = std::as_bytes(std::span(value.data(), value.size()));
    return writeFileAtomically(
        path, {reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size()},
        "plug-in location file", error);
}

} // namespace vizrack
