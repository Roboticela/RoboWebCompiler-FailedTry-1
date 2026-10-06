#include "components/line/LineElement.h"

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

// A line of no length is still a line: a dot the size of its own stroke.
constexpr float LINE_MINIMUM_SPAN = 1.0f;
constexpr float LINE_DEGENERATE_SPAN = 0.5f;
constexpr float TURN_EPSILON = 0.01f;

// A thin line is hard to hit exactly, so the stroke is inflated to a comfortable band.
constexpr float LINE_HIT_TOLERANCE = 1.5f;

constexpr float LINE_MAX_DASH = 512.0f;

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

// Distances between a point and a segment, used to decide whether a click landed on the
// stroke. A zero-length segment falls back to the distance to its single point.
float DistanceToSegment(Point point, Point start, Point end) {
    const float dx = end.x - start.x;
    const float dy = end.y - start.y;
    const float lengthSquared = dx * dx + dy * dy;
    if (lengthSquared <= 0.0001f) {
        return std::hypot(point.x - start.x, point.y - start.y);
    }
    const float t = std::clamp(((point.x - start.x) * dx + (point.y - start.y) * dy) / lengthSquared, 0.0f, 1.0f);
    return std::hypot(point.x - (start.x + dx * t), point.y - (start.y + dy * t));
}

}  // namespace

LineElement::LineElement() {
    SetEndpoints(x1, y1, x2, y2);
}

NodeKind LineElement::Kind() const {
    return NodeKind::LINE;
}

void LineElement::SetEndpoints(float startX, float startY, float endX, float endY) {
    x1 = startX;
    y1 = startY;
    x2 = endX;
    y2 = endY;
    spanOriginX = std::min(x1, x2);
    spanOriginY = std::min(y1, y2);
    spanX = std::fabs(x2 - x1);
    spanY = std::fabs(y2 - y1);
}

void LineElement::SetStroke(Color value, float thickness) {
    stroke = value;
    SetStrokeWidth(thickness);
}

void LineElement::SetStrokeWidth(float thickness) {
    strokeWidth = thickness < 0.0f ? 0.0f : thickness;
}

void LineElement::SetLineCap(std::string value) {
    capName = TrimView(std::move(value));
    if (capName == "round") {
        lineCap = 'r';
    } else if (capName == "square") {
        lineCap = 's';
    } else {
        lineCap = 'b';
        capName = "butt";
    }
}

void LineElement::SetDash(std::string value, float gap) {
    // A dash is a pattern of painted and unpainted runs. One number plus a gap is the short
    // form; several numbers alternate on and off; anything else leaves the line solid.
    dashes.clear();
    const std::string text = TrimView(std::move(value));
    if (text.empty() || text == "none" || text == "solid") {
        return;
    }
    std::vector<float> numbers;
    const char* scan = text.c_str();
    char* end = nullptr;
    while (*scan != '\0') {
        const float number = std::strtof(scan, &end);
        if (end == scan) {
            // A separator or anything unreadable moves the scan along; a pattern with no
            // number in it at all was already treated as solid above.
            ++scan;
            continue;
        }
        if (number > 0.0f) {
            numbers.push_back(std::min(number, LINE_MAX_DASH));
        }
        scan = end;
    }
    if (numbers.empty()) {
        return;
    }
    if (numbers.size() == 1) {
        const float on = numbers.front();
        const float off = gap > 0.0f ? std::min(gap, LINE_MAX_DASH) : on;
        dashes = {on, off};
        return;
    }
    // The pattern has to alternate, so an odd count repeats itself once to close the pair.
    dashes = numbers;
    if (dashes.size() % 2 != 0) {
        dashes.push_back(numbers.front());
    }
}

void LineElement::SetRotation(float degrees) {
    rotationZ = degrees;
}

void LineElement::SetCursor(std::string value) {
    cursor = std::move(value);
}

bool LineElement::WantsHandCursor() const {
    return cursor == "pointer";
}

void LineElement::SetDisplay(std::string value) {
    value = TrimView(std::move(value));
    SetVisible(value != "none" && value != "hidden");
}

Size LineElement::Measure(TextMeasurer&, float availableWidth, float availableHeight, bool) {
    SetEndpoints(x1, y1, x2, y2);
    // The box is the span of the endpoints. A line with no run on an axis still needs a box
    // there, so that axis is floored rather than left at zero.
    float resolvedWidth = std::max(LINE_MINIMUM_SPAN, spanX);
    float resolvedHeight = std::max(LINE_MINIMUM_SPAN, spanY);
    // A line has nothing inside it to hug, so an explicit length is the only thing that can
    // move it away from its span.
    if (Width().unit == LengthUnit::PIXELS || Width().unit == LengthUnit::PERCENT || Width().unit == LengthUnit::FILL) {
        resolvedWidth = Width().Resolve(availableWidth, resolvedWidth);
    }
    if (Height().unit == LengthUnit::PIXELS || Height().unit == LengthUnit::PERCENT || Height().unit == LengthUnit::FILL) {
        resolvedHeight = Height().Resolve(availableHeight, resolvedHeight);
    }
    measured = Size{std::max(LINE_MINIMUM_SPAN, resolvedWidth), std::max(LINE_MINIMUM_SPAN, resolvedHeight)};
    return FinishMeasure(availableWidth, availableHeight);
}

