#include "ui/campfire_view.h"

#include "core/audio_ring.h"
#include "core/utf.h"

#include <windowsx.h>

#include <algorithm>
#include <chrono>
#include <utility>

namespace vizrack {
namespace {

constexpr wchar_t kWindowClass[] = L"VizRack.Campfire";
constexpr UINT_PTR kRefreshTimer = 0x4346;
constexpr UINT kFlameResponseCommand = 600;
constexpr UINT kStarSpeedCommand = 620;
constexpr UINT kStarBrightnessCommand = 640;
constexpr UINT kStarResponseCommand = 660;
constexpr UINT kParticleAmountCommand = 680;
constexpr UINT kParticleIntensityCommand = 700;

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

void appendValueMenu(HMENU parent, const wchar_t* label, UINT commandBase,
                     int currentValue) {
    HMENU values = CreatePopupMenu();
    for (int value = 0; value <= 100; value += 10) {
        const std::wstring valueLabel = std::to_wstring(value);
        AppendMenuW(values, MF_STRING | (value == currentValue ? MF_CHECKED : 0),
                    commandBase + static_cast<UINT>(value / 10), valueLabel.c_str());
    }
    AppendMenuW(parent, MF_POPUP, reinterpret_cast<UINT_PTR>(values), label);
}

} // namespace

CampfireView::CampfireView(StereoFrameRing& ring) : ring_(ring) {}

CampfireView::~CampfireView() {
    detach();
}

void CampfireView::configure(CampfireOptions options,
                             OptionsChangedCallback callback) {
    engine_.setOptions(options);
    optionsChanged_ = std::move(callback);
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
}

bool CampfireView::attach(HINSTANCE instance, HWND parent, std::string& error) {
    if (active()) return true;
    if (!renderer_.available()) {
        error = "내장 캠프파이어 그래픽 초기화에 실패했습니다.";
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
        error = "내장 캠프파이어 창 등록 실패: " + formatWindowsError(GetLastError());
        return false;
    }
    RECT client{};
    GetClientRect(parent, &client);
    hwnd_ = CreateWindowExW(0, kWindowClass, nullptr,
                            WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
                            0, 0, client.right, client.bottom, parent, nullptr, instance, this);
    if (!hwnd_) {
        error = "내장 캠프파이어 창 생성 실패: " + formatWindowsError(GetLastError());
        return false;
    }
    lastUpdate_ = std::chrono::steady_clock::now();
    if (!SetTimer(hwnd_, kRefreshTimer, 16, nullptr)) {
        error = "내장 캠프파이어 갱신 타이머를 시작하지 못했습니다.";
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
        return false;
    }
    return true;
}

void CampfireView::detach() {
    if (hwnd_) {
        KillTimer(hwnd_, kRefreshTimer);
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
    backBuffer_.reset();
    engine_.reset();
    lastUpdate_ = {};
}

void CampfireView::resize(int width, int height) {
    if (!hwnd_) return;
    SetWindowPos(hwnd_, nullptr, 0, 0, std::max(0, width), std::max(0, height),
                 SWP_NOZORDER | SWP_NOACTIVATE);
}

void CampfireView::setSampleRate(uint32_t sampleRate) noexcept {
    engine_.setSampleRate(sampleRate);
}

void CampfireView::updateSamples() {
    ring_.discardOlderThan(builtin::CampfireEngine::kMaxSamples);
    auto left = engine_.inputLeft();
    auto right = engine_.inputRight();
    const size_t count = ring_.popPlanar(left.data(), right.data(), left.size());
    engine_.update(count, elapsedSeconds(lastUpdate_));
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void CampfireView::drawOverlay(HDC dc, float width, float height) const {
    Gdiplus::Graphics graphics(dc);
    Gdiplus::Font title(L"Segoe UI", 12.0f, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::Font smallFont(L"Segoe UI", 9.0f, Gdiplus::FontStyleRegular,
                            Gdiplus::UnitPixel);
    Gdiplus::SolidBrush warm(Gdiplus::Color(175, 244, 220, 188));
    Gdiplus::SolidBrush dim(Gdiplus::Color(105, 188, 165, 145));
    graphics.DrawString(L"CAMPFIRE", -1, &title, {18.0f, 14.0f}, &warm);
    graphics.DrawString(L"NATURAL FLAME  /  GENTLE AUDIO REACTION", -1, &smallFont,
                        {18.0f, height - 27.0f}, &dim);
    Gdiplus::StringFormat right;
    right.SetAlignment(Gdiplus::StringAlignmentFar);
    graphics.DrawString(L"RIGHT CLICK: OPTIONS", -1, &smallFont,
                        {width - 208.0f, height - 27.0f, 190.0f, 18.0f}, &right, &dim);
}

void CampfireView::paint() {
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

void CampfireView::notifyOptionsChanged() {
    if (optionsChanged_) optionsChanged_(engine_.options());
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
}

void CampfireView::showOptionsMenu(POINT point) {
    if (point.x == -1 && point.y == -1) {
        RECT rect{};
        GetWindowRect(hwnd_, &rect);
        point = {rect.left + 24, rect.top + 48};
    }
    const auto options = engine_.options();
    HMENU menu = CreatePopupMenu();
    appendValueMenu(menu, L"불꽃 음악 반응", kFlameResponseCommand,
                    options.flameResponse);
    appendValueMenu(menu, L"별 이동 속도", kStarSpeedCommand,
                    options.starSpeed);
    appendValueMenu(menu, L"별 평소 밝기", kStarBrightnessCommand,
                    options.starBrightness);
    appendValueMenu(menu, L"별 음악 반응", kStarResponseCommand,
                    options.starResponse);
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    appendValueMenu(menu, L"불티 양", kParticleAmountCommand,
                    options.particleAmount);
    appendValueMenu(menu, L"불티 강도", kParticleIntensityCommand,
                    options.particleIntensity);

    const UINT command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
                                        point.x, point.y, 0, hwnd_, nullptr);
    auto updated = options;
    const auto applyValue = [command](UINT base, int& destination) {
        if (command < base || command > base + 10) return false;
        destination = static_cast<int>(command - base) * 10;
        return true;
    };
    const bool changed =
        applyValue(kFlameResponseCommand, updated.flameResponse) ||
        applyValue(kStarSpeedCommand, updated.starSpeed) ||
        applyValue(kStarBrightnessCommand, updated.starBrightness) ||
        applyValue(kStarResponseCommand, updated.starResponse) ||
        applyValue(kParticleAmountCommand, updated.particleAmount) ||
        applyValue(kParticleIntensityCommand, updated.particleIntensity);
    DestroyMenu(menu);
    if (changed) {
        engine_.setOptions(updated);
        notifyOptionsChanged();
    }
}

LRESULT CALLBACK CampfireView::windowProc(HWND hwnd, UINT message, WPARAM wParam,
                                          LPARAM lParam) {
    auto* self = reinterpret_cast<CampfireView*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<CampfireView*>(create->lpCreateParams);
        self->hwnd_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    return self ? self->proc(message, wParam, lParam)
                : DefWindowProcW(hwnd, message, wParam, lParam);
}

LRESULT CampfireView::proc(UINT message, WPARAM wParam, LPARAM lParam) {
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
