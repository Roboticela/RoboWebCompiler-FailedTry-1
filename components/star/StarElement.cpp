#include "components/star/StarElement.h"

#include <algorithm>
#include <cstdlib>

namespace Runtime {
namespace {

constexpr int STAR_MIN_POINTS = 3;
constexpr int STAR_MAX_POINTS = 64;

}  // namespace

NodeKind StarElement::Kind() const {
    return NodeKind::STAR;
}

void StarElement::SetPoints(int count) {
    pointCount = std::clamp(count, STAR_MIN_POINTS, STAR_MAX_POINTS);
}

void StarElement::SetInnerRadius(float value) {
    // One is as deep as a star can go before its valleys stop being valleys.
    innerRadius = std::clamp(value, 0.0f, 1.0f);
}

std::vector<Point> StarElement::Corners(const Rect& area) const {
    // Two corners for every point: the point itself, then a valley half a step along. The polygon
    // owns the step and the first corner, so the first of these is a point at the top and the ring
    // runs clockwise from there exactly as a polygon's does.
    return StretchToBox(RingOnCircle(Points() * 2, 1.0f, InnerRadius()), area);
}

void StarElement::ApplyBound(const std::string& property, const std::string& value) {
    // The polygon answers for the box, the fill, the stroke, the corner radius, the turn, and the
    // cursor, and through the polygon the rectangle does. Only the two values that shape the ring
    // are read here.
    PolygonElement::ApplyBound(property, value);
    if (property == "points") {
        SetPoints(static_cast<int>(std::strtof(value.c_str(), nullptr)));
    } else if (property == "innerRadius") {
        SetInnerRadius(std::strtof(value.c_str(), nullptr));
    }
}

}  // namespace Runtime
