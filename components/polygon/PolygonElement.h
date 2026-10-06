#pragma once

#include "components/rectangle/RectangleElement.h"

#include <string>
#include <vector>

namespace Runtime {

// A polygon is a rectangle's worth of shape with a different outline. It takes the same box,
// the same default size, the same fill, stroke, corner radius, turn, cursor, and bindings, and
// puts a ring of equal sides inside that box instead of a box outline.
//
// It is built on the rectangle for that reason rather than beside it: everything except the
// outline belongs to the shape underneath, so a rectangle that learns something new about being
// measured, hit, filled, stroked, or bound teaches every polygon with it. A shape that stands on
// a polygon, such as a star, takes the ring and replaces the corner list the same way.
class PolygonElement : public RectangleElement {
public:
    NodeKind Kind() const override;
    Element* HitTest(Point point) override;
    void ApplyBound(const std::string& property, const std::string& value) override;

    void SetSides(int count);
    int Sides() const { return sideCount; }

protected:
    void OnPaint(Renderer& renderer) override;

    // The corners of this shape's outline, in composition coordinates, in the order they are
    // drawn: clockwise on screen starting at the top, which is the winding the corner arcs and
    // the hit test both expect. A shape whose outline is not a ring of equal sides keeps the
    // winding and replaces this list.
    virtual std::vector<Point> Corners(const Rect& area) const;

private:
    // The outline in box-local pixels, in the path syntax the renderer reads.
    std::string OutlinePath(const Rect& area) const;

    // A ring of three sides is the fewest that can close; past a few dozen the outline is a
    // circle and the count stops meaning anything.
    int sideCount = 3;
};

}  // namespace Runtime
