#include "components/arrow/ArrowElement.h"

#include "rendering/renderer/Renderer.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace Runtime {
namespace {

// A stroke with no run has no direction to lay a head along, so the arrow is left as the dot
// the line underneath already draws.
constexpr float ARROW_MINIMUM_SPAN = 0.5f;

void AppendNumber(std::string& path, float value) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.3f", value);
    path += buffer;
}

// The path syntax the renderer reads is box-local, so every point loses the box origin on the
// way in. The stroke of the line underneath does the same thing.
void AppendPoint(std::string& path, const Rect& area, Point point) {
    AppendNumber(path, point.x - area.x);
    path += ' ';
    AppendNumber(path, point.y - area.y);
}

std::string TrimView(std::string value) {
    const auto blank = [](unsigned char character) { return std::isspace(character) != 0; };
    value.erase(value.begin(), std::find_if_not(value.begin(), value.end(), blank));
    value.erase(std::find_if_not(value.rbegin(), value.rend(), blank).base(), value.end());
    return value;
}

}  // namespace

NodeKind ArrowElement::Kind() const {
    return NodeKind::ARROW;
}

void ArrowElement::SetHeadLength(float value) {
    headLength = value < 0.0f ? 0.0f : value;
}

void ArrowElement::SetHeadWidth(float value) {
    headWidth = value < 0.0f ? 0.0f : value;
}

void ArrowElement::SetHead(std::string value) {
    headName = TrimView(std::move(value));
    if (headName == "start") {
        head = HeadMode::START;
    } else if (headName == "both") {
        head = HeadMode::BOTH;
    } else {
        head = HeadMode::END;
        headName = "end";
    }
}

void ArrowElement::ApplyBound(const std::string& property, const std::string& value) {
    // The line answers for everything it owns first, so a binding aimed at the endpoints, the
    // stroke, the cap, the dash, or the turn still lands on the arrow.
    LineElement::ApplyBound(property, value);
    if (property == "headLength") {
        SetHeadLength(std::strtof(value.c_str(), nullptr));
    } else if (property == "headWidth") {
        SetHeadWidth(std::strtof(value.c_str(), nullptr));
    } else if (property == "head") {
        SetHead(value);
    }
}

void ArrowElement::PaintHead(Renderer& renderer, const Rect& area, Point apex, float directionX, float directionY, float length, float half) {
    if (length <= 0.0f || half <= 0.0f) {
        return;
    }
    // The two barbs sit on the line through the tip, square to the direction the head points,
    // and the tip closes the triangle.
    const Point neck{apex.x - directionX * length, apex.y - directionY * length};
    const Point left{neck.x - directionY * half, neck.y + directionX * half};
    const Point right{neck.x + directionY * half, neck.y - directionX * half};
    std::string path = "M ";
    AppendPoint(path, area, left);
    path += " L ";
    AppendPoint(path, area, right);
    path += " L ";
    AppendPoint(path, area, apex);
    path += " Z";
    VectorPaint paint;
    paint.bounds = area;
    paint.data = path;
    paint.viewWidth = area.width;
    paint.viewHeight = area.height;
    // The head is solid even when the shaft is dashed: a broken outline would leave the point
    // of the arrow in pieces. It takes the ink from the line it decorates.
    paint.fill = StrokeColor();
    paint.fillEnabled = true;
    paint.lineJoin = 'm';
    renderer.DrawVector(paint);
}

void ArrowElement::PaintStroke(Renderer& renderer, const Rect& area, Point start, Point end) {
    const float span = std::hypot(end.x - start.x, end.y - start.y);
    if (span <= ARROW_MINIMUM_SPAN) {
        LineElement::PaintStroke(renderer, area, start, end);
        return;
    }
    const float directionX = (end.x - start.x) / span;
    const float directionY = (end.y - start.y) / span;
    // A head never eats its own shaft. On a stroke too short to hold one the head shrinks to
    // fit, and a two-headed arrow only ever asks for half the stroke per head, so the shaft
    // between the heads is never inverted into a backwards point.
    const float capacity = head == HeadMode::BOTH ? span * 0.5f : span;
    const float length = std::min(headLength, capacity);
    const float half = headWidth * 0.5f;
    Point shaftStart = start;
    Point shaftEnd = end;
    if (head != HeadMode::START) {
        shaftEnd = Point{end.x - directionX * length, end.y - directionY * length};
    }
    if (head != HeadMode::END) {
        shaftStart = Point{start.x + directionX * length, start.y + directionY * length};
    }
    // The shaft is the line's own stroke, drawn by the line's own code, so a line that learns
    // a new cap, a new dash, or a new way of placing its endpoints changes every arrow with it.
    LineElement::PaintStroke(renderer, area, shaftStart, shaftEnd);
    if (head != HeadMode::START) {
        PaintHead(renderer, area, end, directionX, directionY, length, half);
    }
    if (head != HeadMode::END) {
        PaintHead(renderer, area, start, -directionX, -directionY, length, half);
    }
}

}  // namespace Runtime
