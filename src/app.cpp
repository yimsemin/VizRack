#include "app.h"

#include "core/plugin_storage.h"
#include "core/utf.h"
#include "ui/main_window.h"
#include "vst/plugin_catalog.h"
#include "vst/plugin_discovery.h"

#include <windows.h>
#include <dbghelp.h>
#include <winternl.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <format>
#include <optional>

namespace vizrack {
namespace {

std::filesystem::path gCrashDirectory;

LONG WINAPI writeCrashDump(EXCEPTION_POINTERS* exception) {
    if (gCrashDirectory.empty()) return EXCEPTION_EXECUTE_HANDLER;
    SYSTEMTIME now{};
    GetLocalTime(&now);
    wchar_t name[96]{};
    swprintf_s(name, L"crash-%04u%02u%02u-%02u%02u%02u.dmp", now.wYear, now.wMonth,
               now.wDay, now.wHour, now.wMinute, now.wSecond);
    const auto path = gCrashDirectory / name;
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION information{};
        information.ThreadId = GetCurrentThreadId();
        information.ExceptionPointers = exception;
        information.ClientPointers = FALSE;
        MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file,
                          MiniDumpWithIndirectlyReferencedMemory, &information, nullptr, nullptr);
        CloseHandle(file);
    }
    return EXCEPTION_EXECUTE_HANDLER;
}

std::string windowsVersion() {
    using RtlGetVersion = LONG(WINAPI*)(PRTL_OSVERSIONINFOW);
    const auto module = GetModuleHandleW(L"ntdll.dll");
    const auto getVersion = reinterpret_cast<RtlGetVersion>(GetProcAddress(module, "RtlGetVersion"));
    RTL_OSVERSIONINFOW version{};
    version.dwOSVersionInfoSize = sizeof(version);
    if (getVersion && getVersion(&version) == 0) {
        return std::to_string(version.dwMajorVersion) + "." +
               std::to_string(version.dwMinorVersion) + "." +
               std::to_string(version.dwBuildNumber);
    }
    return "unknown";
}

std::string architectureName() {
    SYSTEM_INFO info{};
    GetNativeSystemInfo(&info);
    switch (info.wProcessorArchitecture) {
        case PROCESSOR_ARCHITECTURE_AMD64: return "x64";
        case PROCESSOR_ARCHITECTURE_ARM64: return "ARM64";
        case PROCESSOR_ARCHITECTURE_INTEL: return "x86";
        default: return "unknown";
    }
}

void mergeMainWindowSettings(Settings& destination, const Settings& source) {
    destination.followDefaultDevice = source.followDefaultDevice;
    destination.fixedDeviceId = source.fixedDeviceId;
    destination.selectedPluginId = source.selectedPluginId;
    destination.windowX = source.windowX;
    destination.windowY = source.windowY;
    destination.windowWidth = source.windowWidth;
    destination.windowHeight = source.windowHeight;
    destination.alwaysOnTop = source.alwaysOnTop;
    destination.borderless = source.borderless;
    destination.opacityPercent = source.opacityPercent;
}

std::string editionForStatus(const PluginDefinition& definition,
                             const PluginDescriptor& descriptor) {
    if (definition.id != "mvmeter2") return definition.editionLabel;
    std::string identity = descriptor.name + " " +
                           toUtf8(descriptor.modulePath.filename().wstring());
    std::transform(identity.begin(), identity.end(), identity.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return identity.find("nogpu") != std::string::npos ? "noGPU" : "GPU";
}

} // namespace

App::App(HINSTANCE instance)
    : instance_(instance), vstHost_(audioRing_, logger_), oscilloscope_(audioRing_),
      artVisualizer_(audioRing_), campfire_(audioRing_), capture_(audioRing_, logger_) {}

App::~App() { shutdown(); }

