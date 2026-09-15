#pragma once

#include "builtin/draw_list.h"
#include "builtin/star_guitar_engine.h"
#include "ui/gdi_back_buffer.h"
#include "ui/gdi_draw_list_renderer.h"

#include <chrono>
#include <functional>
#include <string>

#include <windows.h>

namespace vizrack {

class StereoFrameRing;

// View for the "Star Guitar" homage: a thin Win32 adapter mirroring
// CampfireView's shape. Tunable options are the four per-band peak-
// detection sensitivities; see StarGuitarOptions.
class StarGuitarView {
public:
    using OptionsChangedCallback = std::function<void(const StarGuitarOptions&)>;

    explicit StarGuitarView(StereoFrameRing& ring);
    ~StarGuitarView();

    StarGuitarView(const StarGuitarView&) = delete;
    StarGuitarView& operator=(const StarGuitarView&) = delete;

    void configure(StarGuitarOptions options, OptionsChangedCallback callback);
    bool attach(HINSTANCE instance, HWND parent, std::string& error);
    void detach();
    void resize(int width, int height);
    void setSampleRate(uint32_t sampleRate) noexcept;
    void setInspiration(std::string inspiration) { inspiration_ = std::move(inspiration); }
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
    std::string inspiration_;
    HWND hwnd_{};
    builtin::StarGuitarEngine engine_;
    builtin::DrawList drawList_;
    GdiDrawListRenderer renderer_;
    GdiBackBuffer backBuffer_;
    std::chrono::steady_clock::time_point lastUpdate_{};
    OptionsChangedCallback optionsChanged_;
};

} // namespace vizrack
