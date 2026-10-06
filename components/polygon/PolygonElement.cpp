#include "components/polygon/PolygonElement.h"

#include "engine/math/Geometry.h"
#include "rendering/path/PathCurve.h"
#include "rendering/renderer/Renderer.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstddef>
#include <limits>
#include <string>
#include <vector>

namespace Runtime {
namespace {

constexpr int POLYGON_MIN_SIDES = 3;
constexpr int POLYGON_MAX_SIDES = 64;

constexpr float TURN_EPSILON = 0.01f;
constexpr float LENGTH_EPSILON = 0.01f;
// A thin outline is hard to hit exactly, so the stroke is inflated to a comfortable band.
constexpr float HIT_TOLERANCE = 1.5f;
constexpr float FULL_TURN = 6.28318530718f;
constexpr float QUARTER_TURN = 1.57079632679f;
constexpr float HALF_TURN = 3.14159265359f;

void AppendNumber(std::string& path, float value) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.3f", value);
    path += buffer;
}

// One corner of an outline after rounding. A corner with no radius keeps both ends on the vertex
// itself, so the outline runs straight through it.
struct RoundedCorner {
    Point entry;
    Point exit;
    Point center;
    float radius = 0.0f;
    float sweep = 0.0f;
};

// A corner is replaced by an arc that is tangent to both of its edges. How far from the vertex
// that arc starts is fixed by the angle it has to fit, so a wide corner needs a long lead-in and a
// nearly straight one needs almost none. The lead-in is capped at half the shorter edge, which is
// what keeps two neighbouring arcs from reaching into each other.
RoundedCorner RoundCorner(Point vertex, Point before, Point after, float radius) {
    RoundedCorner corner;
    corner.entry = vertex;
    corner.exit = vertex;
    corner.center = vertex;
    const float inX = before.x - vertex.x;
    const float inY = before.y - vertex.y;
    const float outX = after.x - vertex.x;
    const float outY = after.y - vertex.y;
    const float inLength = std::hypot(inX, inY);
    const float outLength = std::hypot(outX, outY);
    if (radius <= LENGTH_EPSILON || inLength <= LENGTH_EPSILON || outLength <= LENGTH_EPSILON) {
        return corner;
    }
    const float ux = inX / inLength;
    const float uy = inY / inLength;
    const float wx = outX / outLength;
    const float wy = outY / outLength;
    const float dot = std::clamp(ux * wx + uy * wy, -1.0f, 1.0f);
    // The narrow angle between the two edges, which is never more than half a turn.
    const float opening = std::acos(dot);
    if (opening <= LENGTH_EPSILON || opening >= HALF_TURN - LENGTH_EPSILON) {
        // Two edges that carry straight on, or that fold back onto each other, leave no corner
        // to round; forcing an arc in would pull the ring apart.
        return corner;
    }
    // A corner that stands out and one that folds in have the same narrow angle, so which way the
    // ring turns at the vertex decides which of the two it is. Travelling the ring, the vertex is
    // entered along -u and left along w, and a ring wound clockwise on screen turns right at a
    // corner that stands out, which is the positive cross product of those two directions.
    const float turn = -ux * wy + uy * wx;
    const float interior = turn >= 0.0f ? opening : FULL_TURN - opening;
    const float half = interior * 0.5f;
    const float tangentOfHalf = std::tan(half);
    float lead = radius / tangentOfHalf;
    const float limit = std::min(inLength, outLength) * 0.5f;
    if (std::fabs(lead) > limit) {
        lead = std::copysign(limit, lead);
    }
    if (std::fabs(lead) <= LENGTH_EPSILON) {
        return corner;
    }
    // Where the arc meets each edge, and the centre it turns about: on the bisector, far enough
    // out that the arc holds exactly its own radius away from both edges.
    corner.entry = Point{vertex.x + ux * lead, vertex.y + uy * lead};
    corner.exit = Point{vertex.x + wx * lead, vertex.y + wy * lead};
    const float bisectorX = ux + wx;
    const float bisectorY = uy + wy;
    const float bisectorLength = std::hypot(bisectorX, bisectorY);
    if (bisectorLength <= LENGTH_EPSILON) {
        return corner;
    }
    const float reach = lead / std::cos(half);
    corner.center = Point{vertex.x + bisectorX / bisectorLength * reach, vertex.y + bisectorY / bisectorLength * reach};
    // A lead-in that had to be shortened leaves a smaller arc behind, so the radius is read back
    // from the lead-in that survived rather than from the one that was asked for.
    corner.radius = std::fabs(lead * tangentOfHalf);
    // The arc turns through whatever angle the corner itself did not use.
    corner.sweep = HALF_TURN - interior;
    return corner;
}

}  // namespace

NodeKind PolygonElement::Kind() const {
    return NodeKind::POLYGON;
}

void PolygonElement::SetSides(int count) {
    sideCount = std::clamp(count, POLYGON_MIN_SIDES, POLYGON_MAX_SIDES);
}

