#include "components/ellipse/EllipseElement.h"

#include "compiler/types/ValueParser.h"
#include "engine/math/Geometry.h"
#include "rendering/renderer/Renderer.h"
#include "rendering/renderer/StrokePattern.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <string>

namespace Runtime {
namespace {

// An ellipse that never got a size is a circle, the same default every design tool uses.
constexpr float ELLIPSE_DEFAULT_SIZE = 100.0f;
constexpr float TURN_EPSILON = 0.01f;
// A stroke sits half inside and half outside the curve, so the clickable shape is the fill
// plus the outer half of the stroke.
constexpr float ELLIPSE_HIT_TOLERANCE = 1.5f;

void AppendNumber(std::string& path, float value) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.3f", value);
    path += buffer;
}

std::string TrimView(std::string value) {
    const auto blank = [](unsigned char character) { return std::isspace(character) != 0; };
    value.erase(value.begin(), std::find_if_not(value.begin(), value.end(), blank));
    value.erase(std::find_if_not(value.rbegin(), value.rend(), blank).base(), value.end());
    return value;
}

}  // namespace

EllipseElement::EllipseElement() {
    SetWidth(Length::Pixels(ELLIPSE_DEFAULT_SIZE));
    SetHeight(Length::Pixels(ELLIPSE_DEFAULT_SIZE));
}

NodeKind EllipseElement::Kind() const {
    return NodeKind::ELLIPSE;
}

void EllipseElement::SetFill(Color value) {
    fill = value;
}

void EllipseElement::SetStroke(Color value, float thickness) {
    stroke = value;
    SetStrokeWidth(thickness);
}

void EllipseElement::SetStrokeWidth(float thickness) {
    strokeWidth = thickness < 0.0f ? 0.0f : thickness;
}

void EllipseElement::SetDash(std::string value, float gap) {
    // A shape's outline is stroked the way a line and a path are stroked, so the pattern written in
    // the dash attribute is read by the one reader they all share rather than by each of them in turn.
    dashes = ParseDashPattern(value, gap);
}

void EllipseElement::SetDashOffset(float value) {
    dashOffset = value;
}

void EllipseElement::SetRotation(float degrees) {
    rotationZ = degrees;
}

void EllipseElement::SetCursor(std::string value) {
    cursor = std::move(value);
}

bool EllipseElement::WantsHandCursor() const {
    return cursor == "pointer";
}

void EllipseElement::SetDisplay(std::string value) {
    value = TrimView(std::move(value));
    SetVisible(value != "none" && value != "hidden");
}

bool EllipseElement::IsCircle() const {
    return std::fabs(Bounds().width - Bounds().height) < 0.5f;
}

Size EllipseElement::Measure(TextMeasurer&, float availableWidth, float availableHeight, bool) {
    float resolvedWidth = ELLIPSE_DEFAULT_SIZE;
    float resolvedHeight = ELLIPSE_DEFAULT_SIZE;
    // An ellipse has nothing inside it to hug, so a fixed, filled, or percentage length is
    // the only thing that can move it away from its default circle.
    if (Width().unit == LengthUnit::PIXELS || Width().unit == LengthUnit::PERCENT || Width().unit == LengthUnit::FILL) {
        resolvedWidth = Width().Resolve(availableWidth, resolvedWidth);
    }
    if (Height().unit == LengthUnit::PIXELS || Height().unit == LengthUnit::PERCENT || Height().unit == LengthUnit::FILL) {
        resolvedHeight = Height().Resolve(availableHeight, resolvedHeight);
    }
    measured = Size{std::max(1.0f, resolvedWidth), std::max(1.0f, resolvedHeight)};
    return FinishMeasure(availableWidth, availableHeight);
}

Element* EllipseElement::HitTest(Point point) {
    if (!Visible()) {
        return nullptr;
    }
    Point local = point;
    if (std::fabs(rotationZ) > TURN_EPSILON) {
        // Undo the turn around the same centre the renderer turned the shape about, so the
        // test is always against an axis-aligned ellipse.
        const Rect area = Bounds();
        const float centerX = area.x + area.width * 0.5f;
        const float centerY = area.y + area.height * 0.5f;
        const float radians = -rotationZ * 0.01745329252f;
        const float dx = point.x - centerX;
        const float dy = point.y - centerY;
        local = Point{centerX + dx * std::cos(radians) - dy * std::sin(radians), centerY + dx * std::sin(radians) + dy * std::cos(radians)};
    }
    const Rect area = Bounds();
    const float radiusX = area.width * 0.5f + ELLIPSE_HIT_TOLERANCE + strokeWidth * 0.5f;
    const float radiusY = area.height * 0.5f + ELLIPSE_HIT_TOLERANCE + strokeWidth * 0.5f;
    if (radiusX <= 0.0f || radiusY <= 0.0f) {
        return nullptr;
    }
    const float normalizedX = (local.x - (area.x + area.width * 0.5f)) / radiusX;
    const float normalizedY = (local.y - (area.y + area.height * 0.5f)) / radiusY;
    // The interior is the set of points whose radii add up to less than one, which is what
    // keeps a click in the corner of the box outside the shape.
    return normalizedX * normalizedX + normalizedY * normalizedY <= 1.0f ? this : nullptr;
}

