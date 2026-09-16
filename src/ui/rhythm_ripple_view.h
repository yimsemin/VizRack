#pragma once

#include "builtin/draw_list.h"
#include "builtin/rhythm_ripple_engine.h"
#include "ui/gdi_back_buffer.h"
#include "ui/gdi_draw_list_renderer.h"

#include <chrono>
#include <functional>
#include <string>

#include <windows.h>

namespace vizrack {

class StereoFrameRing;

// View for the "Rhythm Ripple" built-in visualizer: a thin Win32 adapter
// mirroring StarGuitarView's/CampfireView's shape. The only tunable option
// is the single onset-detector sensitivity; see RhythmRippleOptions.
class RhythmRippleView {
public:
    using OptionsChangedCallback = std::function<void(const RhythmRippleOptions&)>;

    explicit RhythmRippleView(StereoFrameRing& ring);
    ~RhythmRippleView();

    RhythmRippleView(const RhythmRippleView&) = delete;
    RhythmRippleView& operator=(const RhythmRippleView&) = delete;

    void configure(RhythmRippleOptions options, OptionsChangedCallback callback);
    bool attach(HINSTANCE instance, HWND parent, std::string& error);
    void detach();
    void resize(int width, int height);
    void setSampleRate(uint32_t sampleRate) noexcept;
    void setShowOverlay(bool show);
    bool active() const noexcept { return hwnd_ != nullptr; }

private:
    static LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT proc(UINT message, WPARAM wParam, LPARAM lParam);
    void updateSamples();
    void paint();
    void drawOverlay(HDC dc, float width, float height) const;
    void notifyOptionsChanged();
    void showOptionsMenu(POINT screenPoint);

    StereoFrameRing& ring_;
    HWND hwnd_{};
    builtin::RhythmRippleEngine engine_;
    builtin::DrawList drawList_;
    GdiDrawListRenderer renderer_;
    GdiBackBuffer backBuffer_;
    std::chrono::steady_clock::time_point lastUpdate_{};
    OptionsChangedCallback optionsChanged_;
    bool showOverlay_{true};
};

} // namespace vizrack