void LineElement::PlaceEndpoints(Point& start, Point& end) const {
    const Rect area = Bounds();
    const float scaleX = spanX > LINE_DEGENERATE_SPAN ? area.width / spanX : 1.0f;
    const float scaleY = spanY > LINE_DEGENERATE_SPAN ? area.height / spanY : 1.0f;
    // An axis with no run has nothing to scale, so the line sits across the middle of its
    // box instead of hugging one edge of it.
    const float baseX = spanX > LINE_DEGENERATE_SPAN ? area.x - spanOriginX * scaleX : area.x + area.width * 0.5f;
    const float baseY = spanY > LINE_DEGENERATE_SPAN ? area.y - spanOriginY * scaleY : area.y + area.height * 0.5f;
    start = Point{baseX + x1 * scaleX, baseY + y1 * scaleY};
    end = Point{baseX + x2 * scaleX, baseY + y2 * scaleY};
}

std::string LineElement::SegmentPath(const Rect& area, Point start, Point end) const {
    std::string path = "M ";
    AppendNumber(path, start.x - area.x);
    path += ' ';
    AppendNumber(path, start.y - area.y);
    path += " L ";
    AppendNumber(path, end.x - area.x);
    path += ' ';
    AppendNumber(path, end.y - area.y);
    return path;
}

Element* LineElement::HitTest(Point point) {
    if (!Visible()) {
        return nullptr;
    }
    Point local = point;
    if (std::fabs(rotationZ) > TURN_EPSILON) {
        // Undo the turn around the same centre the renderer turned the line about.
        const Rect area = Bounds();
        const float centerX = area.x + area.width * 0.5f;
        const float centerY = area.y + area.height * 0.5f;
        const float radians = -rotationZ * 0.01745329252f;
        const float dx = point.x - centerX;
        const float dy = point.y - centerY;
        local = Point{centerX + dx * std::cos(radians) - dy * std::sin(radians), centerY + dx * std::sin(radians) + dy * std::cos(radians)};
    }
    Point start{};
    Point end{};
    PlaceEndpoints(start, end);
    const float tolerance = std::max(LINE_HIT_TOLERANCE, strokeWidth * 0.5f);
    return DistanceToSegment(local, start, end) <= tolerance ? this : nullptr;
}

void LineElement::ApplyBound(const std::string& property, const std::string& value) {
    Element::ApplyBound(property, value);
    if (property == "stroke" || property == "fill" || property == "background" || property == "backgroundColor") {
        if (const std::optional<Color> color = ParseColor(value)) {
            stroke = *color;
        }
        return;
    }
    if (property == "strokeWidth") {
        SetStrokeWidth(std::strtof(value.c_str(), nullptr));
    } else if (property == "lineCap") {
        SetLineCap(value);
    } else if (property == "dash") {
        SetDash(value, 0.0f);
    } else if (property == "dashGap") {
        const float gap = std::strtof(value.c_str(), nullptr);
        if (!dashes.empty()) {
            SetDash(std::to_string(dashes.front()), gap);
        }
    } else if (property == "rotation") {
        rotationZ = std::strtof(value.c_str(), nullptr);
    } else if (property == "x1") {
        SetEndpoints(std::strtof(value.c_str(), nullptr), y1, x2, y2);
    } else if (property == "y1") {
        SetEndpoints(x1, std::strtof(value.c_str(), nullptr), x2, y2);
    } else if (property == "x2") {
        SetEndpoints(x1, y1, std::strtof(value.c_str(), nullptr), y2);
    } else if (property == "y2") {
        SetEndpoints(x1, y1, x2, std::strtof(value.c_str(), nullptr));
    }
}

void LineElement::PaintStroke(Renderer& renderer, const Rect& area, Point start, Point end) {
    const std::string path = SegmentPath(area, start, end);
    VectorPaint paint;
    // The path is already in box-local pixels, so the view box matches the box exactly and
    // the stroke width reaches the renderer at its real size.
    paint.bounds = area;
    paint.data = path;
    paint.viewWidth = area.width;
    paint.viewHeight = area.height;
    paint.stroke = stroke;
    paint.strokeWidth = strokeWidth;
    paint.lineCap = lineCap;
    paint.dashes = dashes;
    paint.fillEnabled = false;
    renderer.DrawVector(paint);
}

void LineElement::OnPaint(Renderer& renderer) {
    const Rect area = Bounds();
    if (area.width <= 0.0f || area.height <= 0.0f || strokeWidth <= 0.0f || stroke.a <= 0.0f) {
        return;
    }
    const bool turned = std::fabs(rotationZ) > TURN_EPSILON;
    if (turned) {
        renderer.PushRotation(rotationZ, area.x + area.width * 0.5f, area.y + area.height * 0.5f);
    }
    Point start{};
    Point end{};
    PlaceEndpoints(start, end);
    // Whatever the line stands between its endpoints is drawn under the same turn, so a turn
    // carries a decoration along with the stroke it decorates.
    PaintStroke(renderer, area, start, end);
    if (turned) {
        renderer.PopTransform();
    }
}

}  // namespace Runtime
