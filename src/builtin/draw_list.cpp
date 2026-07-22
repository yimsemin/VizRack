#include "builtin/draw_list.h"

#include <limits>
#include <stdexcept>

namespace vizrack::builtin {

DrawList::DrawList() {
    // Pulse Matrix is currently the largest scene (roughly 1,000 commands). Reserving once
    // keeps steady-state rendering free from frame-by-frame heap growth.
    commands_.reserve(1200);
    points_.reserve(5000);
}

void DrawList::reset() noexcept {
    commands_.clear();
    points_.clear();
}

PointRange DrawList::appendPoints(std::span<const Point> points) {
    if (points_.size() + points.size() > std::numeric_limits<uint32_t>::max()) {
        throw std::length_error("visualizer draw-list point capacity exceeded");
    }
    const PointRange range{static_cast<uint32_t>(points_.size()),
                           static_cast<uint32_t>(points.size())};
    points_.insert(points_.end(), points.begin(), points.end());
    return range;
}

DrawCommand& DrawList::add(DrawPrimitive primitive, Color value) {
    commands_.push_back({});
    auto& command = commands_.back();
    command.primitive = primitive;
    command.primary = value;
    return command;
}

void DrawList::addVerticalGradient(float x, float y, float width, float height,
                                   Color top, Color bottom) {
    auto& command = add(DrawPrimitive::verticalGradient, top);
    command.secondary = bottom;
    command.x = x;
    command.y = y;
    command.width = width;
    command.height = height;
}

void DrawList::addLine(float x1, float y1, float x2, float y2, Color value,
                       float strokeWidth, bool roundStroke) {
    auto& command = add(DrawPrimitive::line, value);
    command.x = x1;
    command.y = y1;
    command.x2 = x2;
    command.y2 = y2;
    command.strokeWidth = strokeWidth;
    command.roundStroke = roundStroke;
}

void DrawList::addPolyline(PointRange points, Color value, float strokeWidth,
                           bool roundStroke) {
    auto& command = add(DrawPrimitive::polyline, value);
    command.points = points;
    command.strokeWidth = strokeWidth;
    command.roundStroke = roundStroke;
}

void DrawList::addArc(float x, float y, float width, float height, float startAngle,
                      float sweepAngle, Color value, float strokeWidth) {
    auto& command = add(DrawPrimitive::arc, value);
    command.x = x;
    command.y = y;
    command.width = width;
    command.height = height;
    command.startAngle = startAngle;
    command.sweepAngle = sweepAngle;
    command.strokeWidth = strokeWidth;
}

void DrawList::addFillEllipse(float x, float y, float width, float height, Color value) {
    auto& command = add(DrawPrimitive::fillEllipse, value);
    command.x = x;
    command.y = y;
    command.width = width;
    command.height = height;
}

void DrawList::addStrokeEllipse(float x, float y, float width, float height, Color value,
                                float strokeWidth) {
    auto& command = add(DrawPrimitive::strokeEllipse, value);
    command.x = x;
    command.y = y;
    command.width = width;
    command.height = height;
    command.strokeWidth = strokeWidth;
}

void DrawList::addStrokePolygon(PointRange points, Color value, float strokeWidth,
                                bool roundStroke) {
    auto& command = add(DrawPrimitive::strokePolygon, value);
    command.points = points;
    command.strokeWidth = strokeWidth;
    command.roundStroke = roundStroke;
}

void DrawList::addFillRectangle(float x, float y, float width, float height, Color value) {
    auto& command = add(DrawPrimitive::fillRectangle, value);
    command.x = x;
    command.y = y;
    command.width = width;
    command.height = height;
}

} // namespace vizrack::builtin
