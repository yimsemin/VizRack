#include "ui/gdi_draw_list_renderer.h"

#include <algorithm>

namespace vizrack {
namespace {

Gdiplus::Color toGdiColor(builtin::Color value) {
    return Gdiplus::Color(value.alpha,
                          static_cast<BYTE>((value.rgb >> 16) & 0xff),
                          static_cast<BYTE>((value.rgb >> 8) & 0xff),
                          static_cast<BYTE>(value.rgb & 0xff));
}

void configurePen(Gdiplus::Pen& pen, builtin::Color value, float width,
                  bool roundStroke) {
    pen.SetColor(toGdiColor(value));
    pen.SetWidth(width);
    pen.SetLineJoin(roundStroke ? Gdiplus::LineJoinRound : Gdiplus::LineJoinMiter);
    pen.SetStartCap(roundStroke ? Gdiplus::LineCapRound : Gdiplus::LineCapFlat);
    pen.SetEndCap(roundStroke ? Gdiplus::LineCapRound : Gdiplus::LineCapFlat);
}

} // namespace

GdiDrawListRenderer::GdiDrawListRenderer() {
    Gdiplus::GdiplusStartupInput input;
    if (Gdiplus::GdiplusStartup(&gdiplusToken_, &input, nullptr) != Gdiplus::Ok) {
        gdiplusToken_ = 0;
    }
    pointScratch_.reserve(2048);
}

GdiDrawListRenderer::~GdiDrawListRenderer() {
    if (gdiplusToken_) Gdiplus::GdiplusShutdown(gdiplusToken_);
}

std::span<const Gdiplus::PointF> GdiDrawListRenderer::pointsFor(
    const builtin::DrawList& list, builtin::PointRange range) {
    const auto source = list.points();
    if (range.offset > source.size() || range.count > source.size() - range.offset) return {};
    if (cachedOffset_ != range.offset || cachedCount_ != range.count) {
        pointScratch_.resize(range.count);
        for (size_t index = 0; index < range.count; ++index) {
            const auto& point = source[range.offset + index];
            pointScratch_[index] = {point.x, point.y};
        }
        cachedOffset_ = range.offset;
        cachedCount_ = range.count;
    }
    return pointScratch_;
}

void GdiDrawListRenderer::render(HDC dc, const builtin::DrawList& list) {
    if (!dc || !available()) return;
    cachedOffset_ = UINT32_MAX;
    cachedCount_ = 0;
    Gdiplus::Graphics graphics(dc);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
    Gdiplus::Pen pen(Gdiplus::Color(0, 0, 0, 0), 1.0f);
    Gdiplus::SolidBrush solidBrush(Gdiplus::Color(0, 0, 0, 0));
    for (const auto& command : list.commands()) {
        switch (command.primitive) {
            case builtin::DrawPrimitive::verticalGradient: {
                const Gdiplus::PointF top(command.x, command.y);
                const Gdiplus::PointF bottom(command.x, command.y + command.height);
                Gdiplus::LinearGradientBrush brush(top, bottom, toGdiColor(command.primary),
                                                   toGdiColor(command.secondary));
                graphics.FillRectangle(&brush, command.x, command.y, command.width, command.height);
                break;
            }
            case builtin::DrawPrimitive::radialGradientEllipse: {
                Gdiplus::GraphicsPath path;
                path.AddEllipse(command.x, command.y, command.width, command.height);
                Gdiplus::PathGradientBrush brush(&path);
                brush.SetCenterColor(toGdiColor(command.primary));
                Gdiplus::Color edge = toGdiColor(command.secondary);
                INT count = 1;
                brush.SetSurroundColors(&edge, &count);
                graphics.FillPath(&brush, &path);
                break;
            }
            case builtin::DrawPrimitive::line: {
                configurePen(pen, command.primary, command.strokeWidth, command.roundStroke);
                graphics.DrawLine(&pen, command.x, command.y, command.x2, command.y2);
                break;
            }
            case builtin::DrawPrimitive::polyline: {
                const auto points = pointsFor(list, command.points);
                if (points.size() < 2) break;
                configurePen(pen, command.primary, command.strokeWidth, command.roundStroke);
                graphics.DrawLines(&pen, points.data(), static_cast<INT>(points.size()));
                break;
            }
            case builtin::DrawPrimitive::arc: {
                configurePen(pen, command.primary, command.strokeWidth, true);
                graphics.DrawArc(&pen, command.x, command.y, command.width, command.height,
                                 command.startAngle, command.sweepAngle);
                break;
            }
            case builtin::DrawPrimitive::fillEllipse: {
                solidBrush.SetColor(toGdiColor(command.primary));
                graphics.FillEllipse(&solidBrush, command.x, command.y,
                                     command.width, command.height);
                break;
            }
            case builtin::DrawPrimitive::strokeEllipse: {
                configurePen(pen, command.primary, command.strokeWidth, false);
                graphics.DrawEllipse(&pen, command.x, command.y, command.width, command.height);
                break;
            }
            case builtin::DrawPrimitive::fillPolygon: {
                const auto points = pointsFor(list, command.points);
                if (points.size() < 3) break;
                solidBrush.SetColor(toGdiColor(command.primary));
                graphics.FillPolygon(&solidBrush, points.data(), static_cast<INT>(points.size()),
                                     Gdiplus::FillModeWinding);
                break;
            }
            case builtin::DrawPrimitive::strokePolygon: {
                const auto points = pointsFor(list, command.points);
                if (points.size() < 2) break;
                configurePen(pen, command.primary, command.strokeWidth, command.roundStroke);
                graphics.DrawPolygon(&pen, points.data(), static_cast<INT>(points.size()));
                break;
            }
            case builtin::DrawPrimitive::fillRectangle: {
                solidBrush.SetColor(toGdiColor(command.primary));
                graphics.FillRectangle(&solidBrush, command.x, command.y,
                                       command.width, command.height);
                break;
            }
        }
    }
}

} // namespace vizrack
