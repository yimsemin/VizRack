#include "core/i18n.h"

#include "core/utf.h"

#include <windows.h>

#include <array>
#include <atomic>
#include <cwchar>

namespace vizrack {
namespace {

constexpr std::size_t kCount = static_cast<std::size_t>(Str::count_);

constexpr std::array<const char*, kCount> kEnglish{{
#define VIZRACK_STR(id, en, ko) en,
#include "core/i18n_strings.inc"
#undef VIZRACK_STR
}};

constexpr std::array<const char*, kCount> kKorean{{
#define VIZRACK_STR(id, en, ko) ko,
#include "core/i18n_strings.inc"
#undef VIZRACK_STR
}};

static_assert(kEnglish.size() == kCount, "English string table is missing rows");
static_assert(kKorean.size() == kCount, "Korean string table is missing rows");

std::atomic<UiLanguage> gLanguage{UiLanguage::english};

} // namespace

UiLanguage resolveUiLanguage(std::string_view preference) {
    if (preference == "en") return UiLanguage::english;
    if (preference == "ko") return UiLanguage::korean;

    // "auto": the first OS preferred UI language decides.
    ULONG languageCount = 0;
    ULONG bufferChars = 0;
    if (GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &languageCount, nullptr, &bufferChars) &&
        bufferChars > 1) {
        std::wstring buffer(bufferChars, L'\0');
        if (GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &languageCount, buffer.data(),
                                        &bufferChars) &&
            !buffer.empty() && buffer.front() != L'\0') {
            return _wcsnicmp(buffer.c_str(), L"ko", 2) == 0 ? UiLanguage::korean
                                                            : UiLanguage::english;
        }
    }
    return UiLanguage::english;
}

std::string_view uiLanguageToken(UiLanguage language) noexcept {
    return language == UiLanguage::korean ? std::string_view{"ko"} : std::string_view{"en"};
}

void setUiLanguage(UiLanguage language) noexcept {
    gLanguage.store(language, std::memory_order_relaxed);
}

UiLanguage currentUiLanguage() noexcept {
    return gLanguage.load(std::memory_order_relaxed);
}

const char* tr(Str id) noexcept {
    const std::size_t index = static_cast<std::size_t>(id);
    if (index >= kCount) return "";
    return currentUiLanguage() == UiLanguage::korean ? kKorean[index] : kEnglish[index];
}

std::wstring trw(Str id) {
    return fromUtf8(tr(id));
}

} // namespace vizrack
