#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>

namespace vizrack {

bool writeFileAtomically(const std::filesystem::path& path, std::span<const uint8_t> bytes,
                         std::string_view description, std::string& error);

} // namespace vizrack