bool App::initialize(std::string& error) {
    try {
        paths_ = PortablePaths::discover();
    } catch (const std::exception& exception) {
        error = exception.what();
        return false;
    }
    paths_.prepare();
    portableWritable_ = paths_.writable;
    if (portableWritable_) {
        logger_.open(paths_.logs);
        gCrashDirectory = paths_.data;
        SetUnhandledExceptionFilter(writeCrashDump);
    }
    logEnvironment();

    const auto loadedSettings = loadSettings(paths_.settings);
    settings_ = loadedSettings.value;
    if (!loadedSettings.warning.empty()) logger_.warning(loadedSettings.warning);
    if (!findPluginDefinition(settings_.selectedPluginId)) {
        settings_.selectedPluginId = pluginCatalog().front().id;
    }
    oscilloscope_.configure(
        {settings_.oscilloscopeFps, settings_.oscilloscopeScalePercent,
         settings_.oscilloscopeSmoothing, settings_.oscilloscopeHistoryMode},
        [this](const OscilloscopeOptions& options) {
            settings_.oscilloscopeFps = options.fps;
            settings_.oscilloscopeScalePercent = options.scalePercent;
            settings_.oscilloscopeSmoothing = options.smoothing;
            settings_.oscilloscopeHistoryMode = options.historyMode;
            saveSettingsNow();
        });
    artVisualizer_.configure(
        {settings_.artScene, settings_.artPalette},
        [this](const ArtVisualizerOptions& options) {
            settings_.artScene = options.scene;
            settings_.artPalette = options.palette;
            saveSettingsNow();
        });
    campfire_.configure(
        {settings_.campfireFlameResponse,
         settings_.campfireStarSpeed,
         settings_.campfireStarBrightness,
         settings_.campfireStarResponse,
         settings_.campfireParticleAmount,
         settings_.campfireParticleIntensity},
        [this](const CampfireOptions& options) {
            settings_.campfireFlameResponse = options.flameResponse;
            settings_.campfireStarSpeed = options.starSpeed;
            settings_.campfireStarBrightness = options.starBrightness;
            settings_.campfireStarResponse = options.starResponse;
            settings_.campfireParticleAmount = options.particleAmount;
            settings_.campfireParticleIntensity = options.particleIntensity;
            saveSettingsNow();
        });
    std::wstring fixedDevice;
    try {
        fixedDevice = fromUtf8(settings_.fixedDeviceId);
    } catch (...) {
        logger_.warning("Saved output device ID is not valid UTF-8; following the default device");
        settings_.fixedDeviceId.clear();
        settings_.followDefaultDevice = true;
    }

    MainWindowCallbacks callbacks;
    callbacks.onExit = [this] { shutdown(); };
    callbacks.onDeviceSelection = [this](bool followDefault, const std::wstring& id) {
        settings_.followDefaultDevice = followDefault;
        settings_.fixedDeviceId = toUtf8(id);
        if (captureStarted_) capture_.setDeviceSelection(followDefault, id);
    };
    callbacks.onPluginSelection = [this](const std::string& pluginId) {
        selectPlugin(pluginId);
    };
    callbacks.onPluginPathSelected =
        [this](const std::string& pluginId, const std::filesystem::path& path) {
            selectPluginPath(pluginId, path);
        };
    callbacks.onSettingsChanged = [this](const Settings& settings) {
        mergeMainWindowSettings(settings_, settings);
        saveSettingsNow();
    };
    callbacks.onResume = [this] {
        logger_.info("Power resume detected; requesting audio reinitialization");
        capture_.requestReconnect("절전 모드 복귀");
    };
    window_ = std::make_unique<MainWindow>(instance_, settings_, std::move(callbacks));
    if (!window_->create(error)) return false;
    window_->setEditorConstraintHandler([this](int width, int height) {
        if (builtInViewActive()) return std::pair{width, height};
        return vstHost_.constrainEditorSize(width, height);
    });
    window_->setEditorResizeHandler([this](int width, int height) {
        vstHost_.resizeEditor(width, height);
        oscilloscope_.resize(width, height);
        artVisualizer_.resize(width, height);
        campfire_.resize(width, height);
    });
    window_->setEditorScaleHandler([this](float scale) { vstHost_.setEditorContentScale(scale); });

    if (!portableWritable_) {
        window_->setPluginStatus("포터블 데이터 폴더 쓰기 불가 — 설정과 로그를 저장하지 않음");
    }
    loadInitialPlugin();

    captureStarted_ = capture_.start(
        settings_.followDefaultDevice, std::move(fixedDevice),
        [this](const CaptureStatus& status) {
            if (window_) window_->postCaptureStatus(status);
        },
        [this](uint32_t sampleRate) {
            sampleRate_.store(sampleRate, std::memory_order_release);
            vstHost_.setSampleRate(sampleRate);
            oscilloscope_.setSampleRate(sampleRate);
            artVisualizer_.setSampleRate(sampleRate);
            campfire_.setSampleRate(sampleRate);
        },
        [this] { vstHost_.notifyDataReady(); });
    if (!captureStarted_) {
        error = "WASAPI 캡처 스레드를 시작하지 못했습니다.";
        return false;
    }
    return true;
}

