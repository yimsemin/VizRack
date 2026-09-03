#include "app.h"

#include "core/i18n.h"
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
    destination.uiLanguage = source.uiLanguage;
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
      artVisualizer_(audioRing_), campfire_(audioRing_), spectrum3d_(audioRing_, 0),
      joyDivision_(audioRing_, 1), capture_(audioRing_, logger_) {}

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
    setUiLanguage(resolveUiLanguage(settings_.uiLanguage));
    logger_.info("UI language: " + std::string(uiLanguageToken(currentUiLanguage())) +
                 " (preference '" + settings_.uiLanguage + "')");
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
    spectrum3d_.configure(
        {0, settings_.spectrum3dPalette, settings_.spectrum3dRotation,
         settings_.spectrum3dTilt, settings_.spectrum3dDepth, settings_.spectrum3dHeight},
        [this](const Spectrum3dOptions& options) {
            settings_.spectrum3dPalette = options.palette;
            settings_.spectrum3dRotation = options.rotation;
            settings_.spectrum3dTilt = options.tilt;
            settings_.spectrum3dDepth = options.depth;
            settings_.spectrum3dHeight = options.heightScale;
            saveSettingsNow();
        });
    joyDivision_.configure(
        {1, settings_.joyDivisionPalette, settings_.joyDivisionRotation,
         settings_.joyDivisionTilt, settings_.joyDivisionDepth, settings_.joyDivisionHeight},
        [this](const Spectrum3dOptions& options) {
            settings_.joyDivisionPalette = options.palette;
            settings_.joyDivisionRotation = options.rotation;
            settings_.joyDivisionTilt = options.tilt;
            settings_.joyDivisionDepth = options.depth;
            settings_.joyDivisionHeight = options.heightScale;
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
        capture_.requestReconnect("power resume");
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
        spectrum3d_.resize(width, height);
        joyDivision_.resize(width, height);
    });
    window_->setEditorScaleHandler([this](float scale) { vstHost_.setEditorContentScale(scale); });

    if (!portableWritable_) {
        window_->setPluginStatus(tr(Str::StatusPortableReadOnly));
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
            spectrum3d_.setSampleRate(sampleRate);
            joyDivision_.setSampleRate(sampleRate);
        },
        [this] { vstHost_.notifyDataReady(); });
    if (!captureStarted_) {
        error = "Failed to start the WASAPI capture thread.";
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
                    trw(Str::DialogTitlePortableStorage).c_str(), MB_OK | MB_ICONWARNING);
    }
    if (pluginSelectionNeeded_ && !smokeTest) {
        const auto* definition = findPluginDefinition(settings_.selectedPluginId);
        const std::wstring displayName = definition ? fromUtf8(definition->displayName) : L"VST3";
        const std::wstring message =
            std::vformat(trw(Str::DialogBodyPluginRequiredFmt), std::make_wformat_args(displayName));
        const int choice = MessageBoxW(
            window_->handle(), message.c_str(), trw(Str::DialogTitlePluginRequired).c_str(),
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
            error = targetError + " Recovered the previous plug-in.";
            return false;
        }
        error = targetError + " Recovery of the previous plug-in also failed: " + rollbackError;
    } else if (previousBuiltIn) {
        std::string rollbackError;
        if (startBuiltInPlugin(*previousBuiltIn, rollbackError)) {
            window_->setSelectedPluginId(previousBuiltIn->id);
            error = targetError + " Recovered the previous built-in visualizer.";
            return false;
        }
        error = targetError +
                " Recovery of the previous built-in visualizer also failed: " + rollbackError;
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
            error = targetError + " Recovered the previous VST3 plug-in.";
            return false;
        }
        error = targetError + " Recovery of the previous VST3 plug-in also failed: " + rollbackError;
    } else if (previousBuiltIn) {
        std::string rollbackError;
        if (startBuiltInPlugin(*previousBuiltIn, rollbackError)) {
            window_->setSelectedPluginId(previousBuiltIn->id);
            error = targetError + " Recovered the previous built-in visualizer.";
            return false;
        }
        error = targetError +
                " Recovery of the previous built-in visualizer also failed: " + rollbackError;
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
    } else if (definition.id == "builtin-spectrum3d") {
        attached = spectrum3d_.attach(instance_, window_->pluginParent(), error);
    } else if (definition.id == "builtin-joydivision") {
        attached = joyDivision_.attach(instance_, window_->pluginParent(), error);
    } else {
        error = "Unsupported built-in visualizer: " + definition.id;
    }
    if (!attached) return false;
    const uint32_t sampleRate = sampleRate_.load(std::memory_order_acquire);
    oscilloscope_.setSampleRate(sampleRate);
    artVisualizer_.setSampleRate(sampleRate);
    campfire_.setSampleRate(sampleRate);
    spectrum3d_.setSampleRate(sampleRate);
    joyDivision_.setSampleRate(sampleRate);
    activeBuiltInPluginId_ = definition.id;
    std::string status = definition.displayName;
    if (!definition.inspiration.empty()) status += "  —  " + definition.inspiration;
    window_->setPluginStatus(std::move(status));
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
        error = "Failed to start the processing thread for " + definition.displayName + ".";
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
            MessageBoxW(window_->handle(), fromUtf8(error).c_str(),
                        trw(Str::DialogTitleBuiltinStartFailed).c_str(), MB_OK | MB_ICONERROR);
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
                    trw(Str::DialogTitleSupportedPluginMissing).c_str(), MB_OK | MB_ICONERROR);
        return;
    }
    std::string error;
    if (!activatePlugin(*definition, inspection.descriptor, error)) {
        MessageBoxW(window_->handle(), fromUtf8(error).c_str(),
                    trw(Str::DialogTitleVst3LoadFailed).c_str(), MB_OK | MB_ICONERROR);
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
                    trw(Str::DialogTitleIncompatibleVst3).c_str(), MB_OK | MB_ICONERROR);
        return;
    }
    std::string error;
    if (!activatePlugin(*definition, inspection.descriptor, error)) {
        MessageBoxW(window_->handle(), fromUtf8(error).c_str(),
                    trw(Str::DialogTitleVst3LoadFailed).c_str(), MB_OK | MB_ICONERROR);
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
    return oscilloscope_.active() || artVisualizer_.active() || campfire_.active() ||
           spectrum3d_.active() || joyDivision_.active();
}

const PluginDefinition* App::activeBuiltInDefinition() const {
    return builtInViewActive() ? findPluginDefinition(activeBuiltInPluginId_) : nullptr;
}

void App::detachBuiltInViews() {
    oscilloscope_.detach();
    artVisualizer_.detach();
    campfire_.detach();
    spectrum3d_.detach();
    joyDivision_.detach();
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
