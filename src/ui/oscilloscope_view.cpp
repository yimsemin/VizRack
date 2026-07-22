#include "ui/oscilloscope_view.h"

#include "core/audio_ring.h"
#include "core/utf.h"

#include <algorithm>
#include <string>
#include <windowsx.h>

namespace vizrack {
namespace {

constexpr wchar_t kWindowClass[] = L"VizRack.Oscilloscope";
constexpr UINT_PTR kRefreshTimer = 0x4f53;
constexpr UINT kModeWaveform = 100;
constexpr UINT kModeHistory = 101;
constexpr UINT kScaleSmall = 200;
constexpr UINT kScaleNormal = 201;
constexpr UINT kScaleLarge = 202;
constexpr UINT kSmoothingOff = 300;
constexpr UINT kSmoothingLight = 301;
constexpr UINT kSmoothingStrong = 302;
constexpr UINT kFps15 = 415;
constexpr UINT kFps30 = 430;
constexpr UINT kFps60 = 460;

int timerInterval(int fps) {
    return fps == 15 ? 67 : (fps == 30 ? 33 : 16);
}

void checkMenu(HMENU menu, UINT command, bool checked) {
    CheckMenuItem(menu, command, MF_BYCOMMAND | (checked ? MF_CHECKED : MF_UNCHECKED));
}

} // namespace

OscilloscopeView::OscilloscopeView(StereoFrameRing& ring) : ring_(ring) {}

OscilloscopeView::~OscilloscopeView() {
    detach();
}

void OscilloscopeView::configure(OscilloscopeOptions options,
                                  OptionsChangedCallback callback) {
    engine_.setOptions(options);
    optionsChanged_ = std::move(callback);
    applyTimer();
}

bool OscilloscopeView::attach(HINSTANCE instance, HWND parent, std::string& error) {
    if (active()) return true;
    if (!renderer_.available()) {
        error = "내장 오실로스코프 그래픽 초기화에 실패했습니다.";
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
        error = "내장 오실로스코프 창 등록 실패: " + formatWindowsError(GetLastError());
        return false;
    }
    RECT client{};
    GetClientRect(parent, &client);
    hwnd_ = CreateWindowExW(0, kWindowClass, nullptr,
                            WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
                            0, 0, client.right, client.bottom, parent, nullptr, instance, this);
    if (!hwnd_) {
        error = "내장 오실로스코프 창 생성 실패: " + formatWindowsError(GetLastError());
        return false;
    }
    if (!SetTimer(hwnd_, kRefreshTimer, timerInterval(engine_.options().fps), nullptr)) {
        error = "내장 오실로스코프 갱신 타이머를 시작하지 못했습니다.";
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
        return false;
    }
    return true;
}

void OscilloscopeView::detach() {
    if (hwnd_) {
        KillTimer(hwnd_, kRefreshTimer);
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
    backBuffer_.reset();
    engine_.reset();
}

void OscilloscopeView::resize(int width, int height) {
    if (!hwnd_) return;
    SetWindowPos(hwnd_, nullptr, 0, 0, std::max(0, width), std::max(0, height),
                 SWP_NOZORDER | SWP_NOACTIVATE);
}

void OscilloscopeView::setSampleRate(uint32_t sampleRate) noexcept {
    engine_.setSampleRate(sampleRate);
}

void OscilloscopeView::applyTimer() {
    if (!hwnd_) return;
    KillTimer(hwnd_, kRefreshTimer);
    SetTimer(hwnd_, kRefreshTimer, timerInterval(engine_.options().fps), nullptr);
}

void OscilloscopeView::updateSamples() {
    ring_.discardOlderThan(builtin::OscilloscopeEngine::kMaxSamples);
    auto left = engine_.inputLeft();
    auto right = engine_.inputRight();
    const size_t count = ring_.popPlanar(left.data(), right.data(), left.size());
    engine_.update(count);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void OscilloscopeView::drawOverlay(HDC dc, const RECT& client) const {
    const auto info = engine_.frameInfo();
    const int width = client.right;
    const int height = client.bottom;
    const int centers[] = {height / 4, height * 3 / 4};
    SetBkMode(dc, TRANSPARENT);
    SelectObject(dc, GetStockObject(DEFAULT_GUI_FONT));
    SetTextColor(dc, RGB(145, 168, 174));
    const std::wstring title = info.options.historyMode ? L"내장 오실로스코프 · 시간 히스토리"
                                                        : L"내장 오실로스코프 · 순간 파형";
    TextOutW(dc, 12, 10, title.c_str(), static_cast<int>(title.size()));
    const std::wstring status = std::to_wstring(info.sampleRate) + L" Hz  |  " +
                                std::to_wstring(info.options.fps) + L" FPS";
    SetTextAlign(dc, TA_RIGHT | TA_TOP);
    TextOutW(dc, width - 12, 10, status.c_str(), static_cast<int>(status.size()));
    TextOutW(dc, width - 12, height - 24, L"우클릭: 표시 설정", 10);
    SetTextAlign(dc, TA_LEFT | TA_TOP);
    SetTextColor(dc, RGB(58, 214, 174));
    TextOutW(dc, 12, std::max(32, centers[0] - 20), L"L", 1);
    SetTextColor(dc, RGB(85, 168, 255));
    TextOutW(dc, 12, std::max(32, centers[1] - 20), L"R", 1);
    if (!info.options.historyMode && !info.hasSignalTrace) {
        RECT textRect = client;
        SetTextColor(dc, RGB(145, 168, 174));
        DrawTextW(dc, L"시스템 소리를 재생해 보세요", -1, &textRect,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
}

void OscilloscopeView::paint() {
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
    drawOverlay(dc, client);
    if (buffered) backBuffer_.present(target, width, height);
    EndPaint(hwnd_, &paint);
}

void OscilloscopeView::notifyOptionsChanged() {
    if (optionsChanged_) optionsChanged_(engine_.options());
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
}

void OscilloscopeView::showOptionsMenu(POINT point) {
    if (point.x == -1 && point.y == -1) {
        RECT rect{};
        GetWindowRect(hwnd_, &rect);
        point = {rect.left + 24, rect.top + 48};
    }
    auto options = engine_.options();
    HMENU menu = CreatePopupMenu();
    HMENU mode = CreatePopupMenu();
    HMENU scale = CreatePopupMenu();
    HMENU smoothing = CreatePopupMenu();
    HMENU fps = CreatePopupMenu();
    AppendMenuW(mode, MF_STRING, kModeWaveform, L"순간 파형");
    AppendMenuW(mode, MF_STRING, kModeHistory, L"시간 히스토리 (약 15초)");
    AppendMenuW(scale, MF_STRING, kScaleSmall, L"작게 (50%)");
    AppendMenuW(scale, MF_STRING, kScaleNormal, L"보통 (70%)");
    AppendMenuW(scale, MF_STRING, kScaleLarge, L"크게 (100%)");
    AppendMenuW(smoothing, MF_STRING, kSmoothingOff, L"끔");
    AppendMenuW(smoothing, MF_STRING, kSmoothingLight, L"약하게");
    AppendMenuW(smoothing, MF_STRING, kSmoothingStrong, L"강하게");
    AppendMenuW(fps, MF_STRING, kFps15, L"15 FPS");
    AppendMenuW(fps, MF_STRING, kFps30, L"30 FPS");
    AppendMenuW(fps, MF_STRING, kFps60, L"60 FPS");
    checkMenu(mode, options.historyMode ? kModeHistory : kModeWaveform, true);
    checkMenu(scale, options.scalePercent == 50 ? kScaleSmall :
                     (options.scalePercent == 100 ? kScaleLarge : kScaleNormal), true);
    checkMenu(smoothing, kSmoothingOff + static_cast<UINT>(options.smoothing), true);
    checkMenu(fps, options.fps == 15 ? kFps15 : (options.fps == 30 ? kFps30 : kFps60), true);
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(mode), L"표시 방식");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(scale), L"파형 크기");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(smoothing), L"선 안정화");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(fps), L"프레임 속도");
    const UINT command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
                                        point.x, point.y, 0, hwnd_, nullptr);
    if (command == kModeWaveform || command == kModeHistory) {
        options.historyMode = command == kModeHistory;
    }
    if (command >= kScaleSmall && command <= kScaleLarge) {
        options.scalePercent = command == kScaleSmall ? 50 : (command == kScaleLarge ? 100 : 70);
    }
    if (command >= kSmoothingOff && command <= kSmoothingStrong) {
        options.smoothing = static_cast<int>(command - kSmoothingOff);
    }
    const bool fpsChanged = command == kFps15 || command == kFps30 || command == kFps60;
    if (fpsChanged) options.fps = command == kFps15 ? 15 : (command == kFps30 ? 30 : 60);
    DestroyMenu(menu);
    if (command) {
        engine_.setOptions(options);
        if (fpsChanged) applyTimer();
        notifyOptionsChanged();
    }
}

LRESULT CALLBACK OscilloscopeView::windowProc(HWND hwnd, UINT message, WPARAM wParam,
                                              LPARAM lParam) {
    auto* self = reinterpret_cast<OscilloscopeView*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<OscilloscopeView*>(create->lpCreateParams);
        self->hwnd_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    return self ? self->proc(message, wParam, lParam)
                : DefWindowProcW(hwnd, message, wParam, lParam);
}

LRESULT OscilloscopeView::proc(UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_TIMER:
            if (wParam == kRefreshTimer) { updateSamples(); return 0; }
            break;
        case WM_PAINT: paint(); return 0;
        case WM_ERASEBKGND: return 1;
        case WM_CONTEXTMENU:
            showOptionsMenu({GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)});
            return 0;
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
