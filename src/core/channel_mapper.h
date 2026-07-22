#pragma once

#include <cstdint>
#include <string>

namespace vizrack {

struct ChannelSelection {
    uint32_t leftIndex{0};
    uint32_t rightIndex{0};
    uint32_t channelCount{0};
    bool multichannel{false};
    bool valid{false};
};

ChannelSelection selectFrontLeftRight(uint32_t channelCount, uint32_t channelMask);
std::string channelStatusText(uint32_t channelCount);

} // namespace vizrack

