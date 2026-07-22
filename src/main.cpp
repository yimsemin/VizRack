#include "app.h"

#include "core/utf.h"

#include <windows.h>
#include <commctrl.h>
#include <objbase.h>

#include <exception>
#include <string>

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int commandShow) {
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&controls);
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(com)) {
        MessageBoxW(nullptr, L"COM 초기화에 실패했습니다.", L"VizRack", MB_OK | MB_ICONERROR);
        return 1;
    }
    int exitCode = 1;
    try {
        vizrack::App app(instance);
        std::string error;
        if (!app.initialize(error)) {
            MessageBoxW(nullptr, vizrack::fromUtf8(error).c_str(), L"VizRack 시작 실패",
                        MB_OK | MB_ICONERROR);
        } else {
            exitCode = app.run(commandShow);
        }
    } catch (const std::exception& exception) {
        MessageBoxW(nullptr, vizrack::fromUtf8(exception.what()).c_str(), L"처리되지 않은 오류",
                    MB_OK | MB_ICONERROR);
    } catch (...) {
        MessageBoxW(nullptr, L"알 수 없는 오류가 발생했습니다.", L"처리되지 않은 오류",
                    MB_OK | MB_ICONERROR);
    }
    CoUninitialize();
    return exitCode;
}
