#pragma once

// Shared font rule for every built-in visualizer's overlay text (title +
// status/hint captions). One family and one size pair, so a new built-in
// visualizer never has to invent its own — see CLAUDE.md ▸ UI conventions.
//
// "Segoe UI" is the Windows system UI font (Vista+): no bundling, no license
// to track, and Uniscribe/DirectWrite font-linking substitutes Malgun Gothic
// glyphs for Korean text automatically, so both languages render correctly
// through the same Gdiplus::Font.

#include <gdiplus.h>

#include <cwctype>
#include <string>

namespace vizrack {

constexpr wchar_t kOverlayFontFamily[] = L"Segoe UI";
constexpr float kOverlayTitleSize = 16.0f;  // Gdiplus::UnitPixel
constexpr float kOverlaySmallSize = 12.0f;  // Gdiplus::UnitPixel

inline Gdiplus::Font overlayTitleFont() {
    return Gdiplus::Font(kOverlayFontFamily, kOverlayTitleSize, Gdiplus::FontStyleBold,
                          Gdiplus::UnitPixel);
}

inline Gdiplus::Font overlaySmallFont() {
    return Gdiplus::Font(kOverlayFontFamily, kOverlaySmallSize, Gdiplus::FontStyleRegular,
                          Gdiplus::UnitPixel);
}

// Built-in overlay titles are stylized in all caps (matching the engine-owned
// scene/style names, which are authored in caps already). Uppercasing a
// translated string is a no-op for Hangul, so this keeps the look in both
// languages.
inline std::wstring overlayCaps(std::wstring text) {
    for (wchar_t& character : text) character = static_cast<wchar_t>(std::towupper(character));
    return text;
}

} // namespace vizrack
