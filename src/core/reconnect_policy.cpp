#include "core/reconnect_policy.h"

#include <algorithm>

namespace vizrack {

void ReconnectPolicy::start() noexcept {
    failures_ = 0;
    state_ = CaptureState::connecting;
}

void ReconnectPolicy::connected() noexcept {
    failures_ = 0;
    state_ = CaptureState::running;
}

std::chrono::milliseconds ReconnectPolicy::failed() noexcept {
    state_ = CaptureState::backoff;
    const uint32_t exponent = std::min(failures_, 5u);
    ++failures_;
    return std::chrono::milliseconds(500u << exponent);
}

void ReconnectPolicy::deviceChanged() noexcept {
    failures_ = 0;
    if (state_ != CaptureState::stopped) state_ = CaptureState::connecting;
}

void ReconnectPolicy::stop() noexcept {
    state_ = CaptureState::stopped;
    failures_ = 0;
}

} // namespace vizrack

