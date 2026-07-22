#pragma once

#include <filesystem>
#include <vector>

namespace vizrack {

std::filesystem::path resolveVst3Binary(const std::filesystem::path& modulePath);
bool isVst3CandidatePath(const std::filesystem::path& path);
std::vector<std::filesystem::path> discoverVst3Candidates(const std::filesystem::path& directory);

} // namespace vizrack

