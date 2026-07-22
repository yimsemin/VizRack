#include "core/plugin_paths.h"

#include <algorithm>
#include <cctype>
#include <string>

namespace vizrack {
namespace {

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

} // namespace

std::filesystem::path resolveVst3Binary(const std::filesystem::path& modulePath) {
    std::error_code error;
    if (std::filesystem::is_regular_file(modulePath, error) &&
        lower(modulePath.extension().string()) == ".vst3") {
        return modulePath;
    }
    if (!std::filesystem::is_directory(modulePath, error) ||
        lower(modulePath.extension().string()) != ".vst3") {
        return {};
    }
    const auto architectureDirectory = modulePath / L"Contents" / L"x86_64-win";
    if (!std::filesystem::is_directory(architectureDirectory, error)) return {};
    for (const auto& entry : std::filesystem::directory_iterator(architectureDirectory, error)) {
        if (entry.is_regular_file() && lower(entry.path().extension().string()) == ".vst3") {
            return entry.path();
        }
    }
    return {};
}

bool isVst3CandidatePath(const std::filesystem::path& path) {
    std::error_code error;
    return lower(path.extension().string()) == ".vst3" &&
           (std::filesystem::is_regular_file(path, error) ||
            std::filesystem::is_directory(path, error));
}

std::vector<std::filesystem::path> discoverVst3Candidates(const std::filesystem::path& directory) {
    std::vector<std::filesystem::path> result;
    std::error_code error;
    if (!std::filesystem::is_directory(directory, error)) return result;
    std::filesystem::recursive_directory_iterator iterator(
        directory, std::filesystem::directory_options::skip_permission_denied, error);
    const std::filesystem::recursive_directory_iterator end;
    while (iterator != end) {
        if (error) {
            error.clear();
            iterator.increment(error);
            continue;
        }
        const auto path = iterator->path();
        if (isVst3CandidatePath(path)) {
            result.push_back(path);
            if (iterator->is_directory()) iterator.disable_recursion_pending();
        }
        iterator.increment(error);
    }
    std::sort(result.begin(), result.end());
    result.erase(std::unique(result.begin(), result.end()), result.end());
    return result;
}

} // namespace vizrack

