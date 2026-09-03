#include "ui/main_window.h"

#include "core/i18n.h"
#include "core/utf.h"
#include "platform/audio_devices.h"
#include "resource.h"
#include "vst/plugin_catalog.h"

#include <windowsx.h>
#include <shellapi.h>
#include <shobjidl.h>

#include <algorithm>
#include <format>
#include <memory>
#include <vector>

namespace vizrack {
namespace {

constexpr wchar_t kWindowClass[] = L"VizRack.MainWindow";
constexpr UINT kCaptureStatusMessage = WM_APP + 1;
constexpr UINT kPluginResizeMessage = WM_APP + 2;
constexpr UINT_PTR kSmokeTestTimer = 0x4d56;
constexpr UINT kCommandFollowDefault = 100;
constexpr UINT kCommandAlwaysOnTop = 120;
constexpr UINT kCommandBorderless = 121;
constexpr UINT kCommandExit = 130;
constexpr UINT kCommandProjectPage = 140;
constexpr UINT kCommandAbout = 141;
constexpr UINT kCommandCredits = 142;
constexpr UINT kCommandLanguageBase = 150;
constexpr const char* kLanguageTokens[] = {"auto", "en", "ko"};
constexpr Str kLanguageLabels[] = {Str::MenuLanguageAuto, Str::MenuLanguageEnglish,
                                   Str::MenuLanguageKorean};
constexpr UINT kCommandOpacityBase = 200;
constexpr wchar_t kProjectUrl[] = L"https://github.com/yimsemin/VizRack";
constexpr UINT kCommandDeviceBase = 1000;
constexpr UINT kCommandPluginSelectBase = 2000;
constexpr UINT kCommandPluginFileBase = 3000;
constexpr UINT kCommandPluginFolderBase = 4000;
constexpr UINT kCommandPluginWebsiteBase = 5000;
constexpr int kOpacityValues[] = {100, 90, 75, 50, 25};

struct PluginSize {
    int width;
    int height;
};

std::vector<AudioDeviceInfo>& menuDevices() {
    static std::vector<AudioDeviceInfo> devices;
    return devices;
}

std::wstring stateName(CaptureState state) {
    switch (state) {
        case CaptureState::stopped: return trw(Str::CaptureStopped);
        case CaptureState::disconnected: return trw(Str::CaptureDisconnected);
        case CaptureState::connecting: return trw(Str::CaptureConnecting);
        case CaptureState::running: return trw(Str::CaptureRunning);
        case CaptureState::backoff: return trw(Str::CaptureBackoff);
    }
    return trw(Str::CaptureUnknown);
}

std::wstring localizedPluginName(const PluginDefinition& definition) {
    if (definition.id == "builtin-oscilloscope") return trw(Str::PluginNameOscilloscope);
    if (definition.id == "builtin-art-visualizer") return trw(Str::PluginNameArtVisualizer);
    if (definition.id == "builtin-campfire") return trw(Str::PluginNameCampfire);
    if (definition.id == "builtin-spectrum3d") return trw(Str::PluginNameSpectrum3d);
    if (definition.id == "builtin-joydivision") return trw(Str::PluginNameJoyDivision);
    return fromUtf8(definition.displayName);
}

std::wstring formatW(Str id, const std::wstring& argument) {
    return std::vformat(trw(id), std::make_wformat_args(argument));
}

std::filesystem::path pickPath(HWND owner, const std::wstring& pluginName, bool folder) {
    IFileOpenDialog* dialog = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&dialog)))) {
        return {};
    }
    FILEOPENDIALOGOPTIONS options{};
    dialog->GetOptions(&options);
    options |= FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST;
    if (folder) {
        options |= FOS_PICKFOLDERS;
        const std::wstring title = formatW(Str::PickerFolderTitleFmt, pluginName);
        dialog->SetTitle(title.c_str());
    } else {
        options |= FOS_FILEMUSTEXIST;
        const std::wstring vst3Filter = trw(Str::PickerFilterVst3);
        const std::wstring allFilter = trw(Str::PickerFilterAllFiles);
        const COMDLG_FILTERSPEC filters[] = {{vst3Filter.c_str(), L"*.vst3"},
                                             {allFilter.c_str(), L"*.*"}};
        dialog->SetFileTypes(static_cast<UINT>(std::size(filters)), filters);
        dialog->SetDefaultExtension(L"vst3");
        const std::wstring title = formatW(Str::PickerFileTitleFmt, pluginName);
        dialog->SetTitle(title.c_str());
    }
    std::filesystem::path result;
    if (SUCCEEDED(dialog->Show(owner))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dialog->GetResult(&item))) {
            PWSTR raw = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &raw))) {
                result = raw;
                CoTaskMemFree(raw);
            }
            item->Release();
        }
    }
    dialog->Release();
    return result;
}

} // namespace

