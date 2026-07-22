#pragma once

#include "core/reconnect_policy.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace vizrack {

class Logger;
class StereoFrameRing;

struct CaptureFormatInfo {
    uint32_t sampleRate{0};
    uint32_t channels{0};
    uint32_t channelMask{0};
    uint32_t bitsPerSample{0};
    std::string sampleFormat;
};

struct CaptureStatus {
    CaptureState state{CaptureState::stopped};
    std::wstring deviceName;
    std::wstring deviceId;
    CaptureFormatInfo format;
    std::string detail;
};

class WasapiCapture {
public:
    using StatusCallback = std::function<void(const CaptureStatus&)>;
    using FormatCallback = std::function<void(uint32_t)>;
    using DataReadyCallback = std::function<void()>;

    WasapiCapture(StereoFrameRing& ring, Logger& logger);
    ~WasapiCapture();

    WasapiCapture(const WasapiCapture&) = delete;
    WasapiCapture& operator=(const WasapiCapture&) = delete;

    bool start(bool followDefault, std::wstring fixedDeviceId, StatusCallback statusCallback,
               FormatCallback formatCallback, DataReadyCallback dataReadyCallback);
    void setDeviceSelection(bool followDefault, std::wstring fixedDeviceId);
    void requestReconnect(std::string reason);
    void stop();

private:
    class NotificationClient;
    void run();
    bool shouldReactToDefaultChange() const;
    bool shouldReactToDevice(const wchar_t* deviceId) const;
    void signalDeviceChange(const char* reason);

    StereoFrameRing& ring_;
    Logger& logger_;
    mutable std::mutex selectionMutex_;
    bool followDefault_{true};
    std::wstring fixedDeviceId_;
    std::string reconnectReason_;
    StatusCallback statusCallback_;
    FormatCallback formatCallback_;
    DataReadyCallback dataReadyCallback_;
    void* stopEvent_{nullptr};
    void* deviceChangeEvent_{nullptr};
    std::thread thread_;
    std::atomic<bool> running_{false};
};

} // namespace vizrack

