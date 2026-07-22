#pragma once

#include "core/audio_ring.h"
#include "core/logger.h"
#include "core/portable_paths.h"
#include "core/settings.h"
#include "platform/wasapi_capture.h"
#include "ui/art_visualizer_view.h"
#include "ui/oscilloscope_view.h"
#include "vst/vst_host.h"

#include <atomic>
#include <filesystem>
#include <memory>
#include <string>

#include <windows.h>

namespace vizrack {

class MainWindow;

class App {
public:
    explicit App(HINSTANCE instance);
    ~App();

    bool initialize(std::string& error);
    int run(int commandShow);
    void shutdown();

private:
    bool activatePlugin(const PluginDefinition& definition, const PluginDescriptor& descriptor,
                        std::string& error);
    bool activateBuiltInPlugin(const PluginDefinition& definition, std::string& error);
    bool startPluginInstance(const PluginDefinition& definition,
                             const PluginDescriptor& descriptor, std::string& error);
    bool startBuiltInPlugin(const PluginDefinition& definition, std::string& error);
    void loadInitialPlugin();
    void selectPlugin(const std::string& pluginId);
    void selectPluginPath(const std::string& pluginId, const std::filesystem::path& path);
    void commitPluginSelection(const PluginDefinition& definition,
                               const PluginDescriptor* descriptor = nullptr);
    void saveCurrentPluginState();
    void saveSettingsNow();
    void logEnvironment();
    bool builtInViewActive() const noexcept;
    const PluginDefinition* activeBuiltInDefinition() const;
    void detachBuiltInViews();

    HINSTANCE instance_{};
    PortablePaths paths_;
    Settings settings_;
    Logger logger_;
    StereoFrameRing audioRing_{65536};
    VstHost vstHost_;
    OscilloscopeView oscilloscope_;
    ArtVisualizerView artVisualizer_;
    WasapiCapture capture_;
    std::unique_ptr<MainWindow> window_;
    std::atomic<uint32_t> sampleRate_{48000};
    bool portableWritable_{false};
    bool pluginSelectionNeeded_{false};
    bool captureStarted_{false};
    std::string activeBuiltInPluginId_;
    std::atomic<bool> shuttingDown_{false};
};

} // namespace vizrack
