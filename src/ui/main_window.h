#pragma once

#include "core/settings.h"
#include "platform/wasapi_capture.h"

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <utility>

#include <windows.h>

namespace vizrack {

struct MainWindowCallbacks {
    std::function<void()> onExit;
    std::function<void(bool followDefault, const std::wstring& fixedDeviceId)> onDeviceSelection;
    std::function<void(const std::string& pluginId)> onPluginSelection;
    std::function<void(const std::string& pluginId, const std::filesystem::path& path)>
        onPluginPathSelected;
    std::function<void(const Settings& settings)> onSettingsChanged;
    std::function<void()> onResume;
};

class MainWindow {
public:
    MainWindow(HINSTANCE instance, Settings settings, MainWindowCallbacks callbacks);
    ~MainWindow();

    bool create(std::string& error);
    void show(int commandShow);
    HWND handle() const noexcept { return hwnd_; }
    HWND pluginParent() const noexcept { return pluginParent_; }

    void setPluginStatus(std::string status);
    void setSelectedPluginId(std::string pluginId);
    void postCaptureStatus(const CaptureStatus& status);
    void setEditorResizeHandler(std::function<void(int, int)> handler);
    void setEditorConstraintHandler(
        std::function<std::pair<int, int>(int, int)> handler);
    void setExternalResizeEnabled(bool enabled);
    void setEditorScaleHandler(std::function<void(float)> handler);
    void onPluginRequestedSize(int width, int height);
    void openPluginPicker(const std::string& pluginId, bool folder);
    void openPluginInstallPage(const std::string& pluginId);
    void scheduleClose(unsigned milliseconds);
    const Settings& settings() const noexcept { return settings_; }

private:
    static LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT proc(UINT message, WPARAM wParam, LPARAM lParam);
    bool registerClass(std::string& error);
    void createMenus();
    void retranslate();
    void rebuildDeviceMenu();
    void showContextMenu(POINT point);
    void applyWindowOptions();
    void updateLayout();
    void updateWindowTitle();
    bool constrainSizingRect(UINT edge, RECT& rect);
    void updateWindowRectSettings();
    void notifySettingsChanged();
    void handleCommand(UINT command);
    void showAboutDialog();
    void showCreditsDialog();
    void openExternalUrl(const wchar_t* url);
    void ensureVisible(RECT& rect);
    float dpiScale() const;

    HINSTANCE instance_{};
    HWND hwnd_{};
    HWND pluginParent_{};
    HMENU menuBar_{};
    HMENU settingsMenu_{};
    HMENU deviceMenu_{};
    HMENU opacityMenu_{};
    HMENU pluginMenu_{};
    HMENU languageMenu_{};
    HMENU helpMenu_{};
    Settings settings_;
    MainWindowCallbacks callbacks_;
    CaptureStatus captureStatus_;
    std::string pluginStatus_;
    std::string selectedPluginId_;
    std::function<void(int, int)> editorResizeHandler_;
    std::function<std::pair<int, int>(int, int)> editorConstraintHandler_;
    std::function<void(float)> editorScaleHandler_;
    bool externalResizeEnabled_{true};
    bool closing_{false};
};

} // namespace vizrack
