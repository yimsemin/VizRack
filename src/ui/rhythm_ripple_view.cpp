#include "ui/rhythm_ripple_view.h"

#include "core/audio_ring.h"
#include "core/i18n.h"
#include "core/utf.h"
#include "ui/overlay_font.h"

#include <windowsx.h>

#include <algorithm>
#include <chrono>
#include <utility>

namespace vizrack {
namespace {

constexpr wchar_t kWindowClass[] = L"VizRack.RhythmRipple";
constexpr UINT_PTR kRefreshTimer = 0x5252; // "RR"
constexpr UINT kSensitivityCommand = 920;
constexpr UINT kLongNoteSensitivityCommand = 940;

float elapsedSeconds(std::chrono::steady_clock::time_point& previous) {
    const auto now = std::chrono::steady_clock::now();
    if (previous.time_since_epoch().count() == 0) {
        previous = now;
        return 1.0f / 60.0f;
    }
    const float elapsed = std::chrono::duration<float>(now - previous).count();
    previous = now;
    return std::clamp(elapsed, 1.0f / 240.0f, 1.0f / 15.0f);
}

void appendValueMenu(HMENU parent, const wchar_t* label, UINT commandBase, int currentValue) {
    HMENU values = CreatePopupMenu();
    for (int value = 0; value <= 100; value += 10) {
        const std::wstring valueLabel = std::to_wstring(value);
        AppendMenuW(values, MF_STRING | (value == currentValue ? MF_CHECKED : 0),
                    commandBase + static_cast<UINT>(value / 10), valueLabel.c_str());
    }
    AppendMenuW(parent, MF_POPUP, reinterpret_cast<UINT_PTR>(values), label);
}

} // namespace

RhythmRippleView::RhythmRippleView(StereoFrameRing& ring) : ring_(ring) {}

RhythmRippleView::~RhythmRippleView() {
    detach();
}

void RhythmRippleView::configure(RhythmRippleOptions options, OptionsChangedCallback callback) {
    engine_.setOptions(options);
    optionsChanged_ = std::move(callback);
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
}

bool RhythmRippleView::attach(HINSTANCE instance, HWND parent, std::string& error) {
    if (active()) return true;
    if (!renderer_.available()) {
        error = "Failed to initialize built-in rhythm ripple graphics.";
        return false;
    }
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = windowProc;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
    windowClass.lpszClassName = kWindowClass;
    if (!RegisterClassExW(&windowClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        error = "Failed to register the built-in rhythm ripple window: " +
                formatWindowsError(GetLastError());
        return false;
    }
    RECT client{};
    GetClientRect(parent, &client);
    hwnd_ = CreateWindowExW(0, kWindowClass, nullptr,
                            WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
                            0, 0, client.right, client.bottom, parent, nullptr, instance, this);
    if (!hwnd_) {
        error = "Failed to create the built-in rhythm ripple window: " +
                formatWindowsError(GetLastError());
        return false;
    }
    lastUpdate_ = std::chrono::steady_clock::now();
    if (!SetTimer(hwnd_, kRefreshTimer, 16, nullptr)) {
        error = "Failed to start the built-in rhythm ripple refresh timer.";
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
        return false;
    }
    return true;
}

void RhythmRippleView::detach() {
    if (hwnd_) {
        KillTimer(hwnd_, kRefreshTimer);
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
    backBuffer_.reset();
    engine_.reset();
    lastUpdate_ = {};
}

void RhythmRippleView::resize(int width, int height) {
    if (!hwnd_) return;
    SetWindowPos(hwnd_, nullptr, 0, 0, std::max(0, width), std::max(0, height),
                 SWP_NOZORDER | SWP_NOACTIVATE);
}

void RhythmRippleView::setSampleRate(uint32_t sampleRate) noexcept {
    engine_.setSampleRate(sampleRate);
}

void RhythmRippleView::setShowOverlay(bool show) {
    if (showOverlay_ == show) return;
    showOverlay_ = show;
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
}

void RhythmRippleView::updateSamples() {
    ring_.discardOlderThan(builtin::RhythmRippleEngine::kMaxSamples);
    auto left = engine_.inputLeft();
    auto right = engine_.inputRight();
    const size_t count = ring_.popPlanar(left.data(), right.data(), left.size());
    engine_.update(count, elapsedSeconds(lastUpdate_));
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void RhythmRippleView::drawOverlay(HDC dc, float width, float height) const {
    Gdiplus::Graphics graphics(dc);
    Gdiplus::Font title = overlayTitleFont();
    Gdiplus::Font smallFont = overlaySmallFont();
    Gdiplus::SolidBrush bright(Gdiplus::Color(175, 210, 226, 244));
    Gdiplus::SolidBrush dim(Gdiplus::Color(105, 150, 172, 198));
    const std::wstring name = overlayCaps(trw(Str::PluginNameRhythmRipple));
    graphics.DrawString(name.c_str(), -1, &title, {18.0f, 14.0f}, &bright);
    const std::wstring tagline = trw(Str::RhythmRippleTagline);
    graphics.DrawString(tagline.c_str(), -1, &smallFont, {18.0f, height - 27.0f}, &dim);
    Gdiplus::StringFormat right;
    right.SetAlignment(Gdiplus::StringAlignmentFar);
    const std::wstring hint = trw(Str::HintRightClickOptions);
    graphics.DrawString(hint.c_str(), -1, &smallFont,
                        {width - 208.0f, height - 27.0f, 190.0f, 18.0f}, &right, &dim);
}

void RhythmRippleView::paint() {
    PAINTSTRUCT paint{};
    HDC target = BeginPaint(hwnd_, &paint);
    RECT client{};
    GetClientRect(hwnd_, &client);
    const int width = client.right;
    const int height = client.bottom;
    if (width <= 0 || height <= 0) {
        EndPaint(hwnd_, &paint);
        return;
    }
    const bool buffered = backBuffer_.ensure(target, width, height);
    HDC dc = buffered ? backBuffer_.dc() : target;
    engine_.buildFrame(static_cast<float>(width), static_cast<float>(height), drawList_);
    renderer_.render(dc, drawList_);
    if (showOverlay_) drawOverlay(dc, static_cast<float>(width), static_cast<float>(height));
    if (buffered) backBuffer_.present(target, width, height);
    EndPaint(hwnd_, &paint);
}

void RhythmRippleView::notifyOptionsChanged() {
    if (optionsChanged_) optionsChanged_(engine_.options());
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
}

void RhythmRippleView::showOptionsMenu(POINT point) {
    if (point.x == -1 && point.y == -1) {
        RECT rect{};
        GetWindowRect(hwnd_, &rect);
        point = {rect.left + 24, rect.top + 48};
    }
    const auto options = engine_.options();
    HMENU menu = CreatePopupMenu();
    appendValueMenu(menu, trw(Str::RhythmRippleMenuSensitivity).c_str(), kSensitivityCommand,
                    options.sensitivity);
    // 0 turns long notes off entirely -- there is no separate flag, see
    // RhythmRippleOptions.
    appendValueMenu(menu, trw(Str::RhythmRippleMenuLongNoteSensitivity).c_str(),
                    kLongNoteSensitivityCommand, options.longNoteSensitivity);

    const UINT command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, point.x, point.y,
                                        0, hwnd_, nullptr);
    auto updated = options;
    bool changed = false;
    if (command >= kSensitivityCommand && command <= kSensitivityCommand + 10) {
        updated.sensitivity = static_cast<int>(command - kSensitivityCommand) * 10;
        changed = true;
    } else if (command >= kLongNoteSensitivityCommand && command <= kLongNoteSensitivityCommand + 10) {
        updated.longNoteSensitivity = static_cast<int>(command - kLongNoteSensitivityCommand) * 10;
        changed = true;
    }
    DestroyMenu(menu);
    if (changed) {
        engine_.setOptions(updated);
        notifyOptionsChanged();
    }
}

LRESULT CALLBACK RhythmRippleView::windowProc(HWND hwnd, UINT message, WPARAM wParam,
                                              LPARAM lParam) {
    auto* self = reinterpret_cast<RhythmRippleView*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<RhythmRippleView*>(create->lpCreateParams);
        self->hwnd_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    return self ? self->proc(message, wParam, lParam)
                : DefWindowProcW(hwnd, message, wParam, lParam);
}

LRESULT RhythmRippleView::proc(UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_TIMER:
            if (wParam == kRefreshTimer) {
                updateSamples();
                return 0;
            }
            break;
        case WM_PAINT: paint(); return 0;
        case WM_ERASEBKGND: return 1;
        case WM_LBUTTONDOWN: SetFocus(hwnd_); return 0;
        case WM_CONTEXTMENU:
            SetFocus(hwnd_);
            showOptionsMenu({GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)});
            return 0;
        case WM_GETDLGCODE: return DLGC_WANTARROWS | DLGC_WANTCHARS;
        case WM_NCDESTROY: {
            const HWND destroyed = hwnd_;
            SetWindowLongPtrW(destroyed, GWLP_USERDATA, 0);
            hwnd_ = nullptr;
            return DefWindowProcW(destroyed, message, wParam, lParam);
        }
    }
    return DefWindowProcW(hwnd_, message, wParam, lParam);
}

} // namespace vizrack
