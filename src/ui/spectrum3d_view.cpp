#include "ui/spectrum3d_view.h"

#include "core/audio_ring.h"
#include "core/utf.h"

#include <windowsx.h>

#include <algorithm>
#include <chrono>
#include <string>
#include <utility>

namespace vizrack {
namespace {

constexpr UINT_PTR kRefreshTimer = 0x5350;
constexpr UINT kPaletteCommand = 700;
constexpr UINT kRotationCommand = 720;
constexpr UINT kTiltCommand = 740;
constexpr UINT kDepthCommand = 760;
constexpr UINT kHeightCommand = 780;

void appendValueMenu(HMENU parent, const wchar_t* label, UINT commandBase, int currentValue) {
    HMENU values = CreatePopupMenu();
    for (int value = 0; value <= 100; value += 10) {
        AppendMenuW(values, MF_STRING | (value == currentValue ? MF_CHECKED : 0),
                    commandBase + static_cast<UINT>(value / 10),
                    std::to_wstring(value).c_str());
    }
    AppendMenuW(parent, MF_POPUP, reinterpret_cast<UINT_PTR>(values), label);
}

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

Spectrum3dView::Spectrum3dView(StereoFrameRing& ring, int style)
    : ring_(ring),
      style_(std::clamp(style, 0, builtin::Spectrum3dEngine::kStyleCount - 1)),
      windowClass_(style_ == 1 ? L"VizRack.JoyDivision" : L"VizRack.Spectrum3d") {}

Spectrum3dView::~Spectrum3dView() {
    detach();
}

void Spectrum3dView::configure(Spectrum3dOptions options, OptionsChangedCallback callback) {
    options.style = style_;
    engine_.setOptions(options);
    optionsChanged_ = std::move(callback);
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
}

bool Spectrum3dView::attach(HINSTANCE instance, HWND parent, std::string& error) {
    if (active()) return true;
    if (!renderer_.available()) {
        error = "내장 3D 스펙트럼 그래픽 초기화에 실패했습니다.";
        return false;
    }
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = windowProc;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
    windowClass.lpszClassName = windowClass_;
    if (!RegisterClassExW(&windowClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        error = "내장 3D 스펙트럼 창 등록 실패: " + formatWindowsError(GetLastError());
        return false;
    }
    RECT client{};
    GetClientRect(parent, &client);
    hwnd_ = CreateWindowExW(0, windowClass_, nullptr,
                            WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
                            0, 0, client.right, client.bottom, parent, nullptr, instance, this);
    if (!hwnd_) {
        error = "내장 3D 스펙트럼 창 생성 실패: " + formatWindowsError(GetLastError());
        return false;
    }
    lastUpdate_ = std::chrono::steady_clock::now();
    if (!SetTimer(hwnd_, kRefreshTimer, 16, nullptr)) {
        error = "내장 3D 스펙트럼 갱신 타이머를 시작하지 못했습니다.";
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
        return false;
    }
    return true;
}

void Spectrum3dView::detach() {
    if (hwnd_) {
        KillTimer(hwnd_, kRefreshTimer);
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
    backBuffer_.reset();
    engine_.reset();
    lastUpdate_ = {};
}

void Spectrum3dView::resize(int width, int height) {
    if (!hwnd_) return;
    SetWindowPos(hwnd_, nullptr, 0, 0, std::max(0, width), std::max(0, height),
                 SWP_NOZORDER | SWP_NOACTIVATE);
}

void Spectrum3dView::setSampleRate(uint32_t sampleRate) noexcept {
    engine_.setSampleRate(sampleRate);
}

void Spectrum3dView::updateSamples() {
    ring_.discardOlderThan(builtin::Spectrum3dEngine::kMaxSamples);
    auto left = engine_.inputLeft();
    auto right = engine_.inputRight();
    const size_t count = ring_.popPlanar(left.data(), right.data(), left.size());
    engine_.update(count, elapsedSeconds(lastUpdate_));
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Spectrum3dView::drawOverlay(HDC dc, float width, float height) const {
    const auto info = engine_.frameInfo();
    Gdiplus::Graphics graphics(dc);
    Gdiplus::Font title(L"Segoe UI", 13.0f, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::Font smallFont(L"Segoe UI", 10.0f, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
    Gdiplus::SolidBrush bright(Gdiplus::Color(220, 225, 242, 247));
    Gdiplus::SolidBrush dim(Gdiplus::Color(145, 158, 181, 191));
    const std::wstring styleName = fromUtf8(std::string(info.styleName));
    graphics.DrawString(styleName.c_str(), -1, &title, {18.0f, 14.0f}, &bright);
    const std::wstring status = fromUtf8(std::string(info.colors.name));
    Gdiplus::StringFormat right;
    right.SetAlignment(Gdiplus::StringAlignmentFar);
    graphics.DrawString(status.c_str(), -1, &smallFont,
                        {width - 218.0f, 16.0f, 200.0f, 20.0f}, &right, &dim);
    graphics.DrawString(L"TIME-DEPTH SPECTRUM / BUILT-IN", -1, &smallFont,
                        {18.0f, height - 27.0f}, &dim);
    graphics.DrawString(L"CLICK: PALETTE  ·  RIGHT CLICK: OPTIONS", -1, &smallFont,
                        {width - 300.0f, height - 27.0f, 282.0f, 18.0f}, &right, &dim);
}

void Spectrum3dView::paint() {
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

void Spectrum3dView::notifyOptionsChanged() {
    if (optionsChanged_) optionsChanged_(engine_.options());
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
}

void Spectrum3dView::changePalette(int offset) {
    auto options = engine_.options();
    options.palette = (options.palette + offset + builtin::Spectrum3dEngine::kPaletteCount) %
                      builtin::Spectrum3dEngine::kPaletteCount;
    engine_.setOptions(options);
    notifyOptionsChanged();
}

void Spectrum3dView::showOptionsMenu(POINT point) {
    if (point.x == -1 && point.y == -1) {
        RECT rect{};
        GetWindowRect(hwnd_, &rect);
        point = {rect.left + 24, rect.top + 48};
    }
    const auto options = engine_.options();
    HMENU menu = CreatePopupMenu();
    HMENU palettes = CreatePopupMenu();
    for (int index = 0; index < builtin::Spectrum3dEngine::kPaletteCount; ++index) {
        const std::wstring label =
            fromUtf8(std::string(builtin::Spectrum3dEngine::palette(index).name));
        AppendMenuW(palettes, MF_STRING | (index == options.palette ? MF_CHECKED : 0),
                    kPaletteCommand + index, label.c_str());
    }
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(palettes), L"색상 팔레트");
    if (style_ == 0) {
        appendValueMenu(menu, L"회전 (쿼터뷰)", kRotationCommand, options.rotation);
    }
    appendValueMenu(menu, L"기울기 / 원근", kTiltCommand, options.tilt);
    appendValueMenu(menu, L"시간 깊이", kDepthCommand, options.depth);
    appendValueMenu(menu, L"높이", kHeightCommand, options.heightScale);

    const UINT command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
                                        point.x, point.y, 0, hwnd_, nullptr);
    DestroyMenu(menu);

    auto updated = options;
    const auto applyValue = [command](UINT base, int& destination) {
        if (command < base || command > base + 10) return false;
        destination = static_cast<int>(command - base) * 10;
        return true;
    };
    bool changed = false;
    if (command >= kPaletteCommand &&
        command < kPaletteCommand + builtin::Spectrum3dEngine::kPaletteCount) {
        updated.palette = static_cast<int>(command - kPaletteCommand);
        changed = true;
    } else {
        changed = applyValue(kRotationCommand, updated.rotation) ||
                  applyValue(kTiltCommand, updated.tilt) ||
                  applyValue(kDepthCommand, updated.depth) ||
                  applyValue(kHeightCommand, updated.heightScale);
    }
    if (changed) {
        engine_.setOptions(updated);
        notifyOptionsChanged();
    }
}

LRESULT CALLBACK Spectrum3dView::windowProc(HWND hwnd, UINT message, WPARAM wParam,
                                            LPARAM lParam) {
    auto* self = reinterpret_cast<Spectrum3dView*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<Spectrum3dView*>(create->lpCreateParams);
        self->hwnd_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    return self ? self->proc(message, wParam, lParam)
                : DefWindowProcW(hwnd, message, wParam, lParam);
}

LRESULT Spectrum3dView::proc(UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_TIMER:
            if (wParam == kRefreshTimer) { updateSamples(); return 0; }
            break;
        case WM_PAINT: paint(); return 0;
        case WM_ERASEBKGND: return 1;
        case WM_LBUTTONDOWN: SetFocus(hwnd_); return 0;
        case WM_LBUTTONUP: changePalette(1); return 0;
        case WM_CONTEXTMENU:
            SetFocus(hwnd_);
            showOptionsMenu({GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)});
            return 0;
        case WM_KEYDOWN:
            if (wParam == VK_SPACE || wParam == VK_RIGHT || wParam == VK_UP) {
                changePalette(1);
                return 0;
            }
            if (wParam == VK_LEFT || wParam == VK_DOWN) {
                changePalette(-1);
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
