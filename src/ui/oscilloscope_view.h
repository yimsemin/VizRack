#pragma once

#include "builtin/draw_list.h"
#include "builtin/oscilloscope_engine.h"
#include "ui/gdi_back_buffer.h"
#include "ui/gdi_draw_list_renderer.h"

#include <functional>
#include <string>

#include <windows.h>

namespace vizrack {

class StereoFrameRing;

class OscilloscopeView {
public:
    using OptionsChangedCallback = std::function<void(const OscilloscopeOptions&)>;

    explicit OscilloscopeView(StereoFrameRing& ring);
    ~OscilloscopeView();

    OscilloscopeView(const OscilloscopeView&) = delete;
    OscilloscopeView& operator=(const OscilloscopeView&) = delete;

    void configure(OscilloscopeOptions options, OptionsChangedCallback callback);
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
    void drawOverlay(HDC dc, const RECT& client) const;
    void showOptionsMenu(POINT screenPoint);
    void applyTimer();
    void notifyOptionsChanged();

    StereoFrameRing& ring_;
    HWND hwnd_{};
    builtin::OscilloscopeEngine engine_;
    builtin::DrawList drawList_;
    GdiDrawListRenderer renderer_;
    GdiBackBuffer backBuffer_;
    OptionsChangedCallback optionsChanged_;
};

} // namespace vizrack
