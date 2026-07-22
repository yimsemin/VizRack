#include "platform/wasapi_capture.h"

#include "core/audio_ring.h"
#include "core/channel_mapper.h"
#include "core/logger.h"
#include "core/utf.h"

#include <windows.h>
#include <audioclient.h>
#include <avrt.h>
#include <ksmedia.h>
#include <propsys.h>
#include <functiondiscoverykeys_devpkey.h>
#include <mmdeviceapi.h>
#include <propvarutil.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdio>
#include <vector>

namespace vizrack {
namespace {

using Microsoft::WRL::ComPtr;

std::string hrText(HRESULT result) {
    char code[16]{};
    std::snprintf(code, sizeof(code), "%08lX", static_cast<unsigned long>(result));
    return "HRESULT 0x" + std::string(code) + " (" +
           formatWindowsError(static_cast<unsigned long>(result)) + ")";
}

std::wstring deviceName(IMMDevice* device) {
    ComPtr<IPropertyStore> properties;
    if (FAILED(device->OpenPropertyStore(STGM_READ, &properties))) return L"알 수 없는 출력 장치";
    PROPVARIANT value;
    PropVariantInit(&value);
    std::wstring name = L"알 수 없는 출력 장치";
    if (SUCCEEDED(properties->GetValue(PKEY_Device_FriendlyName, &value)) &&
        value.vt == VT_LPWSTR && value.pwszVal) {
        name = value.pwszVal;
    }
    PropVariantClear(&value);
    return name;
}

std::wstring deviceId(IMMDevice* device) {
    LPWSTR raw = nullptr;
    if (FAILED(device->GetId(&raw))) return {};
    std::wstring id = raw;
    CoTaskMemFree(raw);
    return id;
}

enum class Encoding { float32, pcm16, pcm24, pcm32, unsupported };

struct ParsedFormat {
    Encoding encoding{Encoding::unsupported};
    uint32_t sampleRate{};
    uint32_t channels{};
    uint32_t channelMask{};
    uint32_t bytesPerSample{};
    uint32_t validBits{};
    std::string name;
};

ParsedFormat parseFormat(const WAVEFORMATEX* format) {
    ParsedFormat result;
    result.sampleRate = format->nSamplesPerSec;
    result.channels = format->nChannels;
    result.bytesPerSample = format->wBitsPerSample / 8;
    result.validBits = format->wBitsPerSample;
    WORD tag = format->wFormatTag;
    GUID subFormat{};
    if (tag == WAVE_FORMAT_EXTENSIBLE && format->cbSize >= 22) {
        const auto* extensible = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(format);
        result.channelMask = extensible->dwChannelMask;
        result.validBits = extensible->Samples.wValidBitsPerSample;
        subFormat = extensible->SubFormat;
        if (subFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT) tag = WAVE_FORMAT_IEEE_FLOAT;
        if (subFormat == KSDATAFORMAT_SUBTYPE_PCM) tag = WAVE_FORMAT_PCM;
    }
    if (tag == WAVE_FORMAT_IEEE_FLOAT && format->wBitsPerSample == 32) {
        result.encoding = Encoding::float32;
        result.name = "32-bit float";
    } else if (tag == WAVE_FORMAT_PCM && format->wBitsPerSample == 16) {
        result.encoding = Encoding::pcm16;
        result.name = "16-bit PCM";
    } else if (tag == WAVE_FORMAT_PCM && format->wBitsPerSample == 24) {
        result.encoding = Encoding::pcm24;
        result.name = "24-bit PCM";
    } else if (tag == WAVE_FORMAT_PCM && format->wBitsPerSample == 32) {
        result.encoding = Encoding::pcm32;
        result.name = std::to_string(result.validBits) + "-in-32-bit PCM";
    } else {
        result.name = "unsupported format tag " + std::to_string(tag);
    }
    return result;
}

float readSample(const BYTE* frame, uint32_t channel, const ParsedFormat& format) {
    const BYTE* source = frame + channel * format.bytesPerSample;
    switch (format.encoding) {
        case Encoding::float32:
            return *reinterpret_cast<const float*>(source);
        case Encoding::pcm16:
            return static_cast<float>(*reinterpret_cast<const int16_t*>(source)) / 32768.0f;
        case Encoding::pcm24: {
            int32_t value = static_cast<int32_t>(source[0]) |
                            (static_cast<int32_t>(source[1]) << 8) |
                            (static_cast<int32_t>(source[2]) << 16);
            if (value & 0x00800000) value |= static_cast<int32_t>(0xff000000);
            return static_cast<float>(value) / 8388608.0f;
        }
        case Encoding::pcm32: {
            int32_t value = *reinterpret_cast<const int32_t*>(source);
            const uint32_t validBits = std::clamp(format.validBits, 1u, 32u);
            if (validBits < 32) value >>= (32 - validBits);
            const double divisor = std::ldexp(1.0, static_cast<int>(validBits - 1));
            return static_cast<float>(static_cast<double>(value) / divisor);
        }
        case Encoding::unsupported: break;
    }
    return 0.0f;
}

struct CaptureSession {
    ComPtr<IMMDevice> device;
    ComPtr<IAudioClient> client;
    ComPtr<IAudioCaptureClient> capture;
    WAVEFORMATEX* mixFormat{nullptr};
    ParsedFormat parsed;
    ChannelSelection selection;
    HANDLE sampleEvent{nullptr};
    UINT32 bufferFrames{0};

