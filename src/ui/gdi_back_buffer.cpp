#include "ui/gdi_back_buffer.h"

namespace vizrack {

GdiBackBuffer::~GdiBackBuffer() {
    reset();
}

bool GdiBackBuffer::ensure(HDC target, int width, int height) noexcept {
    if (!target || width <= 0 || height <= 0) return false;
    if (memoryDc_ && bitmap_ && width_ == width && height_ == height) return true;
    if (!memoryDc_) {
        memoryDc_ = CreateCompatibleDC(target);
        if (!memoryDc_) return false;
    }
    HBITMAP replacement = CreateCompatibleBitmap(target, width, height);
    if (!replacement) return false;
    HGDIOBJ replaced = SelectObject(memoryDc_, replacement);
    if (!replaced || replaced == HGDI_ERROR) {
        DeleteObject(replacement);
        return false;
    }
    if (!originalBitmap_) originalBitmap_ = replaced;
    if (bitmap_) DeleteObject(bitmap_);
    bitmap_ = replacement;
    width_ = width;
    height_ = height;
    return true;
}

void GdiBackBuffer::present(HDC target, int width, int height) const noexcept {
    if (target && memoryDc_ && bitmap_) {
        BitBlt(target, 0, 0, width, height, memoryDc_, 0, 0, SRCCOPY);
    }
}

void GdiBackBuffer::reset() noexcept {
    if (memoryDc_ && bitmap_) {
        if (originalBitmap_) SelectObject(memoryDc_, originalBitmap_);
        DeleteObject(bitmap_);
    }
    if (memoryDc_) DeleteDC(memoryDc_);
    memoryDc_ = nullptr;
    bitmap_ = nullptr;
    originalBitmap_ = nullptr;
    width_ = 0;
    height_ = 0;
}

} // namespace vizrack
