#pragma once

#include "builtin/draw_list.h"

#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>

#include <cstdint>
#include <vector>

namespace vizrack {

class GdiDrawListRenderer {
public:
    GdiDrawListRenderer();
    ~GdiDrawListRenderer();

    GdiDrawListRenderer(const GdiDrawListRenderer&) = delete;
    GdiDrawListRenderer& operator=(const GdiDrawListRenderer&) = delete;

    bool available() const noexcept { return gdiplusToken_ != 0; }
    void render(HDC dc, const builtin::DrawList& list);

private:
    std::span<const Gdiplus::PointF> pointsFor(const builtin::DrawList& list,
                                               builtin::PointRange range);

    ULONG_PTR gdiplusToken_{};
    std::vector<Gdiplus::PointF> pointScratch_;
    uint32_t cachedOffset_{UINT32_MAX};
    uint32_t cachedCount_{};
};

} // namespace vizrack
