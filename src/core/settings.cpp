#include "core/settings.h"
#include "core/atomic_file.h"

#include <algorithm>
#include <charconv>
#include <fstream>
#include <optional>
#include <span>
#include <sstream>

namespace vizrack {
namespace {

constexpr int kSettingsSchemaVersion = 1;

std::string escapeJson(const std::string& value) {
    std::string result;
    result.reserve(value.size() + 8);
    for (const unsigned char c : value) {
        switch (c) {
            case '\\': result += "\\\\"; break;
            case '"': result += "\\\""; break;
            case '\b': result += "\\b"; break;
            case '\f': result += "\\f"; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            default:
                if (c < 0x20) {
                    constexpr char hex[] = "0123456789abcdef";
                    result += "\\u00";
                    result += hex[(c >> 4) & 0xf];
                    result += hex[c & 0xf];
                } else {
                    result += static_cast<char>(c);
                }
        }
    }
    return result;
}

std::optional<size_t> valueStart(const std::string& json, const std::string& key) {
    const std::string needle = "\"" + key + "\"";
    size_t position = json.find(needle);
    if (position == std::string::npos) return std::nullopt;
    position = json.find(':', position + needle.size());
    if (position == std::string::npos) return std::nullopt;
    ++position;
    while (position < json.size() && (json[position] == ' ' || json[position] == '\t' ||
                                      json[position] == '\r' || json[position] == '\n')) {
        ++position;
    }
    return position;
}

std::optional<std::string> readString(const std::string& json, const std::string& key) {
    auto start = valueStart(json, key);
    if (!start || *start >= json.size() || json[*start] != '"') return std::nullopt;
    std::string result;
    for (size_t i = *start + 1; i < json.size(); ++i) {
        const char c = json[i];
        if (c == '"') return result;
        if (c != '\\') {
            result += c;
            continue;
        }
        if (++i >= json.size()) return std::nullopt;
        switch (json[i]) {
            case '"': result += '"'; break;
            case '\\': result += '\\'; break;
            case '/': result += '/'; break;
            case 'b': result += '\b'; break;
            case 'f': result += '\f'; break;
            case 'n': result += '\n'; break;
            case 'r': result += '\r'; break;
            case 't': result += '\t'; break;
            default: return std::nullopt;
        }
    }
    return std::nullopt;
}

std::optional<bool> readBool(const std::string& json, const std::string& key) {
    auto start = valueStart(json, key);
    if (!start) return std::nullopt;
    if (json.compare(*start, 4, "true") == 0) return true;
    if (json.compare(*start, 5, "false") == 0) return false;
    return std::nullopt;
}

std::optional<int> readInt(const std::string& json, const std::string& key) {
    auto start = valueStart(json, key);
    if (!start) return std::nullopt;
    const char* begin = json.data() + *start;
    const char* end = json.data() + json.size();
    int value{};
    auto result = std::from_chars(begin, end, value);
    if (result.ec != std::errc{}) return std::nullopt;
    return value;
}

} // namespace

SettingsLoadResult loadSettings(const std::filesystem::path& path) {
    SettingsLoadResult result;
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return result;
    }
    std::ostringstream contents;
    contents << input.rdbuf();
    if (!input.good() && !input.eof()) {
        result.warning = "설정 파일을 읽지 못해 기본값을 사용합니다.";
        return result;
    }
    const std::string json = contents.str();
    if (json.find('{') == std::string::npos || json.find('}') == std::string::npos) {
        result.warning = "설정 파일 형식이 손상되어 기본값을 사용합니다.";
        return result;
    }
    const auto schemaVersion = readInt(json, "schemaVersion");
    if (!schemaVersion || *schemaVersion != kSettingsSchemaVersion) {
        result.warning = "지원하지 않는 설정 파일 버전이므로 기본값을 사용합니다.";
        return result;
    }
    if (auto value = readBool(json, "followDefaultDevice")) result.value.followDefaultDevice = *value;
    if (auto value = readString(json, "fixedDeviceId")) result.value.fixedDeviceId = *value;
    if (auto value = readString(json, "selectedPluginId"); value && !value->empty()) {
        result.value.selectedPluginId = *value;
    }
    if (auto value = readInt(json, "windowX")) result.value.windowX = *value;
    if (auto value = readInt(json, "windowY")) result.value.windowY = *value;
    if (auto value = readInt(json, "windowWidth")) result.value.windowWidth = std::max(320, *value);
    if (auto value = readInt(json, "windowHeight")) result.value.windowHeight = std::max(240, *value);
    if (auto value = readBool(json, "alwaysOnTop")) result.value.alwaysOnTop = *value;
    if (auto value = readBool(json, "borderless")) result.value.borderless = *value;
    if (auto value = readInt(json, "opacityPercent")) {
        result.value.opacityPercent = std::clamp(*value, 25, 100);
    }
    if (auto value = readInt(json, "oscilloscopeFps");
        value && (*value == 15 || *value == 30 || *value == 60)) {
        result.value.oscilloscopeFps = *value;
    }
    if (auto value = readInt(json, "oscilloscopeScalePercent");
        value && (*value == 50 || *value == 70 || *value == 100)) {
        result.value.oscilloscopeScalePercent = *value;
    }
    if (auto value = readInt(json, "oscilloscopeSmoothing")) {
        result.value.oscilloscopeSmoothing = std::clamp(*value, 0, 2);
    }
    if (auto value = readBool(json, "oscilloscopeHistoryMode")) {
        result.value.oscilloscopeHistoryMode = *value;
    }
    if (auto value = readInt(json, "artScene")) result.value.artScene = std::clamp(*value, 0, 5);
    if (auto value = readInt(json, "artPalette")) result.value.artPalette = std::clamp(*value, 0, 5);
    if (auto value = readInt(json, "campfireFlameResponse")) {
        result.value.campfireFlameResponse = std::clamp(*value, 0, 100);
    }
    if (auto value = readInt(json, "campfireStarSpeed")) {
        result.value.campfireStarSpeed = std::clamp(*value, 0, 100);
    }
    if (auto value = readInt(json, "campfireStarBrightness")) {
        result.value.campfireStarBrightness = std::clamp(*value, 0, 100);
    }
    if (auto value = readInt(json, "campfireStarResponse")) {
        result.value.campfireStarResponse = std::clamp(*value, 0, 100);
    }
    if (auto value = readInt(json, "campfireParticleAmount")) {
        result.value.campfireParticleAmount = std::clamp(*value, 0, 100);
    }
    if (auto value = readInt(json, "campfireParticleIntensity")) {
        result.value.campfireParticleIntensity = std::clamp(*value, 0, 100);
    }
    if (auto value = readInt(json, "spectrum3dPalette")) {
        result.value.spectrum3dPalette = std::clamp(*value, 0, 5);
    }
    if (auto value = readInt(json, "spectrum3dRotation")) {
        result.value.spectrum3dRotation = std::clamp(*value, 0, 100);
    }
    if (auto value = readInt(json, "spectrum3dTilt")) {
        result.value.spectrum3dTilt = std::clamp(*value, 0, 100);
    }
    if (auto value = readInt(json, "spectrum3dDepth")) {
        result.value.spectrum3dDepth = std::clamp(*value, 0, 100);
    }
    if (auto value = readInt(json, "spectrum3dHeight")) {
        result.value.spectrum3dHeight = std::clamp(*value, 0, 100);
    }
    if (auto value = readInt(json, "joyDivisionPalette")) {
        result.value.joyDivisionPalette = std::clamp(*value, 0, 5);
    }
    if (auto value = readInt(json, "joyDivisionRotation")) {
        result.value.joyDivisionRotation = std::clamp(*value, 0, 100);
    }
    if (auto value = readInt(json, "joyDivisionTilt")) {
        result.value.joyDivisionTilt = std::clamp(*value, 0, 100);
    }
    if (auto value = readInt(json, "joyDivisionDepth")) {
        result.value.joyDivisionDepth = std::clamp(*value, 0, 100);
    }
    if (auto value = readInt(json, "joyDivisionHeight")) {
        result.value.joyDivisionHeight = std::clamp(*value, 0, 100);
    }
    result.loaded = true;
    return result;
}

