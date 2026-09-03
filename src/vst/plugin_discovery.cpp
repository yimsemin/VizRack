#include "vst/plugin_discovery.h"

#include "core/portable_paths.h"
#include "core/utf.h"

#include "public.sdk/source/vst/hosting/module.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"

#include <windows.h>
#include <shlobj.h>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <set>

namespace vizrack {
namespace {

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

bool isAmd64Pe(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return false;
    IMAGE_DOS_HEADER dos{};
    input.read(reinterpret_cast<char*>(&dos), sizeof(dos));
    if (!input || dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew <= 0) return false;
    input.seekg(dos.e_lfanew, std::ios::beg);
    DWORD signature{};
    IMAGE_FILE_HEADER header{};
    input.read(reinterpret_cast<char*>(&signature), sizeof(signature));
    input.read(reinterpret_cast<char*>(&header), sizeof(header));
    return input && signature == IMAGE_NT_SIGNATURE && header.Machine == IMAGE_FILE_MACHINE_AMD64;
}

bool containsBinaryMarker(const std::filesystem::path& path, const std::string& requiredMarker) {
    if (requiredMarker.empty()) return true;
    std::ifstream input(path, std::ios::binary);
    if (!input) return false;
    const std::string marker = lower(requiredMarker);
    std::array<char, 64 * 1024> block{};
    std::string carry;
    while (input) {
        input.read(block.data(), static_cast<std::streamsize>(block.size()));
        std::string text = carry + std::string(block.data(), static_cast<size_t>(input.gcount()));
        text = lower(std::move(text));
        if (text.find(marker) != std::string::npos) return true;
        carry = text.size() >= marker.size() ? text.substr(text.size() - marker.size()) : text;
    }
    return false;
}

std::filesystem::path commonProgramFilesDirectory() {
    wchar_t buffer[MAX_PATH]{};
    const DWORD length = GetEnvironmentVariableW(L"CommonProgramFiles", buffer, MAX_PATH);
    const std::filesystem::path common = length > 0 && length < MAX_PATH
                                             ? std::filesystem::path(std::wstring(buffer, length))
                                             : std::filesystem::path(L"C:\\Program Files\\Common Files");
    return common;
}

std::filesystem::path knownFolder(REFKNOWNFOLDERID id) {
    PWSTR raw = nullptr;
    std::filesystem::path result;
    if (SUCCEEDED(SHGetKnownFolderPath(id, KF_FLAG_DEFAULT, nullptr, &raw)) && raw) {
        result = raw;
    }
    if (raw) CoTaskMemFree(raw);
    return result;
}

} // namespace

std::vector<std::filesystem::path> defaultSearchLocations(
    const PluginDefinition& definition) {
    std::vector<std::filesystem::path> locations;
    std::vector<std::filesystem::path> roots;
    if (auto user = knownFolder(FOLDERID_UserProgramFilesCommon); !user.empty()) {
        roots.push_back(std::move(user));
    }
    auto global = knownFolder(FOLDERID_ProgramFilesCommon);
    roots.push_back(global.empty() ? commonProgramFilesDirectory() : std::move(global));
    try {
        roots.push_back(PortablePaths::discover().root);
    } catch (...) {
    }

    locations.reserve(roots.size() * definition.searchLocations.size());
    for (const auto& root : roots) {
        for (const auto& relative : definition.searchLocations) {
            const auto candidate = root / relative;
            if (std::find(locations.begin(), locations.end(), candidate) == locations.end()) {
                locations.push_back(candidate);
            }
        }
    }
    return locations;
}

PluginInspection inspectSupportedPlugin(const PluginDefinition& definition,
                                        const std::filesystem::path& modulePath) {
    PluginInspection result;
    if (!isVst3CandidatePath(modulePath)) {
        result.error = "The chosen path is not an existing .vst3 module or bundle.";
        return result;
    }
    const auto binary = resolveVst3Binary(modulePath);
    if (binary.empty() || !isAmd64Pe(binary)) {
        result.error = "The chosen VST3 has no x64 Windows binary.";
        return result;
    }
    if (!containsBinaryMarker(binary, definition.binaryMarker)) {
        result.error = "Could not find the required binary marker '" + definition.binaryMarker +
                       "' for " + definition.displayName + ".";
        return result;
    }

    std::string moduleError;
    auto module = VST3::Hosting::Module::create(toUtf8(modulePath.wstring()), moduleError);
    if (!module) {
        result.error = "VST3 module load failed: " + moduleError;
        return result;
    }
    const auto factory = module->getFactory();
    const auto factoryVendor = lower(factory.info().vendor());
    const auto classNeedle = lower(definition.classNameContains);
    const auto vendorNeedle = lower(definition.vendorContains);
    for (const auto& info : factory.classInfos()) {
        if (info.category() != kVstAudioEffectClass) continue;
        const auto name = lower(info.name());
        const auto vendor = lower(info.vendor().empty() ? factory.info().vendor() : info.vendor());
        if (name.find(classNeedle) == std::string::npos ||
            (vendor.find(vendorNeedle) == std::string::npos &&
             factoryVendor.find(vendorNeedle) == std::string::npos)) {
            continue;
        }
        result.descriptor.definitionId = definition.id;
        result.descriptor.modulePath = std::filesystem::absolute(modulePath).lexically_normal();
        std::copy_n(info.ID().data(), result.descriptor.classId.size(), result.descriptor.classId.begin());
        result.descriptor.name = info.name();
        result.descriptor.vendor = info.vendor().empty() ? factory.info().vendor() : info.vendor();
        result.descriptor.version = info.version();
        result.compatible = true;
        return result;
    }
    result.error = "The VST3 factory has no whitelisted " + definition.displayName +
                   " audio effect class.";
    return result;
}

PluginInspection findSupportedPlugin(const PluginDefinition& definition,
                                     const std::string& savedUtf8Path) {
    if (!savedUtf8Path.empty()) {
        try {
            auto saved = inspectSupportedPlugin(definition,
                                                std::filesystem::path(fromUtf8(savedUtf8Path)));
            if (saved.compatible) return saved;
        } catch (...) {
        }
    }
    PluginInspection last;
    last.error = "Could not find a compatible " + definition.displayName + " VST3.";
    for (const auto& location : defaultSearchLocations(definition)) {
        std::vector<std::filesystem::path> candidates;
        if (isVst3CandidatePath(location)) {
            candidates.push_back(location);
        } else {
            candidates = discoverVst3Candidates(location);
        }
        for (const auto& candidate : candidates) {
            auto inspected = inspectSupportedPlugin(definition, candidate);
            if (inspected.compatible) return inspected;
            last = std::move(inspected);
        }
    }
    return last;
}

} // namespace vizrack
