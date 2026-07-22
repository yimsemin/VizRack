#include "core/plugin_state.h"
#include "core/atomic_file.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>

namespace vizrack {
namespace {

constexpr std::array<uint8_t, 8> kMagic{'M', 'V', 'M', 'S', 'V', 'S', 'T', '3'};
constexpr uint32_t kVersion = 1;
constexpr size_t kHeaderSize = 8 + 4 + 16 + 8 + 8 + 4;
constexpr uint64_t kMaxStateBytes = 64ull * 1024ull * 1024ull;

uint32_t crc32(std::span<const uint8_t> data) {
    uint32_t crc = 0xffffffffu;
    for (uint8_t value : data) {
        crc ^= value;
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
        }
    }
    return ~crc;
}

template <typename T>
void append(std::vector<uint8_t>& output, T value) {
    for (size_t i = 0; i < sizeof(T); ++i) {
        output.push_back(static_cast<uint8_t>((value >> (i * 8)) & 0xff));
    }
}

template <typename T>
bool read(std::span<const uint8_t> input, size_t& cursor, T& value) {
    if (cursor + sizeof(T) > input.size()) return false;
    value = 0;
    for (size_t i = 0; i < sizeof(T); ++i) {
        value |= static_cast<T>(input[cursor + i]) << (i * 8);
    }
    cursor += sizeof(T);
    return true;
}

} // namespace

std::vector<uint8_t> encodePluginState(const PluginStateData& state) {
    std::vector<uint8_t> output;
    output.reserve(kHeaderSize + state.component.size() + state.controller.size());
    output.insert(output.end(), kMagic.begin(), kMagic.end());
    append<uint32_t>(output, kVersion);
    output.insert(output.end(), state.classId.begin(), state.classId.end());
    append<uint64_t>(output, state.component.size());
    append<uint64_t>(output, state.controller.size());
    append<uint32_t>(output, 0);
    output.insert(output.end(), state.component.begin(), state.component.end());
    output.insert(output.end(), state.controller.begin(), state.controller.end());
    const uint32_t checksum = crc32(std::span(output).subspan(kHeaderSize));
    const size_t checksumOffset = kHeaderSize - sizeof(uint32_t);
    for (size_t i = 0; i < sizeof(checksum); ++i) {
        output[checksumOffset + i] = static_cast<uint8_t>((checksum >> (i * 8)) & 0xff);
    }
    return output;
}

bool decodePluginState(std::span<const uint8_t> bytes, PluginStateData& state, std::string& error) {
    if (bytes.size() < kHeaderSize || !std::equal(kMagic.begin(), kMagic.end(), bytes.begin())) {
        error = "플러그인 상태 헤더가 올바르지 않습니다.";
        return false;
    }
    size_t cursor = kMagic.size();
    uint32_t version{};
    if (!read(bytes, cursor, version) || version != kVersion) {
        error = "지원하지 않는 플러그인 상태 버전입니다.";
        return false;
    }
    std::copy_n(bytes.begin() + static_cast<ptrdiff_t>(cursor), 16, state.classId.begin());
    cursor += 16;
    uint64_t componentSize{};
    uint64_t controllerSize{};
    uint32_t expectedCrc{};
    if (!read(bytes, cursor, componentSize) || !read(bytes, cursor, controllerSize) ||
        !read(bytes, cursor, expectedCrc) || componentSize > kMaxStateBytes ||
        controllerSize > kMaxStateBytes || componentSize + controllerSize != bytes.size() - cursor) {
        error = "플러그인 상태 길이가 올바르지 않습니다.";
        return false;
    }
    if (crc32(bytes.subspan(cursor)) != expectedCrc) {
        error = "플러그인 상태 체크섬이 일치하지 않습니다.";
        return false;
    }
    state.component.assign(bytes.begin() + static_cast<ptrdiff_t>(cursor),
                           bytes.begin() + static_cast<ptrdiff_t>(cursor + componentSize));
    cursor += static_cast<size_t>(componentSize);
    state.controller.assign(bytes.begin() + static_cast<ptrdiff_t>(cursor), bytes.end());
    return true;
}

bool savePluginStateFile(const std::filesystem::path& path, const PluginStateData& state,
                         std::string& error) {
    const auto bytes = encodePluginState(state);
    return writeFileAtomically(path, bytes, "플러그인 상태 파일", error);
}

bool loadPluginStateFile(const std::filesystem::path& path, PluginStateData& state,
                         std::string& error) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        error = "플러그인 상태 파일이 없습니다.";
        return false;
    }
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(input)), {});
    if (!input.good() && !input.eof()) {
        error = "플러그인 상태 파일을 읽지 못했습니다.";
        return false;
    }
    return decodePluginState(bytes, state, error);
}

void quarantineCorruptState(const std::filesystem::path& path) {
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) return;
    const auto destination = path.parent_path() / L"plugin-state.corrupt.bin";
    std::filesystem::remove(destination, ec);
    ec.clear();
    std::filesystem::rename(path, destination, ec);
    if (ec) std::filesystem::remove(path, ec);
}

} // namespace vizrack
