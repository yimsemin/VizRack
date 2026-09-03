#include "platform/audio_devices.h"

#include "core/utf.h"

#include <windows.h>
#include <propsys.h>
#include <functiondiscoverykeys_devpkey.h>
#include <mmdeviceapi.h>
#include <propvarutil.h>
#include <wrl/client.h>

#include <cstdio>

namespace vizrack {
namespace {

using Microsoft::WRL::ComPtr;

std::string hrText(HRESULT result) {
    return "HRESULT 0x" + [] (HRESULT value) {
        char buffer[16]{};
        std::snprintf(buffer, sizeof(buffer), "%08lX", static_cast<unsigned long>(value));
        return std::string(buffer);
    }(result) + " (" + formatWindowsError(static_cast<unsigned long>(result)) + ")";
}

bool readInfo(IMMDevice* device, AudioDeviceInfo& info, std::string& error) {
    LPWSTR rawId = nullptr;
    HRESULT hr = device->GetId(&rawId);
    if (FAILED(hr)) {
        error = "Failed to query the device ID: " + hrText(hr);
        return false;
    }
    info.id = rawId;
    CoTaskMemFree(rawId);

    ComPtr<IPropertyStore> properties;
    hr = device->OpenPropertyStore(STGM_READ, &properties);
    if (FAILED(hr)) {
        error = "Failed to open the device property store: " + hrText(hr);
        return false;
    }
    PROPVARIANT value;
    PropVariantInit(&value);
    hr = properties->GetValue(PKEY_Device_FriendlyName, &value);
    if (SUCCEEDED(hr) && value.vt == VT_LPWSTR && value.pwszVal) {
        info.name = value.pwszVal;
    } else {
        info.name = info.id;
    }
    PropVariantClear(&value);
    return true;
}

bool createEnumerator(ComPtr<IMMDeviceEnumerator>& enumerator, std::string& error) {
    const HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                        IID_PPV_ARGS(&enumerator));
    if (FAILED(hr)) {
        error = "Failed to create MMDeviceEnumerator: " + hrText(hr);
        return false;
    }
    return true;
}

} // namespace

bool enumerateRenderDevices(std::vector<AudioDeviceInfo>& devices, std::string& error) {
    devices.clear();
    ComPtr<IMMDeviceEnumerator> enumerator;
    if (!createEnumerator(enumerator, error)) return false;

    std::wstring defaultId;
    ComPtr<IMMDevice> defaultDevice;
    if (SUCCEEDED(enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &defaultDevice))) {
        LPWSTR rawId = nullptr;
        if (SUCCEEDED(defaultDevice->GetId(&rawId))) {
            defaultId = rawId;
            CoTaskMemFree(rawId);
        }
    }

    ComPtr<IMMDeviceCollection> collection;
    HRESULT hr = enumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &collection);
    if (FAILED(hr)) {
        error = "Failed to enumerate output devices: " + hrText(hr);
        return false;
    }
    UINT count = 0;
    collection->GetCount(&count);
    for (UINT index = 0; index < count; ++index) {
        ComPtr<IMMDevice> endpoint;
        if (FAILED(collection->Item(index, &endpoint))) continue;
        AudioDeviceInfo info;
        std::string ignored;
        if (readInfo(endpoint.Get(), info, ignored)) {
            info.isDefault = info.id == defaultId;
            devices.push_back(std::move(info));
        }
    }
    return true;
}

} // namespace vizrack