int App::run(int commandShow) {
    window_->show(commandShow);
    const bool smokeTest = wcsstr(GetCommandLineW(), L"--smoke-test") != nullptr;
    if (smokeTest) {
        window_->scheduleClose(4000);
    }
    if (!portableWritable_) {
        MessageBoxW(window_->handle(), fromUtf8(paths_.writeError).c_str(),
                    L"포터블 저장소 쓰기 불가", MB_OK | MB_ICONWARNING);
    }
    if (pluginSelectionNeeded_ && !smokeTest) {
        const auto* definition = findPluginDefinition(settings_.selectedPluginId);
        const std::wstring displayName = definition ? fromUtf8(definition->displayName) : L"VST3";
        const std::wstring message = displayName +
            L"를 찾지 못했습니다. 먼저 Windows x64 VST3 플러그인을 설치해야 합니다.\n\n"
            L"[예] 공식 설치 페이지 열기\n"
            L"[아니요] 이미 설치한 .vst3 파일 직접 선택\n"
            L"[취소] 나중에 플러그인 메뉴에서 선택";
        const int choice = MessageBoxW(
            window_->handle(), message.c_str(), L"지원 플러그인 필요",
            MB_YESNOCANCEL | MB_ICONINFORMATION);
        if (choice == IDYES && definition) {
            window_->openPluginInstallPage(definition->id);
        } else if (choice == IDNO && definition) {
            window_->openPluginPicker(definition->id, false);
        }
    }
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return static_cast<int>(message.wParam);
}

void App::loadInitialPlugin() {
    const auto* definition = findPluginDefinition(settings_.selectedPluginId);
    if (!definition) return;
    if (definition->kind == PluginKind::builtIn) {
        std::string error;
        if (!activateBuiltInPlugin(*definition, error)) {
            logger_.error(definition->displayName + " activation failed: " + error);
            window_->setPluginStatus(error);
            return;
        }
        commitPluginSelection(*definition);
        return;
    }
    const auto storage = pluginStoragePaths(paths_.plugins, definition->id);
    std::string locationWarning;
    const std::string savedPath = loadPluginLocation(storage.location, locationWarning);
    if (!locationWarning.empty()) logger_.warning(locationWarning);
    const auto inspection = findSupportedPlugin(*definition, savedPath);
    if (!inspection.compatible) {
        logger_.warning(definition->displayName + " discovery failed: " + inspection.error);
        window_->setPluginStatus(inspection.error);
        pluginSelectionNeeded_ = true;
        return;
    }
    std::string error;
    if (!activatePlugin(*definition, inspection.descriptor, error)) {
        logger_.error(definition->displayName + " activation failed: " + error);
        window_->setPluginStatus(error);
        pluginSelectionNeeded_ = true;
        return;
    }
    commitPluginSelection(*definition, &inspection.descriptor);
}

bool App::activatePlugin(const PluginDefinition& definition,
                         const PluginDescriptor& descriptor, std::string& error) {
    std::optional<PluginDescriptor> previousDescriptor;
    const PluginDefinition* previousDefinition = nullptr;
    if (vstHost_.loaded()) {
        previousDescriptor = vstHost_.descriptor();
        previousDefinition = findPluginDefinition(previousDescriptor->definitionId);
    }
    const PluginDefinition* previousBuiltIn = activeBuiltInDefinition();
    window_->setExternalResizeEnabled(true);
    detachBuiltInViews();
    if (vstHost_.loaded()) {
        vstHost_.stopProcessing();
        saveCurrentPluginState();
        vstHost_.detachEditor();
        vstHost_.unload();
    }
    if (startPluginInstance(definition, descriptor, error)) return true;

    const std::string targetError = error;
    if (previousDescriptor && previousDefinition) {
        std::string rollbackError;
        if (startPluginInstance(*previousDefinition, *previousDescriptor, rollbackError)) {
            window_->setSelectedPluginId(previousDefinition->id);
            error = targetError + " 이전 플러그인으로 복구했습니다.";
            return false;
        }
        error = targetError + " 이전 플러그인 복구도 실패했습니다: " + rollbackError;
    } else if (previousBuiltIn) {
        std::string rollbackError;
        if (startBuiltInPlugin(*previousBuiltIn, rollbackError)) {
            window_->setSelectedPluginId(previousBuiltIn->id);
            error = targetError + " 이전 내장 플러그인으로 복구했습니다.";
            return false;
        }
        error = targetError + " 이전 내장 플러그인 복구도 실패했습니다: " + rollbackError;
    }
    window_->setExternalResizeEnabled(true);
    return false;
}

