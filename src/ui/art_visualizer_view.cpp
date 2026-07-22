#include "ui/art_visualizer_view.h"

#include "core/audio_ring.h"
#include "core/utf.h"

#include <windowsx.h>

#include <algorithm>
#include <chrono>
#include <string>

namespace vizrack {
namespace {

constexpr wchar_t kWindowClass[] = L"VizRack.ArtVisualizer";
constexpr UINT_PTR kRefreshTimer = 0x4152;
constexpr UINT kSceneCommand = 600;
constexpr UINT kPaletteCommand = 700;

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

ArtVisualizerView::ArtVisualizerView(StereoFrameRing& ring) : ring_(ring) {}

ArtVisualizerView::~ArtVisualizerView() {
    detach();
}

void ArtVisualizerView::configure(ArtVisualizerOptions options,
                                  OptionsChangedCallback callback) {
    engine_.setOptions(options);
    optionsChanged_ = std::move(callback);
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
}

bool ArtVisualizerView::attach(HINSTANCE instance, HWND parent, std::string& error) {
    if (active()) return true;
    if (!renderer_.available()) {
        error = "내장 아트 비주얼라이저 그래픽 초기화에 실패했습니다.";
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
        error = "내장 아트 비주얼라이저 창 등록 실패: " + formatWindowsError(GetLastError());
        return false;
    }
    RECT client{};
    GetClientRect(parent, &client);
    hwnd_ = CreateWindowExW(0, kWindowClass, nullptr,
                            WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
                            0, 0, client.right, client.bottom, parent, nullptr, instance, this);
    if (!hwnd_) {
        error = "내장 아트 비주얼라이저 창 생성 실패: " + formatWindowsError(GetLastError());
        return false;
    }
    lastUpdate_ = std::chrono::steady_clock::now();
    if (!SetTimer(hwnd_, kRefreshTimer, 16, nullptr)) {
        error = "내장 아트 비주얼라이저 갱신 타이머를 시작하지 못했습니다.";
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
        return false;
    }
    return true;
}

void ArtVisualizerView::detach() {
    if (hwnd_) {
        KillTimer(hwnd_, kRefreshTimer);
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
    backBuffer_.reset();
    engine_.reset();
    lastUpdate_ = {};
}

void ArtVisualizerView::resize(int width, int height) {
    if (!hwnd_) return;
    SetWindowPos(hwnd_, nullptr, 0, 0, std::max(0, width), std::max(0, height),
                 SWP_NOZORDER | SWP_NOACTIVATE);
}

void ArtVisualizerView::setSampleRate(uint32_t sampleRate) noexcept {
    engine_.setSampleRate(sampleRate);
}

void ArtVisualizerView::updateSamples() {
    ring_.discardOlderThan(builtin::ArtVisualizerEngine::kMaxSamples);
    auto left = engine_.inputLeft();
    auto right = engine_.inputRight();
    const size_t count = ring_.popPlanar(left.data(), right.data(), left.size());
    engine_.update(count, elapsedSeconds(lastUpdate_));
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void ArtVisualizerView::drawOverlay(HDC dc, float width, float height) const {
    const auto info = engine_.frameInfo();
    Gdiplus::Graphics graphics(dc);
    Gdiplus::Font title(L"Segoe UI", 13.0f, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::Font smallFont(L"Segoe UI", 10.0f, Gdiplus::FontStyleRegular,
                            Gdiplus::UnitPixel);
    Gdiplus::SolidBrush bright(Gdiplus::Color(220, 225, 242, 247));
    Gdiplus::SolidBrush dim(Gdiplus::Color(145, 158, 181, 191));
    const std::wstring sceneName = fromUtf8(std::string(info.sceneName));
    graphics.DrawString(sceneName.c_str(), -1, &title, {18.0f, 14.0f}, &bright);
    const std::wstring status = std::to_wstring(info.scene + 1) + L" / " +
                                std::to_wstring(builtin::ArtVisualizerEngine::kSceneCount) +
                                L"  ·  " + fromUtf8(std::string(info.colors.name));
    Gdiplus::StringFormat right;
    right.SetAlignment(Gdiplus::StringAlignmentFar);
    graphics.DrawString(status.c_str(), -1, &smallFont,
                        {width - 218.0f, 16.0f, 200.0f, 20.0f}, &right, &dim);
    graphics.DrawString(L"AUDIO REACTIVE / BUILT-IN", -1, &smallFont,
                        {18.0f, height - 27.0f}, &dim);
    graphics.DrawString(L"CLICK: SCENE  ·  RIGHT CLICK: OPTIONS", -1, &smallFont,
                        {width - 300.0f, height - 27.0f, 282.0f, 18.0f}, &right, &dim);
}

void ArtVisualizerView::paint() {
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

void ArtVisualizerView::notifyOptionsChanged() {
    if (optionsChanged_) optionsChanged_(engine_.options());
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
}

void ArtVisualizerView::changeScene(int offset) {
    auto options = engine_.options();
    options.scene = (options.scene + offset + builtin::ArtVisualizerEngine::kSceneCount) %
                    builtin::ArtVisualizerEngine::kSceneCount;
    engine_.setOptions(options);
    notifyOptionsChanged();
}

void ArtVisualizerView::changePalette(int offset) {
    auto options = engine_.options();
    options.palette = (options.palette + offset + builtin::ArtVisualizerEngine::kPaletteCount) %
                      builtin::ArtVisualizerEngine::kPaletteCount;
    engine_.setOptions(options);
    notifyOptionsChanged();
}

void ArtVisualizerView::showOptionsMenu(POINT point) {
    if (point.x == -1 && point.y == -1) {
        RECT rect{};
        GetWindowRect(hwnd_, &rect);
        point = {rect.left + 24, rect.top + 48};
    }
    const auto options = engine_.options();
    HMENU menu = CreatePopupMenu();
    HMENU scenes = CreatePopupMenu();
    HMENU palettes = CreatePopupMenu();
    for (int index = 0; index < builtin::ArtVisualizerEngine::kSceneCount; ++index) {
        const std::wstring label = fromUtf8(std::string(builtin::ArtVisualizerEngine::sceneName(index)));
        AppendMenuW(scenes, MF_STRING | (index == options.scene ? MF_CHECKED : 0),
                    kSceneCommand + index, label.c_str());
    }
    for (int index = 0; index < builtin::ArtVisualizerEngine::kPaletteCount; ++index) {
        const std::wstring label = fromUtf8(std::string(builtin::ArtVisualizerEngine::palette(index).name));
        AppendMenuW(palettes, MF_STRING | (index == options.palette ? MF_CHECKED : 0),
                    kPaletteCommand + index, label.c_str());
    }
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(scenes), L"장면");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(palettes), L"색상 팔레트");
    const UINT command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
                                        point.x, point.y, 0, hwnd_, nullptr);
    auto updated = options;
    if (command >= kSceneCommand &&
        command < kSceneCommand + builtin::ArtVisualizerEngine::kSceneCount) {
        updated.scene = static_cast<int>(command - kSceneCommand);
    } else if (command >= kPaletteCommand &&
               command < kPaletteCommand + builtin::ArtVisualizerEngine::kPaletteCount) {
        updated.palette = static_cast<int>(command - kPaletteCommand);
    }
    DestroyMenu(menu);
    if (updated.scene != options.scene || updated.palette != options.palette) {
        engine_.setOptions(updated);
        notifyOptionsChanged();
    }
}

LRESULT CALLBACK ArtVisualizerView::windowProc(HWND hwnd, UINT message, WPARAM wParam,
                                               LPARAM lParam) {
    auto* self = reinterpret_cast<ArtVisualizerView*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<ArtVisualizerView*>(create->lpCreateParams);
        self->hwnd_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    return self ? self->proc(message, wParam, lParam)
                : DefWindowProcW(hwnd, message, wParam, lParam);
}

LRESULT ArtVisualizerView::proc(UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_TIMER:
            if (wParam == kRefreshTimer) { updateSamples(); return 0; }
            break;
        case WM_PAINT: paint(); return 0;
        case WM_ERASEBKGND: return 1;
        case WM_LBUTTONDOWN: SetFocus(hwnd_); return 0;
        case WM_LBUTTONUP: changeScene(1); return 0;
        case WM_CONTEXTMENU:
            SetFocus(hwnd_);
            showOptionsMenu({GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)});
            return 0;
        case WM_KEYDOWN:
            if (wParam == VK_SPACE || wParam == VK_RIGHT || wParam == VK_UP) {
                changeScene(1);
                return 0;
            }
            if (wParam == VK_LEFT || wParam == VK_DOWN) {
                changeScene(-1);
                return 0;
            }
            if (wParam == L'C') {
                changePalette(1);
                return 0;
            }
            if (wParam >= L'1' &&
                wParam < L'1' + builtin::ArtVisualizerEngine::kSceneCount) {
                auto options = engine_.options();
                options.scene = static_cast<int>(wParam - L'1');
                engine_.setOptions(options);
                notifyOptionsChanged();
                return 0;
            }
            break;
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
