#pragma once

#include <windows.h>
#include <windowsx.h>

namespace vizrack {

// Height, in 96-dpi pixels, of the strip along the top of a borderless window that moves
// the window when dragged and brings the border back when double-clicked.
inline constexpr float kBorderlessDragStripHeight = 24.0f;

// True when a WM_NCHITTEST at the screen point in `lParam` lands in that strip of the
// borderless top-level window that owns `view`. A child view answers HTTRANSPARENT there
// so the main window decides; otherwise the view would swallow the hit as client area.
inline bool isInBorderlessDragStrip(HWND view, LPARAM lParam) {
    const HWND root = GetAncestor(view, GA_ROOT);
    if (!root || (GetWindowLongPtrW(root, GWL_STYLE) & WS_CAPTION) == WS_CAPTION) return false;
    POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
    ScreenToClient(root, &point);
    const int strip =
        static_cast<int>(kBorderlessDragStripHeight * static_cast<float>(GetDpiForWindow(root)) / 96.0f);
    return point.y >= 0 && point.y < strip;
}

} // namespace vizrack