bool App::activateBuiltInPlugin(const PluginDefinition& definition, std::string& error) {
    std::optional<PluginDescriptor> previousDescriptor;
    const PluginDefinition* previousDefinition = nullptr;
    const PluginDefinition* previousBuiltIn = activeBuiltInDefinition();
    if (vstHost_.loaded()) {
        previousDescriptor = vstHost_.descriptor();
        previousDefinition = findPluginDefinition(previousDescriptor->definitionId);
        vstHost_.stopProcessing();
        saveCurrentPluginState();
        vstHost_.detachEditor();
        vstHost_.unload();
    }
    detachBuiltInViews();
    if (startBuiltInPlugin(definition, error)) return true;

    const std::string targetError = error;
    if (previousDescriptor && previousDefinition) {
        std::string rollbackError;
        if (startPluginInstance(*previousDefinition, *previousDescriptor, rollbackError)) {
            window_->setSelectedPluginId(previousDefinition->id);
            error = targetError + " 이전 VST3 플러그인으로 복구했습니다.";
            return false;
        }
        error = targetError + " 이전 VST3 플러그인 복구도 실패했습니다: " + rollbackError;
    } else if (previousBuiltIn) {
        std::string rollbackError;
        if (startBuiltInPlugin(*previousBuiltIn, rollbackError)) {
            window_->setSelectedPluginId(previousBuiltIn->id);
            error = targetError + " 이전 내장 플러그인으로 복구했습니다.";
            return false;
        }
        error = targetError + " 이전 내장 플러그인 복구도 실패했습니다: " + rollbackError;
    }
    return false;
}

bool App::startBuiltInPlugin(const PluginDefinition& definition, std::string& error) {
    window_->setExternalResizeEnabled(true);
    bool attached = false;
    if (definition.id == "builtin-oscilloscope") {
        attached = oscilloscope_.attach(instance_, window_->pluginParent(), error);
    } else if (definition.id == "builtin-art-visualizer") {
        attached = artVisualizer_.attach(instance_, window_->pluginParent(), error);
    } else if (definition.id == "builtin-campfire") {
        attached = campfire_.attach(instance_, window_->pluginParent(), error);
    } else {
        error = "지원하지 않는 내장 플러그인입니다: " + definition.id;
    }
    if (!attached) return false;
    const uint32_t sampleRate = sampleRate_.load(std::memory_order_acquire);
    oscilloscope_.setSampleRate(sampleRate);
    artVisualizer_.setSampleRate(sampleRate);
    campfire_.setSampleRate(sampleRate);
    activeBuiltInPluginId_ = definition.id;
    window_->setPluginStatus(definition.displayName);
    pluginSelectionNeeded_ = false;
    logger_.info("Built-in visualization activated: id='" + definition.id + "'");
    return true;
}

bool App::startPluginInstance(const PluginDefinition& definition,
                              const PluginDescriptor& descriptor, std::string& error) {
    const auto storage = pluginStoragePaths(paths_.plugins, definition.id);
    const auto loadResult = vstHost_.load(descriptor, storage.state);
    if (!loadResult.loaded) {
        error = loadResult.message;
        return false;
    }
    if (!vstHost_.attachEditor(
            window_->pluginParent(),
            [this](int width, int height) {
                if (window_) window_->onPluginRequestedSize(width, height);
            },
            error)) {
        vstHost_.unload();
        return false;
    }
    vstHost_.setEditorContentScale(static_cast<float>(GetDpiForWindow(window_->handle())) / 96.0f);
    window_->setExternalResizeEnabled(vstHost_.editorHostResizable());
    const auto [editorWidth, editorHeight] = vstHost_.editorSize();
    if (editorWidth > 0 && editorHeight > 0) {
        window_->onPluginRequestedSize(editorWidth, editorHeight);
    }
    if (!vstHost_.startProcessing(sampleRate_.load(std::memory_order_acquire))) {
        error = definition.displayName + " 처리 스레드를 시작하지 못했습니다.";
        vstHost_.unload();
        return false;
    }
    std::string status = descriptor.name + " " + descriptor.version;
    const std::string edition = editionForStatus(definition, descriptor);
    if (!edition.empty()) status += " (" + edition + ")";
    window_->setPluginStatus(std::move(status));
    pluginSelectionNeeded_ = false;
    return true;
}