std::vector<Point> PolygonElement::Corners(const Rect& area) const {
    const int count = Sides();
    // A regular polygon is a ring of equal steps around a circle. The first corner sits at the
    // top and the ring runs clockwise from there, so a triangle points up and a four-sided
    // polygon stands on one corner.
    const float step = FULL_TURN / static_cast<float>(count);
    std::vector<Point> ring;
    ring.reserve(static_cast<std::size_t>(count));
    float minX = 1.0f;
    float maxX = -1.0f;
    float minY = 1.0f;
    float maxY = -1.0f;
    for (int index = 0; index < count; ++index) {
        const float angle = -QUARTER_TURN + step * static_cast<float>(index);
        const Point point{std::cos(angle), std::sin(angle)};
        ring.push_back(point);
        minX = std::min(minX, point.x);
        maxX = std::max(maxX, point.x);
        minY = std::min(minY, point.y);
        maxY = std::max(maxY, point.y);
    }
    // The circle the ring was read off is then stretched onto the box, one axis at a time. A
    // shape left inside its circle instead would sit away from the handles of the box it was
    // dragged out to, and a three-sided ring would look narrow for no reason.
    const float spanX = maxX - minX;
    const float spanY = maxY - minY;
    std::vector<Point> corners;
    corners.reserve(ring.size());
    for (const Point& point : ring) {
        const float unitX = spanX > LENGTH_EPSILON ? (point.x - minX) / spanX : 0.5f;
        const float unitY = spanY > LENGTH_EPSILON ? (point.y - minY) / spanY : 0.5f;
        corners.push_back(Point{area.x + unitX * area.width, area.y + unitY * area.height});
    }
    return corners;
}

std::string PolygonElement::OutlinePath(const Rect& area) const {
    const std::vector<Point> corners = Corners(area);
    if (corners.size() < 3) {
        return {};
    }
    const float radius = std::max(0.0f, Radius());
    const std::size_t count = corners.size();
    std::string path;
    for (std::size_t index = 0; index < count; ++index) {
        const Point vertex = corners[index];
        const RoundedCorner corner = RoundCorner(vertex, corners[(index + count - 1) % count], corners[(index + 1) % count], radius);
        // The path is in box-local pixels, which is the space the view box describes.
        path += index == 0 ? "M " : "L ";
        AppendNumber(path, corner.entry.x - area.x);
        path += ' ';
        AppendNumber(path, corner.entry.y - area.y);
        path += ' ';
        if (corner.radius > LENGTH_EPSILON) {
            path += "A ";
            AppendNumber(path, corner.radius);
            path += ' ';
            AppendNumber(path, corner.radius);
            // A corner arc never turns through half a circle or more, so it never takes the long
            // way round; only the sweep follows the way the ring is wound.
            path += " 0 0 ";
            path += corner.sweep >= 0.0f ? '1' : '0';
            path += ' ';
            AppendNumber(path, corner.exit.x - area.x);
            path += ' ';
            AppendNumber(path, corner.exit.y - area.y);
            path += ' ';
        }
    }
    path += "Z";
    return path;
}

Element* PolygonElement::HitTest(Point point) {
    if (!Visible() || !ContainsPoint(Bounds(), point)) {
        return nullptr;
    }
    if (std::fabs(Rotation()) > TURN_EPSILON) {
        // A turned shape is hit through the box the renderer turned with it, the same way a
        // turned rectangle is.
        return this;
    }
    const Rect area = Bounds();
    const std::string path = OutlinePath(area);
    if (path.empty()) {
        return nullptr;
    }
    // The outline is sampled exactly the way the renderer samples it, so a click lands on the
    // shape that was actually drawn rather than on a second guess at it. The samples come back
    // in box-local pixels, so the point loses the box origin on the way in.
    const Point local{point.x - area.x, point.y - area.y};
    const float tolerance = std::max(HIT_TOLERANCE, StrokeWidth() * 0.5f);
    for (const PathContour& contour : SampleContours(path)) {
        const std::size_t count = contour.points.size();
        bool inside = false;
        float nearest = std::numeric_limits<float>::max();
        for (std::size_t index = 0; index < count; ++index) {
            const PathSample& from = contour.points[index];
            const PathSample& to = contour.points[(index + 1) % count];
            // A point is inside a closed ring when a ray from it crosses the ring an odd number
            // of times. The comparison is written so a crossing exactly on a vertex is counted
            // once rather than twice.
            if ((from.y > local.y) != (to.y > local.y)) {
                const float crossing = from.x + (local.y - from.y) / (to.y - from.y) * (to.x - from.x);
                if (local.x < crossing) {
                    inside = !inside;
                }
            }
            // A stroke reaches outside the outline, so the distance to the outline counts too.
            const float dx = to.x - from.x;
            const float dy = to.y - from.y;
            const float lengthSquared = dx * dx + dy * dy;
            float along = 0.0f;
            if (lengthSquared > 0.0f) {
                along = std::clamp(((local.x - from.x) * dx + (local.y - from.y) * dy) / lengthSquared, 0.0f, 1.0f);
            }
            const float distance = std::hypot(local.x - (from.x + dx * along), local.y - (from.y + dy * along));
            if (distance < nearest) {
                nearest = distance;
            }
        }
        if (inside || nearest <= tolerance) {
            return this;
        }
    }
    return nullptr;
}

void PolygonElement::ApplyBound(const std::string& property, const std::string& value) {
    // The rectangle answers for the box, the fill, the stroke, the corner radius, and the turn,
    // and a polygon is those things with a different outline. Only the side count is new.
    RectangleElement::ApplyBound(property, value);
    if (property == "sides") {
        SetSides(static_cast<int>(std::strtof(value.c_str(), nullptr)));
    }
}

void PolygonElement::OnPaint(Renderer& renderer) {
    // The rectangle underneath owns the turn, the fill, the stroke, and the decision not to draw
    // a shape that has neither of those. The only thing this shape brings is its own outline.
    PaintOutline(renderer, OutlinePath(Bounds()));
}

}  // namespace Runtime
