#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace vizrack::builtin {

struct Point {
    float x{};
    float y{};
};

struct Color {
    uint32_t rgb{};
    uint8_t alpha{255};
};

struct PointRange {
    uint32_t offset{};
    uint32_t count{};
};

enum class DrawPrimitive : uint8_t {
    verticalGradient,
    line,
    polyline,
    arc,
    fillEllipse,
    strokeEllipse,
    strokePolygon,
    fillRectangle,
};

// DrawCommand is deliberately a small, renderer-neutral vocabulary. Scene code owns the
// geometry; platform adapters only translate these commands to GDI+ or, later, Canvas 2D.
// Add a primitive only when both platform renderers genuinely need a new capability.
struct DrawCommand {
    DrawPrimitive primitive{};
    Color primary{};
    Color secondary{};
    float strokeWidth{1.0f};
    float x{};
    float y{};
    float width{};
    float height{};
    float x2{};
    float y2{};
    float startAngle{};
    float sweepAngle{};
    PointRange points{};
    bool roundStroke{};
};

class DrawList {
public:
    DrawList();

    void reset() noexcept;
    PointRange appendPoints(std::span<const Point> points);

    void addVerticalGradient(float x, float y, float width, float height,
                             Color top, Color bottom);
    void addLine(float x1, float y1, float x2, float y2, Color color,
                 float strokeWidth, bool roundStroke = false);
    void addPolyline(PointRange points, Color color, float strokeWidth,
                     bool roundStroke = true);
    void addArc(float x, float y, float width, float height, float startAngle,
                float sweepAngle, Color color, float strokeWidth);
    void addFillEllipse(float x, float y, float width, float height, Color color);
    void addStrokeEllipse(float x, float y, float width, float height, Color color,
                          float strokeWidth);
    void addStrokePolygon(PointRange points, Color color, float strokeWidth,
                          bool roundStroke = true);
    void addFillRectangle(float x, float y, float width, float height, Color color);

    std::span<const DrawCommand> commands() const noexcept { return commands_; }
    std::span<const Point> points() const noexcept { return points_; }
    size_t commandCapacity() const noexcept { return commands_.capacity(); }
    size_t pointCapacity() const noexcept { return points_.capacity(); }

private:
    DrawCommand& add(DrawPrimitive primitive, Color color);

    std::vector<DrawCommand> commands_;
    std::vector<Point> points_;
};

constexpr Color color(uint32_t rgb, uint8_t alpha = 255) noexcept {
    return Color{rgb, alpha};
}

} // namespace vizrack::builtin
