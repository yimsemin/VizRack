#pragma once

#include <string>
#include <string_view>

namespace vizrack {

std::string toUtf8(std::wstring_view value);
std::wstring fromUtf8(std::string_view value);
std::string formatWindowsError(unsigned long error);

} // namespace vizrack