    ~CaptureSession() { close(); }
    void close() {
        if (client) client->Stop();
        capture.Reset();
        client.Reset();
        device.Reset();
        if (mixFormat) CoTaskMemFree(mixFormat);
        mixFormat = nullptr;
        if (sampleEvent) CloseHandle(sampleEvent);
        sampleEvent = nullptr;
    }
};

} // namespace

class WasapiCapture::NotificationClient final : public IMMNotificationClient {
public:
    explicit NotificationClient(WasapiCapture& owner) : owner_(owner) {}

    ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG value = --references_;
        if (value == 0) delete this;
        return value;
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        if (iid == __uuidof(IUnknown) || iid == __uuidof(IMMNotificationClient)) {
            *object = static_cast<IMMNotificationClient*>(this);
            AddRef();
            return S_OK;
        }
        *object = nullptr;
        return E_NOINTERFACE;
    }
    HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR id, DWORD) override {
        if (owner_.shouldReactToDevice(id)) owner_.signalDeviceChange("장치 상태 변경");
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR id) override {
        if (owner_.shouldReactToDevice(id)) owner_.signalDeviceChange("장치 연결");
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR id) override {
        if (owner_.shouldReactToDevice(id)) owner_.signalDeviceChange("장치 분리");
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(EDataFlow flow, ERole role, LPCWSTR) override {
        if (flow == eRender && role == eConsole && owner_.shouldReactToDefaultChange()) {
            owner_.signalDeviceChange("기본 출력 장치 변경");
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(LPCWSTR, const PROPERTYKEY) override {
        return S_OK;
    }

private:
    std::atomic<ULONG> references_{1};
    WasapiCapture& owner_;
};

WasapiCapture::WasapiCapture(StereoFrameRing& ring, Logger& logger) : ring_(ring), logger_(logger) {}

WasapiCapture::~WasapiCapture() { stop(); }

bool WasapiCapture::start(bool followDefault, std::wstring fixedDeviceId,
                          StatusCallback statusCallback, FormatCallback formatCallback,
                          DataReadyCallback dataReadyCallback) {
    if (running_.exchange(true)) return false;
    {
        std::scoped_lock lock(selectionMutex_);
        followDefault_ = followDefault;
        fixedDeviceId_ = std::move(fixedDeviceId);
    }
    statusCallback_ = std::move(statusCallback);
    formatCallback_ = std::move(formatCallback);
    dataReadyCallback_ = std::move(dataReadyCallback);
    stopEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    deviceChangeEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!stopEvent_ || !deviceChangeEvent_) {
        running_ = false;
        if (stopEvent_) CloseHandle(static_cast<HANDLE>(stopEvent_));
        if (deviceChangeEvent_) CloseHandle(static_cast<HANDLE>(deviceChangeEvent_));
        stopEvent_ = deviceChangeEvent_ = nullptr;
        return false;
    }
    thread_ = std::thread(&WasapiCapture::run, this);
    return true;
}

void WasapiCapture::setDeviceSelection(bool followDefault, std::wstring fixedDeviceId) {
    {
        std::scoped_lock lock(selectionMutex_);
        followDefault_ = followDefault;
        fixedDeviceId_ = std::move(fixedDeviceId);
        reconnectReason_ = "출력 장치 선택 변경";
    }
    if (deviceChangeEvent_) SetEvent(static_cast<HANDLE>(deviceChangeEvent_));
}

void WasapiCapture::requestReconnect(std::string reason) {
    {
        std::scoped_lock lock(selectionMutex_);
        reconnectReason_ = std::move(reason);
    }
    if (deviceChangeEvent_) SetEvent(static_cast<HANDLE>(deviceChangeEvent_));
}

void WasapiCapture::stop() {
    if (!running_.exchange(false)) return;
    if (stopEvent_) SetEvent(static_cast<HANDLE>(stopEvent_));
    if (thread_.joinable()) thread_.join();
    if (stopEvent_) CloseHandle(static_cast<HANDLE>(stopEvent_));
    if (deviceChangeEvent_) CloseHandle(static_cast<HANDLE>(deviceChangeEvent_));
    stopEvent_ = deviceChangeEvent_ = nullptr;
}

bool WasapiCapture::shouldReactToDefaultChange() const {
    std::scoped_lock lock(selectionMutex_);
    return followDefault_;
}

bool WasapiCapture::shouldReactToDevice(const wchar_t* id) const {
    std::scoped_lock lock(selectionMutex_);
    return followDefault_ || (id && fixedDeviceId_ == id);
}

void WasapiCapture::signalDeviceChange(const char* reason) {
    {
        std::scoped_lock lock(selectionMutex_);
        reconnectReason_ = reason;
    }
    if (deviceChangeEvent_) SetEvent(static_cast<HANDLE>(deviceChangeEvent_));
}

void WasapiCapture::run() {
    const HRESULT comResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(comResult)) {
        logger_.error("WASAPI thread COM initialization failed: " + hrText(comResult));
        return;
    }
    DWORD taskIndex = 0;
    HANDLE mmcss = AvSetMmThreadCharacteristicsW(L"Audio", &taskIndex);
    ComPtr<IMMDeviceEnumerator> enumerator;
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                  IID_PPV_ARGS(&enumerator));
    if (FAILED(hr)) {
        logger_.error("MMDeviceEnumerator creation failed: " + hrText(hr));
        if (mmcss) AvRevertMmThreadCharacteristics(mmcss);
        CoUninitialize();
        return;
    }
    ComPtr<IMMNotificationClient> notification;
    notification.Attach(new NotificationClient(*this));
    enumerator->RegisterEndpointNotificationCallback(notification.Get());

    ReconnectPolicy policy;
    policy.start();
    CaptureSession session;
    std::vector<float> converted;
    CaptureStatus status;

    auto publish = [&](CaptureState state, std::string detail) {
        status.state = state;
        status.detail = std::move(detail);
        if (statusCallback_) statusCallback_(status);
    };

    auto connect = [&]() -> bool {
        session.close();
        publish(CaptureState::connecting, "WASAPI loopback 연결 중");
        bool followDefault;
        std::wstring selectedId;
        {
            std::scoped_lock lock(selectionMutex_);
            followDefault = followDefault_;
            selectedId = fixedDeviceId_;
        }
        if (followDefault || selectedId.empty()) {
            hr = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &session.device);
        } else {
            hr = enumerator->GetDevice(selectedId.c_str(), &session.device);
        }
        if (FAILED(hr)) {
            logger_.warning("Output device open failed: " + hrText(hr));
            publish(CaptureState::disconnected, "출력 장치를 열 수 없습니다: " + hrText(hr));
            return false;
        }
        status.deviceName = deviceName(session.device.Get());
        status.deviceId = deviceId(session.device.Get());
        hr = session.device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                                      reinterpret_cast<void**>(session.client.GetAddressOf()));
        if (FAILED(hr)) {
            publish(CaptureState::disconnected, "IAudioClient 활성화 실패: " + hrText(hr));
            return false;
        }
        hr = session.client->GetMixFormat(&session.mixFormat);
        if (FAILED(hr)) {
            publish(CaptureState::disconnected, "기본 mix format 조회 실패: " + hrText(hr));
            return false;
        }
        session.parsed = parseFormat(session.mixFormat);
        session.selection = selectFrontLeftRight(session.parsed.channels, session.parsed.channelMask);
        if (session.parsed.encoding == Encoding::unsupported || !session.selection.valid) {
            publish(CaptureState::disconnected,
                    "지원하지 않는 mix format 또는 Front L/R이 없는 장치입니다: " +
                        session.parsed.name);
            return false;
        }
        session.sampleEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!session.sampleEvent) {
            publish(CaptureState::disconnected, "오디오 이벤트 생성 실패");
            return false;
        }
        constexpr DWORD flags = AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_EVENTCALLBACK |
                                AUDCLNT_STREAMFLAGS_NOPERSIST;
        hr = session.client->Initialize(AUDCLNT_SHAREMODE_SHARED, flags, 0, 0, session.mixFormat, nullptr);
        if (FAILED(hr)) {
            publish(CaptureState::disconnected, "WASAPI shared loopback 초기화 실패: " + hrText(hr));
            return false;
        }
        hr = session.client->SetEventHandle(session.sampleEvent);
        if (FAILED(hr)) {
            publish(CaptureState::disconnected, "WASAPI 이벤트 연결 실패: " + hrText(hr));
            return false;
        }
        hr = session.client->GetBufferSize(&session.bufferFrames);
        if (FAILED(hr)) return false;
        converted.resize(static_cast<size_t>(session.bufferFrames) * 2);
        hr = session.client->GetService(IID_PPV_ARGS(&session.capture));
        if (FAILED(hr)) {
            publish(CaptureState::disconnected, "IAudioCaptureClient 조회 실패: " + hrText(hr));
            return false;
        }
        hr = session.client->Start();
        if (FAILED(hr)) {
            publish(CaptureState::disconnected, "WASAPI 캡처 시작 실패: " + hrText(hr));
            return false;
        }
        status.format = {session.parsed.sampleRate, session.parsed.channels,
                         session.parsed.channelMask, session.mixFormat->wBitsPerSample,
                         session.parsed.name};
        logger_.info("Capture started: device='" + toUtf8(status.deviceName) + "' id='" +
                     toUtf8(status.deviceId) + "' sampleRate=" +
                     std::to_string(status.format.sampleRate) + " channels=" +
                     std::to_string(status.format.channels) + " format=" + status.format.sampleFormat);
        if (formatCallback_) formatCallback_(session.parsed.sampleRate);
        policy.connected();
        publish(CaptureState::running, channelStatusText(session.parsed.channels));
        return true;
    };

    while (running_.load(std::memory_order_acquire)) {
        if (!connect()) {
            const auto delay = policy.failed();
            publish(CaptureState::backoff, "재연결 대기 " + std::to_string(delay.count()) + " ms");
            HANDLE waits[] = {static_cast<HANDLE>(stopEvent_), static_cast<HANDLE>(deviceChangeEvent_)};
            const DWORD result = WaitForMultipleObjects(2, waits, FALSE, static_cast<DWORD>(delay.count()));
            if (result == WAIT_OBJECT_0) break;
            policy.deviceChanged();
            continue;
        }

        bool reconnect = false;
        while (running_.load(std::memory_order_acquire) && !reconnect) {
            HANDLE waits[] = {static_cast<HANDLE>(stopEvent_), static_cast<HANDLE>(deviceChangeEvent_),
                              session.sampleEvent};
            const DWORD wait = WaitForMultipleObjects(3, waits, FALSE, INFINITE);
            if (wait == WAIT_OBJECT_0) break;
            if (wait == WAIT_OBJECT_0 + 1) {
                std::string reason;
                {
                    std::scoped_lock lock(selectionMutex_);
                    reason = reconnectReason_;
                }
                logger_.info("Capture reinitialization requested: " + reason);
                reconnect = true;
                continue;
            }
            if (wait != WAIT_OBJECT_0 + 2) {
                logger_.warning("Audio event wait failed: " + formatWindowsError(GetLastError()));
                reconnect = true;
                continue;
            }

            UINT32 packetFrames = 0;
            while (SUCCEEDED(hr = session.capture->GetNextPacketSize(&packetFrames)) && packetFrames > 0) {
                BYTE* data = nullptr;
                UINT32 frames = 0;
                DWORD flags = 0;
                hr = session.capture->GetBuffer(&data, &frames, &flags, nullptr, nullptr);
                if (FAILED(hr)) break;
                size_t offset = 0;
                while (offset < frames) {
                    const size_t count = std::min<size_t>(frames - offset, session.bufferFrames);
                    if ((flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0 || !data) {
                        std::fill_n(converted.data(), count * 2, 0.0f);
                    } else {
                        const size_t frameBytes = session.mixFormat->nBlockAlign;
                        for (size_t index = 0; index < count; ++index) {
                            const BYTE* frame = data + (offset + index) * frameBytes;
                            converted[index * 2] = readSample(frame, session.selection.leftIndex, session.parsed);
                            converted[index * 2 + 1] = readSample(frame, session.selection.rightIndex, session.parsed);
                        }
                    }
                    ring_.pushInterleaved(converted.data(), count);
                    offset += count;
                }
                session.capture->ReleaseBuffer(frames);
                if (dataReadyCallback_) dataReadyCallback_();
            }
            if (FAILED(hr)) {
                logger_.warning("WASAPI packet read failed; reconnecting: " + hrText(hr));
                reconnect = true;
            }
        }
        logger_.info("Capture stopped for device id='" + toUtf8(status.deviceId) + "'");
        session.close();
        if (running_.load(std::memory_order_acquire)) policy.deviceChanged();
    }

    session.close();
    policy.stop();
    publish(CaptureState::stopped, "캡처 중지");
    enumerator->UnregisterEndpointNotificationCallback(notification.Get());
    notification.Reset();
    enumerator.Reset();
    if (mmcss) AvRevertMmThreadCharacteristics(mmcss);
    CoUninitialize();
}

} // namespace vizrack