std::string EllipseOutline(float boxWidth, float boxHeight) {
    const float radiusX = boxWidth * 0.5f;
    const float radiusY = boxHeight * 0.5f;
    // Two half arcs, left pole to right pole and back. Each arc's chord is the full width,
    // so the radii fit it exactly and no correction is applied by the sampler.
    std::string path = "M ";
    AppendNumber(path, 0.0f);
    path += ' ';
    AppendNumber(path, radiusY);
    path += " A ";
    AppendNumber(path, radiusX);
    path += ' ';
    AppendNumber(path, radiusY);
    path += " 0 0 1 ";
    AppendNumber(path, boxWidth);
    path += ' ';
    AppendNumber(path, radiusY);
    path += " A ";
    AppendNumber(path, radiusX);
    path += ' ';
    AppendNumber(path, radiusY);
    path += " 0 0 1 ";
    AppendNumber(path, 0.0f);
    path += ' ';
    AppendNumber(path, radiusY);
    return path;
}

void EllipseElement::ApplyBound(const std::string& property, const std::string& value) {
    Element::ApplyBound(property, value);
    if (property == "fill" || property == "background" || property == "backgroundColor") {
        if (const std::optional<Color> color = ParseColor(value)) {
            fill = *color;
        }
        return;
    }
    if (property == "stroke") {
        if (const std::optional<Color> color = ParseColor(value)) {
            stroke = *color;
        }
        return;
    }
    if (property == "strokeWidth") {
        SetStrokeWidth(std::strtof(value.c_str(), nullptr));
    } else if (property == "dash") {
        SetDash(value, 0.0f);
    } else if (property == "dashGap") {
        // The gap a single-number pattern is stroked with. A pattern written as several numbers
        // carries its own gaps, so only the first number is read back here.
        const float gap = std::strtof(value.c_str(), nullptr);
        if (!dashes.empty()) {
            SetDash(std::to_string(dashes.front()), gap);
        }
    } else if (property == "dashOffset") {
        dashOffset = std::strtof(value.c_str(), nullptr);
    } else if (property == "rotation") {
        rotationZ = std::strtof(value.c_str(), nullptr);
    }
}

void EllipseElement::OnPaint(Renderer& renderer) {
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
    // A pattern is drawn only along a path, so a patterned stroke takes the path even when equal
    // radii would have let the platform primitive draw the circle.
    const bool patterned = stroked && !dashes.empty();
    if (IsCircle() && !patterned) {
        // Equal radii mean a rounded rectangle whose radius is half the box, which is the
        // same circle drawn by the platform primitive instead of a sampled path.
        const float radius = std::min(area.width, area.height) * 0.5f;
        if (filled) {
            renderer.FillRectangle(area, fill, radius);
        }
        if (stroked) {
            renderer.StrokeRectangle(area, stroke, strokeWidth, radius);
        }
    } else {
        // Only a path can turn a box into a true ellipse: a rounded rectangle with the same
        // radii would come out a stadium, not an ellipse. A square box written this way is the
        // circle's outline as well, so the patterned stroke is drawn against the same curve.
        const std::string path = EllipseOutline(area.width, area.height);
        VectorPaint paint;
        paint.bounds = area;
        paint.data = path;
        paint.viewX = 0.0f;
        paint.viewY = 0.0f;
        paint.viewWidth = area.width;
        paint.viewHeight = area.height;
        paint.fill = fill;
        paint.stroke = stroke;
        paint.strokeWidth = strokeWidth;
        paint.fillEnabled = filled;
        paint.lineJoin = 'r';
        paint.dashes = dashes;
        paint.dashOffset = dashOffset;
        renderer.DrawVector(paint);
    }
    if (turned) {
        renderer.PopTransform();
    }
}

}  // namespace Runtime
