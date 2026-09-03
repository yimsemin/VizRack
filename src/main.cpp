#include "app.h"

#include "core/i18n.h"
#include "core/utf.h"

#include <windows.h>
#include <commctrl.h>
#include <objbase.h>

#include <exception>
#include <string>

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int commandShow) {
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&controls);
    // Best-effort language for pre-settings errors; App::initialize refines it
    // once the saved preference is loaded.
    vizrack::setUiLanguage(vizrack::resolveUiLanguage("auto"));
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(com)) {
        MessageBoxW(nullptr, vizrack::trw(vizrack::Str::MsgComInitFailed).c_str(),
                    vizrack::trw(vizrack::Str::AppName).c_str(), MB_OK | MB_ICONERROR);
        return 1;
    }
    int exitCode = 1;
    try {
        vizrack::App app(instance);
        std::string error;
        if (!app.initialize(error)) {
            MessageBoxW(nullptr, vizrack::fromUtf8(error).c_str(),
                        vizrack::trw(vizrack::Str::DialogTitleStartupFailed).c_str(),
                        MB_OK | MB_ICONERROR);
        } else {
            exitCode = app.run(commandShow);
        }
    } catch (const std::exception& exception) {
        MessageBoxW(nullptr, vizrack::fromUtf8(exception.what()).c_str(),
                    vizrack::trw(vizrack::Str::DialogTitleUnhandledError).c_str(),
                    MB_OK | MB_ICONERROR);
    } catch (...) {
        MessageBoxW(nullptr, vizrack::trw(vizrack::Str::MsgUnknownError).c_str(),
                    vizrack::trw(vizrack::Str::DialogTitleUnhandledError).c_str(),
                    MB_OK | MB_ICONERROR);
    }
    CoUninitialize();
    return exitCode;
}
