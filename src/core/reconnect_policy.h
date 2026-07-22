#pragma once

#include <chrono>
#include <cstdint>

namespace vizrack {

enum class CaptureState { stopped, disconnected, connecting, running, backoff };

class ReconnectPolicy {
public:
    void start() noexcept;
    void connected() noexcept;
    std::chrono::milliseconds failed() noexcept;
    void deviceChanged() noexcept;
    void stop() noexcept;
    CaptureState state() const noexcept { return state_; }
    uint32_t failureCount() const noexcept { return failures_; }

private:
    CaptureState state_{CaptureState::stopped};
    uint32_t failures_{0};
};

} // namespace vizrack

