#pragma once

#include <string>
#include <vector>

namespace vizrack {

struct AudioDeviceInfo {
    std::wstring id;
    std::wstring name;
    bool isDefault{false};
};

bool enumerateRenderDevices(std::vector<AudioDeviceInfo>& devices, std::string& error);

} // namespace vizrack
