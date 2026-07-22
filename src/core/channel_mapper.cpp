#include "core/channel_mapper.h"

#include <bit>

namespace vizrack {
namespace {

constexpr uint32_t kFrontLeft = 0x1;
constexpr uint32_t kFrontRight = 0x2;

uint32_t indexOfSpeaker(uint32_t mask, uint32_t speaker) {
    return std::popcount(mask & (speaker - 1));
}

} // namespace

ChannelSelection selectFrontLeftRight(uint32_t channelCount, uint32_t channelMask) {
    ChannelSelection result;
    result.channelCount = channelCount;
    result.multichannel = channelCount > 2;
    if (channelCount < 2) {
        return result;
    }
    if (channelMask != 0) {
        if (!(channelMask & kFrontLeft) || !(channelMask & kFrontRight)) return result;
        result.leftIndex = indexOfSpeaker(channelMask, kFrontLeft);
        result.rightIndex = indexOfSpeaker(channelMask, kFrontRight);
    } else {
        result.leftIndex = 0;
        result.rightIndex = 1;
    }
    result.valid = result.leftIndex < channelCount && result.rightIndex < channelCount;
    return result;
}

std::string channelStatusText(uint32_t channelCount) {
    if (channelCount > 2) {
        return "입력: " + std::to_string(channelCount) +
               "채널 — Front Left / Front Right만 측정 중";
    }
    if (channelCount == 2) {
        return "입력: 스테레오 (Left / Right)";
    }
    if (channelCount == 1) {
        return "입력: 모노 — 선택한 플러그인에는 무음으로 전달";
    }
    return "입력 채널 정보 없음";
}

} // namespace vizrack
