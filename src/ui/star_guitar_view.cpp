#include "ui/star_guitar_view.h"

#include "core/audio_ring.h"
#include "core/i18n.h"
#include "core/utf.h"

#include <windowsx.h>

#include <algorithm>
#include <chrono>
#include <utility>

namespace vizrack {
namespace {

constexpr wchar_t kWindowClass[] = L"VizRack.StarGuitar";
constexpr UINT_PTR kRefreshTimer = 0x5347;
constexpr UINT kAlgorithmReactiveCommand = 900;
constexpr UINT kAlgorithmPredictiveCommand = 901;

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

} // namespace

StarGuitarView::StarGuitarView(StereoFrameRing& ring) : ring_(ring) {}

StarGuitarView::~StarGuitarView() {
    detach();
}

void StarGuitarView::configure(StarGuitarOptions options, OptionsChangedCallback callback) {
    engine_.setOptions(options);
    optionsChanged_ = std::move(callback);
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
}

bool StarGuitarView::attach(HINSTANCE instance, HWND parent, std::string& error) {
    if (active()) return true;
    if (!renderer_.available()) {
        error = "Failed to initialize built-in star guitar graphics.";
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
        error = "Failed to register the built-in star guitar window: " +
                formatWindowsError(GetLastError());
        return false;
    }
    RECT client{};
    GetClientRect(parent, &client);
    hwnd_ = CreateWindowExW(0, kWindowClass, nullptr,
                            WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
                            0, 0, client.right, client.bottom, parent, nullptr, instance, this);
    if (!hwnd_) {
        error = "Failed to create the built-in star guitar window: " +
                formatWindowsError(GetLastError());
        return false;
    }
    lastUpdate_ = std::chrono::steady_clock::now();
    if (!SetTimer(hwnd_, kRefreshTimer, 16, nullptr)) {
        error = "Failed to start the built-in star guitar refresh timer.";
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
        return false;
    }
    return true;
}

void StarGuitarView::detach() {
    if (hwnd_) {
        KillTimer(hwnd_, kRefreshTimer);
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
    backBuffer_.reset();
    engine_.reset();
    lastUpdate_ = {};
}

void StarGuitarView::resize(int width, int height) {
    if (!hwnd_) return;
    SetWindowPos(hwnd_, nullptr, 0, 0, std::max(0, width), std::max(0, height),
                 SWP_NOZORDER | SWP_NOACTIVATE);
}

void StarGuitarView::setSampleRate(uint32_t sampleRate) noexcept {
    engine_.setSampleRate(sampleRate);
}

void StarGuitarView::updateSamples() {
    ring_.discardOlderThan(builtin::StarGuitarEngine::kMaxSamples);
    auto left = engine_.inputLeft();
    auto right = engine_.inputRight();
    const size_t count = ring_.popPlanar(left.data(), right.data(), left.size());
    engine_.update(count, elapsedSeconds(lastUpdate_));
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void StarGuitarView::drawOverlay(HDC dc, float width, float height) const {
    // Stylized caption; kept English-only like other builtin scene names
    // (CLAUDE.md ▸ Localization).
    Gdiplus::Graphics graphics(dc);
    Gdiplus::Font title(L"Segoe UI", 12.0f, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::Font smallFont(L"Segoe UI", 9.0f, Gdiplus::FontStyleRegular,
                            Gdiplus::UnitPixel);
    Gdiplus::SolidBrush bright(Gdiplus::Color(175, 210, 226, 244));
    Gdiplus::SolidBrush dim(Gdiplus::Color(105, 150, 172, 198));
    graphics.DrawString(L"STAR GUITAR", -1, &title, {18.0f, 14.0f}, &bright);
    graphics.DrawString(L"PROTOTYPE  /  RHYTHM SEQUENCER LANDSCAPE", -1, &smallFont,
                        {18.0f, height - 27.0f}, &dim);
    Gdiplus::StringFormat right;
    right.SetAlignment(Gdiplus::StringAlignmentFar);
    graphics.DrawString(L"RIGHT CLICK: OPTIONS", -1, &smallFont,
                        {width - 208.0f, height - 27.0f, 190.0f, 18.0f}, &right, &dim);
    if (!inspiration_.empty()) {
        const std::wstring inspiration = fromUtf8(inspiration_);
        graphics.DrawString(inspiration.c_str(), -1, &smallFont, {18.0f, 34.0f}, &dim);
    }
}

void StarGuitarView::paint() {
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
    drawOverlay(dc, static_cast<float>(width), static_cast<float>(height));
    if (buffered) backBuffer_.present(target, width, height);
    EndPaint(hwnd_, &paint);
}

void StarGuitarView::notifyOptionsChanged() {
    if (optionsChanged_) optionsChanged_(engine_.options());
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
}

void StarGuitarView::showOptionsMenu(POINT point) {
    if (point.x == -1 && point.y == -1) {
        RECT rect{};
        GetWindowRect(hwnd_, &rect);
        point = {rect.left + 24, rect.top + 48};
    }
    const auto options = engine_.options();
    HMENU menu = CreatePopupMenu();
    HMENU algorithm = CreatePopupMenu();
    const bool reactive = options.algorithmMode == StarGuitarAlgorithmMode::reactive;
    AppendMenuW(algorithm, MF_STRING | (reactive ? MF_CHECKED : 0), kAlgorithmReactiveCommand,
                trw(Str::StarGuitarAlgorithmReactive).c_str());
    AppendMenuW(algorithm, MF_STRING | (!reactive ? MF_CHECKED : 0), kAlgorithmPredictiveCommand,
                trw(Str::StarGuitarAlgorithmPredictive).c_str());
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(algorithm),
                trw(Str::StarGuitarMenuAlgorithm).c_str());

    const UINT command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, point.x, point.y,
                                        0, hwnd_, nullptr);
    auto updated = options;
    bool changed = false;
    if (command == kAlgorithmReactiveCommand) {
        updated.algorithmMode = StarGuitarAlgorithmMode::reactive;
        changed = true;
    } else if (command == kAlgorithmPredictiveCommand) {
        updated.algorithmMode = StarGuitarAlgorithmMode::predictive;
        changed = true;
    }
    DestroyMenu(menu);
    if (changed) {
        engine_.setOptions(updated);
        notifyOptionsChanged();
    }
}

LRESULT CALLBACK StarGuitarView::windowProc(HWND hwnd, UINT message, WPARAM wParam,
                                            LPARAM lParam) {
    auto* self = reinterpret_cast<StarGuitarView*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<StarGuitarView*>(create->lpCreateParams);
        self->hwnd_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    return self ? self->proc(message, wParam, lParam)
                : DefWindowProcW(hwnd, message, wParam, lParam);
}

LRESULT StarGuitarView::proc(UINT message, WPARAM wParam, LPARAM lParam) {
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