MainWindow::MainWindow(HINSTANCE instance, Settings settings, MainWindowCallbacks callbacks)
    : instance_(instance), settings_(std::move(settings)), callbacks_(std::move(callbacks)),
      selectedPluginId_(settings_.selectedPluginId) {}

MainWindow::~MainWindow() {
    if (menuBar_) DestroyMenu(menuBar_);
}

bool MainWindow::registerClass(std::string& error) {
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_DBLCLKS | CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = windowProc;
    windowClass.hInstance = instance_;
    windowClass.hIcon = LoadIconW(instance_, MAKEINTRESOURCEW(IDI_VIZRACK_ICON));
    windowClass.hIconSm = windowClass.hIcon;
    windowClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
    windowClass.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    windowClass.lpszClassName = kWindowClass;
    if (!RegisterClassExW(&windowClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        error = "Failed to register the window class: " + formatWindowsError(GetLastError());
        return false;
    }
    return true;
}

bool MainWindow::create(std::string& error) {
    if (!registerClass(error)) return false;
    createMenus();
    RECT rect{settings_.windowX, settings_.windowY,
              settings_.windowX + settings_.windowWidth,
              settings_.windowY + settings_.windowHeight};
    ensureVisible(rect);
    hwnd_ = CreateWindowExW(WS_EX_APPWINDOW, kWindowClass, L"VizRack",
                            WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
                            rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top,
                            nullptr, menuBar_, instance_, this);
    if (!hwnd_) {
        error = "Failed to create the main window: " + formatWindowsError(GetLastError());
        return false;
    }
    pluginParent_ = CreateWindowExW(0, L"STATIC", nullptr,
                                    WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
                                    0, 0, 100, 100, hwnd_, nullptr, instance_, nullptr);
    if (!pluginParent_) {
        error = "Failed to create the plug-in host area: " + formatWindowsError(GetLastError());
        return false;
    }
    applyWindowOptions();
    updateWindowTitle();
    updateLayout();
    return true;
}

void MainWindow::show(int commandShow) {
    ShowWindow(hwnd_, commandShow);
    UpdateWindow(hwnd_);
}

void MainWindow::createMenus() {
    menuBar_ = CreateMenu();
    settingsMenu_ = CreatePopupMenu();
    deviceMenu_ = CreatePopupMenu();
    opacityMenu_ = CreatePopupMenu();
    languageMenu_ = CreatePopupMenu();
    pluginMenu_ = CreatePopupMenu();
    AppendMenuW(deviceMenu_, MF_STRING, kCommandFollowDefault, trw(Str::MenuFollowDefault).c_str());
    AppendMenuW(deviceMenu_, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(settingsMenu_, MF_POPUP, reinterpret_cast<UINT_PTR>(deviceMenu_),
                trw(Str::MenuOutputDevice).c_str());
    AppendMenuW(settingsMenu_, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(settingsMenu_, MF_STRING, kCommandAlwaysOnTop, trw(Str::MenuAlwaysOnTop).c_str());
    AppendMenuW(settingsMenu_, MF_STRING, kCommandBorderless, trw(Str::MenuBorderless).c_str());
    for (size_t index = 0; index < std::size(kOpacityValues); ++index) {
        const std::wstring label = std::to_wstring(kOpacityValues[index]) + L"%";
        AppendMenuW(opacityMenu_, MF_STRING, kCommandOpacityBase + index, label.c_str());
    }
    AppendMenuW(settingsMenu_, MF_POPUP, reinterpret_cast<UINT_PTR>(opacityMenu_),
                trw(Str::MenuOpacity).c_str());
    for (size_t index = 0; index < std::size(kLanguageTokens); ++index) {
        const bool active = settings_.uiLanguage == kLanguageTokens[index];
        AppendMenuW(languageMenu_, MF_STRING | (active ? MF_CHECKED : MF_UNCHECKED),
                    kCommandLanguageBase + index, trw(kLanguageLabels[index]).c_str());
    }
    AppendMenuW(settingsMenu_, MF_POPUP, reinterpret_cast<UINT_PTR>(languageMenu_),
                trw(Str::MenuLanguage).c_str());
    AppendMenuW(settingsMenu_, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(settingsMenu_, MF_STRING, kCommandExit, trw(Str::MenuExit).c_str());

    const auto& catalog = pluginCatalog();
    for (size_t index = 0; index < catalog.size(); ++index) {
        const auto& definition = catalog[index];
        HMENU entryMenu = CreatePopupMenu();
        AppendMenuW(entryMenu, MF_STRING, kCommandPluginSelectBase + index,
                    trw(definition.kind == PluginKind::builtIn ? Str::MenuPluginUse
                                                              : Str::MenuPluginUseAutoDetect)
                        .c_str());
        if (definition.kind == PluginKind::vst3) {
            AppendMenuW(entryMenu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(entryMenu, MF_STRING, kCommandPluginFileBase + index,
                        trw(Str::MenuPluginPickFile).c_str());
            AppendMenuW(entryMenu, MF_STRING, kCommandPluginFolderBase + index,
                        trw(Str::MenuPluginPickFolder).c_str());
            AppendMenuW(entryMenu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(entryMenu, MF_STRING, kCommandPluginWebsiteBase + index,
                        trw(Str::MenuPluginInstallPage).c_str());
        }
        AppendMenuW(pluginMenu_, MF_POPUP, reinterpret_cast<UINT_PTR>(entryMenu),
                    localizedPluginName(definition).c_str());
    }
    helpMenu_ = CreatePopupMenu();
    AppendMenuW(helpMenu_, MF_STRING, kCommandProjectPage, trw(Str::MenuHelpGithub).c_str());
    AppendMenuW(helpMenu_, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(helpMenu_, MF_STRING, kCommandCredits, trw(Str::MenuCredits).c_str());
    AppendMenuW(helpMenu_, MF_STRING, kCommandAbout, trw(Str::MenuAbout).c_str());

    AppendMenuW(menuBar_, MF_POPUP, reinterpret_cast<UINT_PTR>(settingsMenu_),
                trw(Str::MenuSettings).c_str());
    AppendMenuW(menuBar_, MF_POPUP, reinterpret_cast<UINT_PTR>(pluginMenu_),
                formatW(Str::MenuPluginBarFmt, trw(Str::PluginStatusSearching)).c_str());
    AppendMenuW(menuBar_, MF_POPUP, reinterpret_cast<UINT_PTR>(helpMenu_), trw(Str::MenuHelp).c_str());
    rebuildDeviceMenu();
    setSelectedPluginId(selectedPluginId_);
    if (!pluginStatus_.empty()) setPluginStatus(pluginStatus_);
}

void MainWindow::retranslate() {
    HMENU previous = menuBar_;
    menuBar_ = nullptr;
    createMenus();  // builds a fresh menuBar_ and its submenus
    if (hwnd_) {
        applyWindowOptions();  // SetMenu(new menuBar_) + DrawMenuBar
        updateWindowTitle();
    }
    if (previous) DestroyMenu(previous);
}

void MainWindow::rebuildDeviceMenu() {
    auto& devices = menuDevices();
    std::string error;
    enumerateRenderDevices(devices, error);
    for (UINT id = kCommandDeviceBase; DeleteMenu(deviceMenu_, id, MF_BYCOMMAND); ++id) {
    }
    CheckMenuItem(deviceMenu_, kCommandFollowDefault,
                  MF_BYCOMMAND | (settings_.followDefaultDevice ? MF_CHECKED : MF_UNCHECKED));
    for (size_t index = 0; index < devices.size(); ++index) {
        std::wstring label = devices[index].name;
        if (devices[index].isDefault) label += trw(Str::DeviceDefaultSuffix);
        AppendMenuW(deviceMenu_, MF_STRING, kCommandDeviceBase + index, label.c_str());
        if (!settings_.followDefaultDevice && fromUtf8(settings_.fixedDeviceId) == devices[index].id) {
            CheckMenuItem(deviceMenu_, kCommandDeviceBase + static_cast<UINT>(index),
                          MF_BYCOMMAND | MF_CHECKED);
        }
    }
}

void MainWindow::showContextMenu(POINT point) {
    rebuildDeviceMenu();
    CheckMenuItem(settingsMenu_, kCommandAlwaysOnTop,
                  MF_BYCOMMAND | (settings_.alwaysOnTop ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(settingsMenu_, kCommandBorderless,
                  MF_BYCOMMAND | (settings_.borderless ? MF_CHECKED : MF_UNCHECKED));
    for (size_t index = 0; index < std::size(kOpacityValues); ++index) {
        CheckMenuItem(opacityMenu_, kCommandOpacityBase + static_cast<UINT>(index),
                      MF_BYCOMMAND | (settings_.opacityPercent == kOpacityValues[index]
                                          ? MF_CHECKED
                                          : MF_UNCHECKED));
    }
    TrackPopupMenu(settingsMenu_, TPM_RIGHTBUTTON, point.x, point.y, 0, hwnd_, nullptr);
}

void MainWindow::applyWindowOptions() {
    if (!hwnd_) return;
    LONG_PTR style = GetWindowLongPtrW(hwnd_, GWL_STYLE);
    if (settings_.borderless) {
        style &= ~(WS_CAPTION | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU);
        SetMenu(hwnd_, nullptr);
    } else {
        style |= WS_CAPTION | WS_MINIMIZEBOX | WS_SYSMENU;
        if (externalResizeEnabled_) {
            style |= WS_THICKFRAME | WS_MAXIMIZEBOX;
        } else {
            style &= ~(WS_THICKFRAME | WS_MAXIMIZEBOX);
        }
        SetMenu(hwnd_, menuBar_);
    }
    SetWindowLongPtrW(hwnd_, GWL_STYLE, style);
    LONG_PTR exStyle = GetWindowLongPtrW(hwnd_, GWL_EXSTYLE) | WS_EX_LAYERED;
    SetWindowLongPtrW(hwnd_, GWL_EXSTYLE, exStyle);
    const BYTE alpha = static_cast<BYTE>(std::clamp(settings_.opacityPercent, 25, 100) * 255 / 100);
    SetLayeredWindowAttributes(hwnd_, 0, alpha, LWA_ALPHA);
    SetWindowPos(hwnd_, settings_.alwaysOnTop ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_FRAMECHANGED | SWP_NOACTIVATE);
    DrawMenuBar(hwnd_);
}

float MainWindow::dpiScale() const {
    return hwnd_ ? static_cast<float>(GetDpiForWindow(hwnd_)) / 96.0f : 1.0f;
}

void MainWindow::updateLayout() {
    if (!hwnd_ || !pluginParent_) return;
    RECT client{};
    GetClientRect(hwnd_, &client);
    const int clientWidth = static_cast<int>(client.right);
    const int pluginHeight = std::max(0, static_cast<int>(client.bottom));
    SetWindowPos(pluginParent_, nullptr, 0, 0, clientWidth, pluginHeight,
                 SWP_NOZORDER | SWP_NOACTIVATE);
    if (editorResizeHandler_) editorResizeHandler_(clientWidth, pluginHeight);
}

void MainWindow::updateWindowTitle() {
    if (!hwnd_) return;
    const std::wstring output = captureStatus_.deviceName.empty()
                                    ? trw(Str::TitleCheckingDevice)
                                    : captureStatus_.deviceName;
    std::wstring title = trw(Str::AppName) + L" — " + output;
    if (captureStatus_.state != CaptureState::running) {
        title += L" — " + stateName(captureStatus_.state);
    }
    if (captureStatus_.format.channels > 2) title += L" — " + trw(Str::TitleFrontLrMetering);
    SetWindowTextW(hwnd_, title.c_str());
}

bool MainWindow::constrainSizingRect(UINT edge, RECT& rect) {
    if (!editorConstraintHandler_ || !hwnd_) return false;
    RECT windowRect{};
    RECT clientRect{};
    GetWindowRect(hwnd_, &windowRect);
    GetClientRect(hwnd_, &clientRect);
    const int nonClientWidth = (windowRect.right - windowRect.left) - clientRect.right;
    const int nonClientHeight = (windowRect.bottom - windowRect.top) - clientRect.bottom;
    const int requestedWidth =
        std::max(1, static_cast<int>(rect.right - rect.left) - nonClientWidth);
    const int requestedHeight =
        std::max(1, static_cast<int>(rect.bottom - rect.top) - nonClientHeight);
    const auto [clientWidth, clientHeight] =
        editorConstraintHandler_(requestedWidth, requestedHeight);
    const int outerWidth = std::max(1, clientWidth) + nonClientWidth;
    const int outerHeight = std::max(1, clientHeight) + nonClientHeight;

    if (edge == WMSZ_LEFT || edge == WMSZ_TOPLEFT || edge == WMSZ_BOTTOMLEFT) {
        rect.left = rect.right - outerWidth;
    } else {
        rect.right = rect.left + outerWidth;
    }
    if (edge == WMSZ_TOP || edge == WMSZ_TOPLEFT || edge == WMSZ_TOPRIGHT) {
        rect.top = rect.bottom - outerHeight;
    } else {
        rect.bottom = rect.top + outerHeight;
    }
    return true;
}

void MainWindow::updateWindowRectSettings() {
    if (!hwnd_ || IsIconic(hwnd_)) return;
    RECT rect{};
    GetWindowRect(hwnd_, &rect);
    settings_.windowX = rect.left;
    settings_.windowY = rect.top;
    settings_.windowWidth = rect.right - rect.left;
    settings_.windowHeight = rect.bottom - rect.top;
}

void MainWindow::notifySettingsChanged() {
    updateWindowRectSettings();
    if (callbacks_.onSettingsChanged) callbacks_.onSettingsChanged(settings_);
}

void MainWindow::setPluginStatus(std::string status) {
    pluginStatus_ = std::move(status);
    if (!menuBar_ || !pluginMenu_) return;
    const std::wstring label = formatW(Str::MenuPluginBarFmt, fromUtf8(pluginStatus_));
    ModifyMenuW(menuBar_, 1, MF_BYPOSITION | MF_POPUP,
                reinterpret_cast<UINT_PTR>(pluginMenu_), label.c_str());
    if (hwnd_) DrawMenuBar(hwnd_);
}

void MainWindow::setSelectedPluginId(std::string pluginId) {
    selectedPluginId_ = std::move(pluginId);
    settings_.selectedPluginId = selectedPluginId_;
    const auto& catalog = pluginCatalog();
    for (size_t index = 0; index < catalog.size(); ++index) {
        HMENU entryMenu = GetSubMenu(pluginMenu_, static_cast<int>(index));
        if (!entryMenu) continue;
        CheckMenuItem(entryMenu, kCommandPluginSelectBase + static_cast<UINT>(index),
                      MF_BYCOMMAND | (catalog[index].id == selectedPluginId_
                                          ? MF_CHECKED
                                          : MF_UNCHECKED));
    }
    updateWindowTitle();
}

void MainWindow::postCaptureStatus(const CaptureStatus& status) {
    auto copy = std::make_unique<CaptureStatus>(status);
    if (!hwnd_ || !PostMessageW(hwnd_, kCaptureStatusMessage, 0,
                                reinterpret_cast<LPARAM>(copy.get()))) {
        return;
    }
    copy.release();
}

void MainWindow::setEditorResizeHandler(std::function<void(int, int)> handler) {
    editorResizeHandler_ = std::move(handler);
    updateLayout();
}

void MainWindow::setEditorConstraintHandler(
    std::function<std::pair<int, int>(int, int)> handler) {
    editorConstraintHandler_ = std::move(handler);
}

void MainWindow::setExternalResizeEnabled(bool enabled) {
    if (externalResizeEnabled_ == enabled) return;
    externalResizeEnabled_ = enabled;
    applyWindowOptions();
}

void MainWindow::setEditorScaleHandler(std::function<void(float)> handler) {
    editorScaleHandler_ = std::move(handler);
}

void MainWindow::onPluginRequestedSize(int width, int height) {
    if (!hwnd_) return;
    auto size = std::make_unique<PluginSize>(PluginSize{width, height});
    if (GetCurrentThreadId() == GetWindowThreadProcessId(hwnd_, nullptr)) {
        auto* raw = size.release();
        SendMessageW(hwnd_, kPluginResizeMessage, 0, reinterpret_cast<LPARAM>(raw));
    } else {
        auto* raw = size.release();
        if (!PostMessageW(hwnd_, kPluginResizeMessage, 0, reinterpret_cast<LPARAM>(raw))) {
            delete raw;
        }
    }
}

void MainWindow::openPluginPicker(const std::string& pluginId, bool folder) {
    const auto* definition = findPluginDefinition(pluginId);
    if (!definition || definition->kind != PluginKind::vst3) return;
    const auto path = pickPath(hwnd_, localizedPluginName(*definition), folder);
    if (!path.empty() && callbacks_.onPluginPathSelected) {
        callbacks_.onPluginPathSelected(pluginId, path);
    }
}

void MainWindow::openPluginInstallPage(const std::string& pluginId) {
    const auto* definition = findPluginDefinition(pluginId);
    if (!definition || definition->kind != PluginKind::vst3 || definition->installUrl.empty()) return;
    const auto result = reinterpret_cast<INT_PTR>(ShellExecuteW(
        hwnd_, L"open", fromUtf8(definition->installUrl).c_str(), nullptr, nullptr, SW_SHOWNORMAL));
    if (result <= 32) {
        MessageBoxW(hwnd_, trw(Str::MsgInstallPageOpenFailed).c_str(),
                    trw(Str::DialogTitleBrowserFailed).c_str(), MB_OK | MB_ICONERROR);
    }
}

void MainWindow::openExternalUrl(const wchar_t* url) {
    const auto result = reinterpret_cast<INT_PTR>(
        ShellExecuteW(hwnd_, L"open", url, nullptr, nullptr, SW_SHOWNORMAL));
    if (result <= 32) {
        MessageBoxW(hwnd_, trw(Str::MsgDefaultBrowserOpenFailed).c_str(), trw(Str::AppName).c_str(),
                    MB_OK | MB_ICONERROR);
    }
}

void MainWindow::showAboutDialog() {
    const std::wstring text =
        trw(Str::AppName) + L" " + fromUtf8(VIZRACK_VERSION) + L"\n\n" +
        trw(Str::AboutDescription) +
        L"\n\nGitHub: " + kProjectUrl +
        L"\nMIT License, Copyright (c) 2026 subProject\n\n"
        L"VST is a registered trademark of Steinberg Media Technologies GmbH.";
    MessageBoxW(hwnd_, text.c_str(), trw(Str::MenuAbout).c_str(), MB_OK | MB_ICONINFORMATION);
}

void MainWindow::showCreditsDialog() {
    std::wstring text = trw(Str::CreditsIntro) + L"\n";
    for (const auto& definition : pluginCatalog()) {
        if (definition.inspiration.empty()) continue;
        text += L"\n" + localizedPluginName(definition) + L"\n" +
                fromUtf8(definition.inspiration) + L"\n";
    }
    MessageBoxW(hwnd_, text.c_str(), trw(Str::MenuCredits).c_str(), MB_OK | MB_ICONINFORMATION);
}

void MainWindow::scheduleClose(unsigned milliseconds) {
    if (hwnd_) SetTimer(hwnd_, kSmokeTestTimer, milliseconds, nullptr);
}

void MainWindow::handleCommand(UINT command) {
    if (command == kCommandFollowDefault) {
        settings_.followDefaultDevice = true;
        settings_.fixedDeviceId.clear();
        if (callbacks_.onDeviceSelection) callbacks_.onDeviceSelection(true, {});
        notifySettingsChanged();
    } else if (command >= kCommandDeviceBase &&
               command < kCommandDeviceBase + menuDevices().size()) {
        const auto& device = menuDevices()[command - kCommandDeviceBase];
        settings_.followDefaultDevice = false;
        settings_.fixedDeviceId = toUtf8(device.id);
        if (callbacks_.onDeviceSelection) callbacks_.onDeviceSelection(false, device.id);
        notifySettingsChanged();
    } else if (command == kCommandAlwaysOnTop) {
        settings_.alwaysOnTop = !settings_.alwaysOnTop;
        applyWindowOptions();
        notifySettingsChanged();
    } else if (command == kCommandBorderless) {
        settings_.borderless = !settings_.borderless;
        applyWindowOptions();
        notifySettingsChanged();
    } else if (command >= kCommandOpacityBase &&
               command < kCommandOpacityBase + std::size(kOpacityValues)) {
        settings_.opacityPercent = kOpacityValues[command - kCommandOpacityBase];
        applyWindowOptions();
        notifySettingsChanged();
    } else if (command == kCommandExit) {
        SendMessageW(hwnd_, WM_CLOSE, 0, 0);
    } else if (command == kCommandProjectPage) {
        openExternalUrl(kProjectUrl);
    } else if (command == kCommandAbout) {
        showAboutDialog();
    } else if (command == kCommandCredits) {
        showCreditsDialog();
    } else if (command >= kCommandLanguageBase &&
               command < kCommandLanguageBase + std::size(kLanguageTokens)) {
        const char* token = kLanguageTokens[command - kCommandLanguageBase];
        if (settings_.uiLanguage != token) {
            settings_.uiLanguage = token;
            setUiLanguage(resolveUiLanguage(settings_.uiLanguage));
            retranslate();
            notifySettingsChanged();
        }
    } else {
        const auto& catalog = pluginCatalog();
        if (command >= kCommandPluginSelectBase &&
            command < kCommandPluginSelectBase + catalog.size()) {
            const auto& definition = catalog[command - kCommandPluginSelectBase];
            if (callbacks_.onPluginSelection) callbacks_.onPluginSelection(definition.id);
        } else if (command >= kCommandPluginFileBase &&
                   command < kCommandPluginFileBase + catalog.size()) {
            openPluginPicker(catalog[command - kCommandPluginFileBase].id, false);
        } else if (command >= kCommandPluginFolderBase &&
                   command < kCommandPluginFolderBase + catalog.size()) {
            openPluginPicker(catalog[command - kCommandPluginFolderBase].id, true);
        } else if (command >= kCommandPluginWebsiteBase &&
                   command < kCommandPluginWebsiteBase + catalog.size()) {
            openPluginInstallPage(catalog[command - kCommandPluginWebsiteBase].id);
        }
    }
}

void MainWindow::ensureVisible(RECT& rect) {
    HMONITOR monitor = MonitorFromRect(&rect, MONITOR_DEFAULTTONULL);
    if (monitor) return;
    MONITORINFO info{sizeof(info)};
    GetMonitorInfoW(MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY), &info);
    const int width = std::max(320L, rect.right - rect.left);
    const int height = std::max(240L, rect.bottom - rect.top);
    rect.left = info.rcWork.left + 40;
    rect.top = info.rcWork.top + 40;
    rect.right = rect.left + width;
    rect.bottom = rect.top + height;
}

LRESULT CALLBACK MainWindow::windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    MainWindow* self = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<MainWindow*>(create->lpCreateParams);
        self->hwnd_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    return self ? self->proc(message, wParam, lParam)
                : DefWindowProcW(hwnd, message, wParam, lParam);
}

LRESULT MainWindow::proc(UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_SIZE:
            updateLayout();
            return 0;
        case WM_SIZING:
            return constrainSizingRect(static_cast<UINT>(wParam),
                                       *reinterpret_cast<RECT*>(lParam))
                       ? TRUE
                       : FALSE;
        case WM_EXITSIZEMOVE:
            notifySettingsChanged();
            return 0;
        case WM_COMMAND:
            handleCommand(LOWORD(wParam));
            return 0;
        case WM_INITMENUPOPUP:
            rebuildDeviceMenu();
            return 0;
        case WM_CONTEXTMENU: {
            POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            if (point.x == -1 && point.y == -1) {
                RECT rect{};
                GetWindowRect(hwnd_, &rect);
                point = {rect.left + 16, rect.top + 16};
            }
            showContextMenu(point);
            return 0;
        }
        case WM_NCRBUTTONUP:
            if (settings_.borderless && wParam == HTCAPTION) {
                showContextMenu(POINT{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)});
                return 0;
            }
            break;
        case WM_KEYDOWN:
            if (wParam == VK_F10) {
                RECT rect{};
                GetWindowRect(hwnd_, &rect);
                showContextMenu(POINT{rect.left + 16, rect.top + 16});
                return 0;
            }
            break;
        case WM_NCHITTEST: {
            const LRESULT hit = DefWindowProcW(hwnd_, message, wParam, lParam);
            if (settings_.borderless && hit == HTCLIENT) {
                POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
                ScreenToClient(hwnd_, &point);
                const int dragStrip = static_cast<int>(24.0f * dpiScale());
                if (point.y >= 0 && point.y < dragStrip) return HTCAPTION;
            }
            return hit;
        }
        case WM_DPICHANGED: {
            auto* suggested = reinterpret_cast<RECT*>(lParam);
            SetWindowPos(hwnd_, nullptr, suggested->left, suggested->top,
                         suggested->right - suggested->left, suggested->bottom - suggested->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            if (editorScaleHandler_) editorScaleHandler_(dpiScale());
            updateLayout();
            return 0;
        }
        case WM_POWERBROADCAST:
            if (wParam == PBT_APMRESUMEAUTOMATIC || wParam == PBT_APMRESUMESUSPEND) {
                if (callbacks_.onResume) callbacks_.onResume();
            }
            return TRUE;
        case WM_TIMER:
            if (wParam == kSmokeTestTimer) {
                KillTimer(hwnd_, kSmokeTestTimer);
                SendMessageW(hwnd_, WM_CLOSE, 0, 0);
                return 0;
            }
            break;
        case kCaptureStatusMessage: {
            std::unique_ptr<CaptureStatus> status(reinterpret_cast<CaptureStatus*>(lParam));
            captureStatus_ = std::move(*status);
            updateWindowTitle();
            return 0;
        }
        case kPluginResizeMessage: {
            std::unique_ptr<PluginSize> size(reinterpret_cast<PluginSize*>(lParam));
            RECT client{0, 0, size->width, size->height};
            const DWORD style = static_cast<DWORD>(GetWindowLongPtrW(hwnd_, GWL_STYLE));
            const DWORD exStyle = static_cast<DWORD>(GetWindowLongPtrW(hwnd_, GWL_EXSTYLE));
            AdjustWindowRectExForDpi(&client, style, !settings_.borderless, exStyle,
                                     GetDpiForWindow(hwnd_));
            SetWindowPos(hwnd_, nullptr, 0, 0, client.right - client.left,
                         client.bottom - client.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
            return 0;
        }
        case WM_GETMINMAXINFO: {
            auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
            info->ptMinTrackSize = {320, 240};
            return 0;
        }
        case WM_CLOSE:
            if (!closing_) {
                closing_ = true;
                updateWindowRectSettings();
                if (callbacks_.onExit) callbacks_.onExit();
                DestroyWindow(hwnd_);
            }
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd_, message, wParam, lParam);
}

} // namespace vizrack
