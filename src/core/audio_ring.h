#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace vizrack {

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4324) // Intentional cache-line padding around producer/consumer indices.
#endif
class StereoFrameRing {
public:
    explicit StereoFrameRing(size_t capacityFrames);

    size_t pushInterleaved(const float* stereo, size_t frames) noexcept;
    size_t popPlanar(float* left, float* right, size_t maxFrames) noexcept;
    size_t available() const noexcept;
    size_t capacity() const noexcept { return capacityFrames_; }
    uint64_t droppedFrames() const noexcept { return dropped_.load(std::memory_order_relaxed); }
    void discardOlderThan(size_t framesToKeep) noexcept;

private:
    static size_t nextPowerOfTwo(size_t value);

    size_t capacityFrames_{};
    size_t mask_{};
    std::vector<float> samples_;
    alignas(64) std::atomic<uint64_t> write_{0};
    alignas(64) std::atomic<uint64_t> read_{0};
    std::atomic<uint64_t> dropped_{0};
};
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

} // namespace vizrack
