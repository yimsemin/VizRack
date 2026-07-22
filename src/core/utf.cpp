#include "core/utf.h"

#include <windows.h>

#include <stdexcept>
#include <vector>

namespace vizrack {
namespace {

template <typename OutChar, typename InChar, typename Convert>
std::basic_string<OutChar> convertString(std::basic_string_view<InChar> input, Convert convert) {
    if (input.empty()) {
        return {};
    }
    const int needed = convert(nullptr, 0);
    if (needed <= 0) {
        throw std::runtime_error("UTF conversion failed");
    }
    std::basic_string<OutChar> output(static_cast<size_t>(needed), OutChar{});
    if (convert(output.data(), needed) != needed) {
        throw std::runtime_error("UTF conversion failed");
    }
    return output;
}

} // namespace

std::string toUtf8(std::wstring_view value) {
    return convertString<char>(value, [&](char* output, int size) {
        return WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
                                   static_cast<int>(value.size()), output, size, nullptr, nullptr);
    });
}

std::wstring fromUtf8(std::string_view value) {
    return convertString<wchar_t>(value, [&](wchar_t* output, int size) {
        return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                                   static_cast<int>(value.size()), output, size);
    });
}

std::string formatWindowsError(unsigned long error) {
    wchar_t* raw = nullptr;
    const DWORD count = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER |
                                           FORMAT_MESSAGE_FROM_SYSTEM |
                                           FORMAT_MESSAGE_IGNORE_INSERTS,
                                       nullptr, error, 0, reinterpret_cast<wchar_t*>(&raw), 0,
                                       nullptr);
    std::wstring message = count && raw ? std::wstring(raw, count) : L"Unknown Windows error";
    if (raw) {
        LocalFree(raw);
    }
    while (!message.empty() && (message.back() == L'\r' || message.back() == L'\n')) {
        message.pop_back();
    }
    return toUtf8(message);
}

} // namespace vizrack

