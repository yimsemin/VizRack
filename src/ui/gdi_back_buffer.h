#pragma once

#include <windows.h>

namespace vizrack {

// Reusable memory DC/bitmap pair. The previous implementation recreated a full-size bitmap on
// every frame; this object only reallocates when the client size changes.
class GdiBackBuffer {
public:
    GdiBackBuffer() = default;
    ~GdiBackBuffer();

    GdiBackBuffer(const GdiBackBuffer&) = delete;
    GdiBackBuffer& operator=(const GdiBackBuffer&) = delete;

    bool ensure(HDC target, int width, int height) noexcept;
    void present(HDC target, int width, int height) const noexcept;
    void reset() noexcept;
    HDC dc() const noexcept { return memoryDc_; }

private:
    HDC memoryDc_{};
    HBITMAP bitmap_{};
    HGDIOBJ originalBitmap_{};
    int width_{};
    int height_{};
};

} // namespace vizrack