void App::selectPlugin(const std::string& pluginId) {
    const auto* definition = findPluginDefinition(pluginId);
    if (!definition) return;
    if (definition->kind == PluginKind::builtIn) {
        std::string error;
        if (!activateBuiltInPlugin(*definition, error)) {
            MessageBoxW(window_->handle(), fromUtf8(error).c_str(), L"내장 플러그인 시작 실패",
                        MB_OK | MB_ICONERROR);
            return;
        }
        commitPluginSelection(*definition);
        return;
    }
    const auto storage = pluginStoragePaths(paths_.plugins, definition->id);
    std::string warning;
    const auto savedPath = loadPluginLocation(storage.location, warning);
    if (!warning.empty()) logger_.warning(warning);
    const auto inspection = findSupportedPlugin(*definition, savedPath);
    if (!inspection.compatible) {
        logger_.warning(definition->displayName + " selection failed: " + inspection.error);
        MessageBoxW(window_->handle(), fromUtf8(inspection.error).c_str(),
                    L"지원 플러그인을 찾지 못함", MB_OK | MB_ICONERROR);
        return;
    }
    std::string error;
    if (!activatePlugin(*definition, inspection.descriptor, error)) {
        MessageBoxW(window_->handle(), fromUtf8(error).c_str(), L"VST3 로딩 실패",
                    MB_OK | MB_ICONERROR);
        return;
    }
    commitPluginSelection(*definition, &inspection.descriptor);
}

void App::selectPluginPath(const std::string& pluginId, const std::filesystem::path& path) {
    const auto* definition = findPluginDefinition(pluginId);
    if (!definition || definition->kind != PluginKind::vst3) return;
    const auto inspection = inspectSupportedPlugin(*definition, path);
    if (!inspection.compatible) {
        logger_.warning("Manual plug-in selection rejected: " + inspection.error);
        MessageBoxW(window_->handle(), fromUtf8(inspection.error).c_str(),
                    L"호환되지 않는 VST3", MB_OK | MB_ICONERROR);
        return;
    }
    std::string error;
    if (!activatePlugin(*definition, inspection.descriptor, error)) {
        MessageBoxW(window_->handle(), fromUtf8(error).c_str(), L"VST3 로딩 실패",
                    MB_OK | MB_ICONERROR);
        return;
    }
    commitPluginSelection(*definition, &inspection.descriptor);
}

void App::commitPluginSelection(const PluginDefinition& definition,
                                const PluginDescriptor* descriptor) {
    settings_.selectedPluginId = definition.id;
    window_->setSelectedPluginId(definition.id);
    if (portableWritable_ && descriptor) {
        const auto storage = pluginStoragePaths(paths_.plugins, definition.id);
        std::string error;
        if (!savePluginLocation(storage.location, descriptor->modulePath, error)) {
            logger_.warning(error);
        }
    }
    saveSettingsNow();
}

void App::saveCurrentPluginState() {
    if (!portableWritable_ || !vstHost_.loaded()) return;
    const auto& descriptor = vstHost_.descriptor();
    const auto storage = pluginStoragePaths(paths_.plugins, descriptor.definitionId);
    std::string error;
    if (!vstHost_.saveState(storage.state, error)) logger_.warning(error);
}

void App::saveSettingsNow() {
    if (!portableWritable_) return;
    std::string error;
    if (!saveSettings(paths_.settings, settings_, error)) logger_.warning(error);
}

void App::logEnvironment() {
    logger_.info("VizRack version=" VIZRACK_VERSION);
    logger_.info("Windows version=" + windowsVersion() + " architecture=" + architectureName());
    logger_.info("Executable path='" + toUtf8(paths_.executable.wstring()) + "'");
    if (!paths_.writable) logger_.warning(paths_.writeError);
}

bool App::builtInViewActive() const noexcept {
    return oscilloscope_.active() || artVisualizer_.active() || campfire_.active();
}

const PluginDefinition* App::activeBuiltInDefinition() const {
    return builtInViewActive() ? findPluginDefinition(activeBuiltInPluginId_) : nullptr;
}

void App::detachBuiltInViews() {
    oscilloscope_.detach();
    artVisualizer_.detach();
    campfire_.detach();
    activeBuiltInPluginId_.clear();
}

void App::shutdown() {
    if (shuttingDown_.exchange(true)) return;
    logger_.info("Application shutdown started");
    if (captureStarted_) {
        capture_.stop();
        captureStarted_ = false;
    }
    vstHost_.stopProcessing();
    saveCurrentPluginState();
    vstHost_.detachEditor();
    vstHost_.unload();
    detachBuiltInViews();
    if (window_) mergeMainWindowSettings(settings_, window_->settings());
    saveSettingsNow();
    logger_.info("Application shutdown completed");
}

} // namespace vizrack
