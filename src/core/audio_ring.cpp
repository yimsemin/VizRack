#include "core/audio_ring.h"

#include <algorithm>
#include <stdexcept>

namespace vizrack {

size_t StereoFrameRing::nextPowerOfTwo(size_t value) {
    size_t result = 1;
    while (result < value) {
        result <<= 1;
    }
    return result;
}

StereoFrameRing::StereoFrameRing(size_t capacityFrames)
    : capacityFrames_(nextPowerOfTwo(std::max<size_t>(capacityFrames, 2))),
      mask_(capacityFrames_ - 1),
      samples_(capacityFrames_ * 2) {}

size_t StereoFrameRing::pushInterleaved(const float* stereo, size_t frames) noexcept {
    if (!stereo || frames == 0) {
        return 0;
    }
    const uint64_t write = write_.load(std::memory_order_relaxed);
    const uint64_t read = read_.load(std::memory_order_acquire);
    const size_t free = capacityFrames_ - static_cast<size_t>(write - read);
    const size_t accepted = std::min(frames, free);
    for (size_t i = 0; i < accepted; ++i) {
        const size_t destination = static_cast<size_t>((write + i) & mask_) * 2;
        samples_[destination] = stereo[i * 2];
        samples_[destination + 1] = stereo[i * 2 + 1];
    }
    write_.store(write + accepted, std::memory_order_release);
    dropped_.fetch_add(frames - accepted, std::memory_order_relaxed);
    return accepted;
}

size_t StereoFrameRing::popPlanar(float* left, float* right, size_t maxFrames) noexcept {
    if (!left || !right || maxFrames == 0) {
        return 0;
    }
    const uint64_t read = read_.load(std::memory_order_relaxed);
    const uint64_t write = write_.load(std::memory_order_acquire);
    const size_t count = std::min(maxFrames, static_cast<size_t>(write - read));
    for (size_t i = 0; i < count; ++i) {
        const size_t source = static_cast<size_t>((read + i) & mask_) * 2;
        left[i] = samples_[source];
        right[i] = samples_[source + 1];
    }
    read_.store(read + count, std::memory_order_release);
    return count;
}

size_t StereoFrameRing::available() const noexcept {
    const uint64_t write = write_.load(std::memory_order_acquire);
    const uint64_t read = read_.load(std::memory_order_acquire);
    return static_cast<size_t>(write - read);
}

void StereoFrameRing::discardOlderThan(size_t framesToKeep) noexcept {
    const uint64_t write = write_.load(std::memory_order_acquire);
    const uint64_t read = read_.load(std::memory_order_relaxed);
    const uint64_t count = write - read;
    if (count > framesToKeep) {
        const uint64_t discard = count - framesToKeep;
        read_.store(read + discard, std::memory_order_release);
        dropped_.fetch_add(discard, std::memory_order_relaxed);
    }
}

} // namespace vizrack
