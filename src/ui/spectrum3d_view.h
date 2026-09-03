#pragma once

#include "builtin/draw_list.h"
#include "builtin/spectrum3d_engine.h"
#include "ui/gdi_back_buffer.h"
#include "ui/gdi_draw_list_renderer.h"

#include <chrono>
#include <functional>
#include <string>

#include <windows.h>

namespace vizrack {

class StereoFrameRing;

// One view class backs both built-in cascade plug-ins; `style` (0 = CLASSIC CASCADE,
// 1 = JOY DIVISION) is fixed per instance and the palette is the only runtime option.
class Spectrum3dView {
public:
    using OptionsChangedCallback = std::function<void(const Spectrum3dOptions&)>;

    Spectrum3dView(StereoFrameRing& ring, int style);
    ~Spectrum3dView();

    Spectrum3dView(const Spectrum3dView&) = delete;
    Spectrum3dView& operator=(const Spectrum3dView&) = delete;

    void configure(Spectrum3dOptions options, OptionsChangedCallback callback);
    bool attach(HINSTANCE instance, HWND parent, std::string& error);
    void detach();
    void resize(int width, int height);
    void setSampleRate(uint32_t sampleRate) noexcept;
    bool active() const noexcept { return hwnd_ != nullptr; }

private:
    static LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT proc(UINT message, WPARAM wParam, LPARAM lParam);
    void updateSamples();
    void paint();
    void drawOverlay(HDC dc, float width, float height) const;
    void changePalette(int offset);
    void notifyOptionsChanged();
    void showOptionsMenu(POINT screenPoint);

    StereoFrameRing& ring_;
    int style_;
    const wchar_t* windowClass_;
    HWND hwnd_{};
    builtin::Spectrum3dEngine engine_;
    builtin::DrawList drawList_;
    GdiDrawListRenderer renderer_;
    GdiBackBuffer backBuffer_;
    std::chrono::steady_clock::time_point lastUpdate_{};
    OptionsChangedCallback optionsChanged_;
};

} // namespace vizrack
