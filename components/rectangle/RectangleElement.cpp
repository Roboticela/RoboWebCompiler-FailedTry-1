#include "components/rectangle/RectangleElement.h"

#include "compiler/types/ValueParser.h"
#include "engine/math/Geometry.h"
#include "rendering/renderer/Renderer.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <string>

namespace Runtime {
namespace {

// A rectangle that never got a size is a square, the same default every design tool uses.
constexpr float RECTANGLE_DEFAULT_SIZE = 100.0f;
constexpr float TURN_EPSILON = 0.01f;

void AppendNumber(std::string& path, float value) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.3f", value);
    path += buffer;
}

}  // namespace

RectangleElement::RectangleElement() {
    SetWidth(Length::Pixels(RECTANGLE_DEFAULT_SIZE));
    SetHeight(Length::Pixels(RECTANGLE_DEFAULT_SIZE));
}

NodeKind RectangleElement::Kind() const {
    return NodeKind::RECTANGLE;
}

void RectangleElement::SetFill(Color value) {
    fill = value;
}

void RectangleElement::SetStroke(Color value, float thickness) {
    stroke = value;
    strokeWidth = thickness < 0.0f ? 0.0f : thickness;
}

void RectangleElement::SetRadius(float value) {
    const float radius = value < 0.0f ? 0.0f : value;
    for (float& corner : cornerRadius) {
        corner = radius;
    }
}

void RectangleElement::SetCornerRadius(int corner, float value) {
    if (corner < 0 || corner > 3) {
        return;
    }
    cornerRadius[corner] = value < 0.0f ? 0.0f : value;
}

void RectangleElement::SetRotation(float degrees) {
    rotationZ = degrees;
}

void RectangleElement::SetCursor(std::string value) {
    cursor = std::move(value);
}

bool RectangleElement::WantsHandCursor() const {
    return cursor == "pointer";
}

void RectangleElement::SetDisplay(std::string value) {
    const auto blank = [](unsigned char character) { return std::isspace(character) != 0; };
    value.erase(value.begin(), std::find_if_not(value.begin(), value.end(), blank));
    value.erase(std::find_if_not(value.rbegin(), value.rend(), blank).base(), value.end());
    SetVisible(value != "none" && value != "hidden");
}

float RectangleElement::CornerRadius(int corner) const {
    if (corner < 0 || corner > 3) {
        return 0.0f;
    }
    return cornerRadius[corner];
}

float RectangleElement::Radius() const {
    return std::max(std::max(cornerRadius[0], cornerRadius[1]), std::max(cornerRadius[2], cornerRadius[3]));
}

bool RectangleElement::UniformCorners() const {
    return cornerRadius[0] == cornerRadius[1] && cornerRadius[1] == cornerRadius[2] && cornerRadius[2] == cornerRadius[3];
}

Size RectangleElement::Measure(TextMeasurer&, float availableWidth, float availableHeight, bool) {
    float resolvedWidth = RECTANGLE_DEFAULT_SIZE;
    float resolvedHeight = RECTANGLE_DEFAULT_SIZE;
    // A rectangle has nothing inside it to hug, so a fixed, filled, or percentage length
    // is the only thing that can move it away from its default square.
    if (Width().unit == LengthUnit::PIXELS || Width().unit == LengthUnit::PERCENT || Width().unit == LengthUnit::FILL) {
        resolvedWidth = Width().Resolve(availableWidth, resolvedWidth);
    }
    if (Height().unit == LengthUnit::PIXELS || Height().unit == LengthUnit::PERCENT || Height().unit == LengthUnit::FILL) {
        resolvedHeight = Height().Resolve(availableHeight, resolvedHeight);
    }
    measured = Size{std::max(1.0f, resolvedWidth), std::max(1.0f, resolvedHeight)};
    return FinishMeasure(availableWidth, availableHeight);
}

Element* RectangleElement::HitTest(Point point) {
    if (!Visible() || !ContainsPoint(Bounds(), point)) {
        return nullptr;
    }
    if (std::fabs(rotationZ) > TURN_EPSILON) {
        // A turned rectangle is hit through the box the renderer turned with it.
        return this;
    }
    const float left = Bounds().x;
    const float top = Bounds().y;
    const float right = left + Bounds().width;
    const float bottom = top + Bounds().height;
    const float limit = std::min(Bounds().width, Bounds().height) * 0.5f;
    // The corner square is the only part of the box a rounded corner removes, so the
    // distance to the corner centre decides whether the point is still inside the shape.
    const struct {
        float centerX;
        float centerY;
        float radius;
        bool rightSide;
        bool bottomSide;
    } corners[4] = {
        {left, top, std::min(CornerRadius(0), limit), false, false},
        {right, top, std::min(CornerRadius(1), limit), true, false},
        {right, bottom, std::min(CornerRadius(2), limit), true, true},
        {left, bottom, std::min(CornerRadius(3), limit), false, true},
    };
    for (const auto& corner : corners) {
        if (corner.radius <= TURN_EPSILON) {
            continue;
        }
        const float centerX = corner.rightSide ? right - corner.radius : left + corner.radius;
        const float centerY = corner.bottomSide ? bottom - corner.radius : top + corner.radius;
        const bool towardsX = corner.rightSide ? point.x > centerX : point.x < centerX;
        const bool towardsY = corner.bottomSide ? point.y > centerY : point.y < centerY;
        if (!towardsX || !towardsY) {
            continue;
        }
        const float dx = point.x - centerX;
        const float dy = point.y - centerY;
        if (dx * dx + dy * dy > corner.radius * corner.radius) {
            return nullptr;
        }
    }
    return this;
}