bool saveSettings(const std::filesystem::path& path, const Settings& settings, std::string& error) {
    std::ostringstream output;
    output << "{\n"
           << "  \"schemaVersion\": " << kSettingsSchemaVersion << ",\n"
           << "  \"followDefaultDevice\": " << (settings.followDefaultDevice ? "true" : "false") << ",\n"
           << "  \"fixedDeviceId\": \"" << escapeJson(settings.fixedDeviceId) << "\",\n"
           << "  \"selectedPluginId\": \"" << escapeJson(settings.selectedPluginId) << "\",\n"
           << "  \"windowX\": " << settings.windowX << ",\n"
           << "  \"windowY\": " << settings.windowY << ",\n"
           << "  \"windowWidth\": " << settings.windowWidth << ",\n"
           << "  \"windowHeight\": " << settings.windowHeight << ",\n"
           << "  \"alwaysOnTop\": " << (settings.alwaysOnTop ? "true" : "false") << ",\n"
           << "  \"borderless\": " << (settings.borderless ? "true" : "false") << ",\n"
           << "  \"opacityPercent\": " << std::clamp(settings.opacityPercent, 25, 100) << ",\n"
           << "  \"oscilloscopeFps\": " << settings.oscilloscopeFps << ",\n"
           << "  \"oscilloscopeScalePercent\": " << settings.oscilloscopeScalePercent << ",\n"
           << "  \"oscilloscopeSmoothing\": " << std::clamp(settings.oscilloscopeSmoothing, 0, 2) << ",\n"
           << "  \"oscilloscopeHistoryMode\": "
           << (settings.oscilloscopeHistoryMode ? "true" : "false") << ",\n"
           << "  \"artScene\": " << std::clamp(settings.artScene, 0, 5) << ",\n"
           << "  \"artPalette\": " << std::clamp(settings.artPalette, 0, 5) << ",\n"
           << "  \"campfireFlameResponse\": "
           << std::clamp(settings.campfireFlameResponse, 0, 100) << ",\n"
           << "  \"campfireStarSpeed\": "
           << std::clamp(settings.campfireStarSpeed, 0, 100) << ",\n"
           << "  \"campfireStarBrightness\": "
           << std::clamp(settings.campfireStarBrightness, 0, 100) << ",\n"
           << "  \"campfireStarResponse\": "
           << std::clamp(settings.campfireStarResponse, 0, 100) << ",\n"
           << "  \"campfireParticleAmount\": "
           << std::clamp(settings.campfireParticleAmount, 0, 100) << ",\n"
           << "  \"campfireParticleIntensity\": "
           << std::clamp(settings.campfireParticleIntensity, 0, 100) << ",\n"
           << "  \"spectrum3dPalette\": " << std::clamp(settings.spectrum3dPalette, 0, 5) << ",\n"
           << "  \"spectrum3dRotation\": " << std::clamp(settings.spectrum3dRotation, 0, 100) << ",\n"
           << "  \"spectrum3dTilt\": " << std::clamp(settings.spectrum3dTilt, 0, 100) << ",\n"
           << "  \"spectrum3dDepth\": " << std::clamp(settings.spectrum3dDepth, 0, 100) << ",\n"
           << "  \"spectrum3dHeight\": " << std::clamp(settings.spectrum3dHeight, 0, 100) << ",\n"
           << "  \"joyDivisionPalette\": " << std::clamp(settings.joyDivisionPalette, 0, 5) << ",\n"
           << "  \"joyDivisionRotation\": " << std::clamp(settings.joyDivisionRotation, 0, 100) << ",\n"
           << "  \"joyDivisionTilt\": " << std::clamp(settings.joyDivisionTilt, 0, 100) << ",\n"
           << "  \"joyDivisionDepth\": " << std::clamp(settings.joyDivisionDepth, 0, 100) << ",\n"
           << "  \"joyDivisionHeight\": " << std::clamp(settings.joyDivisionHeight, 0, 100) << "\n"
           << "}\n";
    const std::string contents = output.str();
    const auto bytes = std::as_bytes(std::span(contents.data(), contents.size()));
    return writeFileAtomically(
        path, {reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size()}, "설정 파일", error);
}

} // namespace vizrack
