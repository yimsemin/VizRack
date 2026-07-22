#pragma once

#include "builtin/art_visualizer_engine.h"
#include "builtin/draw_list.h"
#include "ui/gdi_back_buffer.h"
#include "ui/gdi_draw_list_renderer.h"

#include <chrono>
#include <functional>
#include <string>

#include <windows.h>

namespace vizrack {

class StereoFrameRing;

class ArtVisualizerView {
public:
    using OptionsChangedCallback = std::function<void(const ArtVisualizerOptions&)>;

    explicit ArtVisualizerView(StereoFrameRing& ring);
    ~ArtVisualizerView();

    ArtVisualizerView(const ArtVisualizerView&) = delete;
    ArtVisualizerView& operator=(const ArtVisualizerView&) = delete;

    void configure(ArtVisualizerOptions options, OptionsChangedCallback callback);
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
    void changeScene(int offset);
    void changePalette(int offset);
    void notifyOptionsChanged();
    void showOptionsMenu(POINT screenPoint);

    StereoFrameRing& ring_;
    HWND hwnd_{};
    builtin::ArtVisualizerEngine engine_;
    builtin::DrawList drawList_;
    GdiDrawListRenderer renderer_;
    GdiBackBuffer backBuffer_;
    std::chrono::steady_clock::time_point lastUpdate_{};
    OptionsChangedCallback optionsChanged_;
};

} // namespace vizrack