void RectangleElement::ApplyBound(const std::string& property, const std::string& value) {
    Element::ApplyBound(property, value);
    if (property == "fill" || property == "background" || property == "backgroundColor" || property == "stroke") {
        const std::optional<Color> color = ParseColor(value);
        if (color.has_value()) {
            if (property == "stroke") {
                stroke = *color;
            } else {
                fill = *color;
            }
        }
        return;
    }
    if (property == "strokeWidth") {
        SetStroke(stroke, std::strtof(value.c_str(), nullptr));
    } else if (property == "radius") {
        SetRadius(std::strtof(value.c_str(), nullptr));
    } else if (property == "rotation") {
        rotationZ = std::strtof(value.c_str(), nullptr);
    }
}

std::string RectangleElement::CornerPath(float boxWidth, float boxHeight) const {
    const float limit = std::min(boxWidth, boxHeight) * 0.5f;
    float radius[4];
    for (int corner = 0; corner < 4; ++corner) {
        radius[corner] = std::clamp(cornerRadius[corner], 0.0f, limit);
    }
    auto move = [&](std::string& path, float x, float y) {
        path += "M ";
        AppendNumber(path, x);
        path += ' ';
        AppendNumber(path, y);
        path += ' ';
    };
    auto line = [&](std::string& path, float x, float y) {
        path += "L ";
        AppendNumber(path, x);
        path += ' ';
        AppendNumber(path, y);
        path += ' ';
    };
    auto arc = [&](std::string& path, float value, float x, float y) {
        if (value <= TURN_EPSILON) {
            return;
        }
        path += "A ";
        AppendNumber(path, value);
        path += ' ';
        AppendNumber(path, value);
        path += " 0 0 1 ";
        AppendNumber(path, x);
        path += ' ';
        AppendNumber(path, y);
        path += ' ';
    };
    // Clockwise from the top-left corner: top edge, right edge, bottom edge, left edge.
    // Each corner is a line to where its arc starts, so a square corner needs no arc and
    // the four lines alone already draw the box.
    std::string path;
    move(path, radius[0], 0.0f);
    line(path, boxWidth - radius[1], 0.0f);
    arc(path, radius[1], boxWidth, radius[1]);
    line(path, boxWidth, boxHeight - radius[2]);
    arc(path, radius[2], boxWidth - radius[2], boxHeight);
    line(path, radius[3], boxHeight);
    arc(path, radius[3], 0.0f, boxHeight - radius[3]);
    line(path, 0.0f, radius[0]);
    arc(path, radius[0], radius[0], 0.0f);
    path += "Z";
    return path;
}

void RectangleElement::OnPaint(Renderer& renderer) {
    const Rect area = Bounds();
    if (area.width <= 0.0f || area.height <= 0.0f) {
        return;
    }
    const bool filled = fill.a > 0.0f;
    const bool stroked = strokeWidth > 0.0f && stroke.a > 0.0f;
    if (!filled && !stroked) {
        return;
    }
    const bool turned = std::fabs(rotationZ) > TURN_EPSILON;
    if (turned) {
        renderer.PushRotation(rotationZ, area.x + area.width * 0.5f, area.y + area.height * 0.5f);
    }
    if (UniformCorners()) {
        if (filled) {
            renderer.FillRectangle(area, fill, Radius());
        }
        if (stroked) {
            renderer.StrokeRectangle(area, stroke, strokeWidth, Radius());
        }
    } else {
        VectorPaint paint;
        paint.bounds = area;
        paint.data = CornerPath(area.width, area.height);
        paint.viewX = 0.0f;
        paint.viewY = 0.0f;
        paint.viewWidth = area.width;
        paint.viewHeight = area.height;
        paint.fill = fill;
        paint.stroke = stroke;
        paint.strokeWidth = strokeWidth;
        paint.fillEnabled = filled;
        paint.lineJoin = 'r';
        renderer.DrawVector(paint);
    }
    if (turned) {
        renderer.PopTransform();
    }
}

}  // namespace Runtime
