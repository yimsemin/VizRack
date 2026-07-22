#pragma once

#include "vst/plugin_discovery.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <utility>

struct HWND__;
using HWND = HWND__*;

namespace vizrack {

class Logger;
class StereoFrameRing;

struct VstLoadResult {
    bool loaded{false};
    std::string message;
};

class VstHost {
public:
    using EditorResizeCallback = std::function<void(int width, int height)>;

    VstHost(StereoFrameRing& ring, Logger& logger);
    ~VstHost();

    VstHost(const VstHost&) = delete;
    VstHost& operator=(const VstHost&) = delete;

    VstLoadResult load(const PluginDescriptor& descriptor,
                       const std::filesystem::path& statePath);
    void unload();
    bool startProcessing(uint32_t sampleRate);
    void stopProcessing();
    void setSampleRate(uint32_t sampleRate);
    void notifyDataReady();

    bool attachEditor(HWND parent, EditorResizeCallback resizeCallback, std::string& error);
    void detachEditor();
    void resizeEditor(int width, int height);
    std::pair<int, int> constrainEditorSize(int width, int height) const;
    bool editorHostResizable() const;
    void setEditorContentScale(float scale);
    std::pair<int, int> editorSize() const;

    bool saveState(const std::filesystem::path& path, std::string& error);
    bool loaded() const noexcept;
    const PluginDescriptor& descriptor() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace vizrack
