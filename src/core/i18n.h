#pragma once

#include <string>
#include <string_view>

namespace vizrack {

// UI languages VizRack ships. English is the neutral default for an
// international build; Korean is the original locale.
enum class UiLanguage { english, korean };

// Every user-facing string in the Windows UI layer. The list lives in
// core/i18n_strings.inc so the enum and both translation tables share one
// source — a row missing a language does not compile.
enum class Str {
#define VIZRACK_STR(id, en, ko) id,
#include "core/i18n_strings.inc"
#undef VIZRACK_STR
    count_
};

// Resolve a persisted preference ("auto", "en" or "ko") to a concrete language.
// "auto" (or anything unrecognized) consults the OS UI language list and falls
// back to English.
UiLanguage resolveUiLanguage(std::string_view preference);

// Canonical settings token for a language: "en" or "ko".
std::string_view uiLanguageToken(UiLanguage language) noexcept;

// Process-global current language. English until setUiLanguage() is called.
void setUiLanguage(UiLanguage language) noexcept;
UiLanguage currentUiLanguage() noexcept;

// Look up a string in the current language. tr() returns a UTF-8 C string owned
// by the table (never null); trw() converts it to UTF-16 for Win32 APIs.
const char* tr(Str id) noexcept;
std::wstring trw(Str id);

} // namespace vizrack
