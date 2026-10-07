#pragma once

#include "components/polygon/PolygonElement.h"

#include <vector>

namespace Runtime {

// A star is a polygon whose ring holds two radii instead of one: a point further out, then a valley
// further in, all the way round. Everything else about it belongs to the polygon underneath, and
// through the polygon to the rectangle underneath that: the box, the default size, the fill, the
// stroke, the corner radius, the turn, the cursor, the bindings, the way an outline becomes a path,
// and the hit test that samples that path.
//
// So this class brings no paint of its own at all. It says how many points the ring has and how
// deep the valleys between them go, and the shape is measured, drawn, rounded, and hit by the code
// that already does those things for a polygon. A polygon that learns to round a corner better
// rounds every star better the same day.
class StarElement : public PolygonElement {
public:
    NodeKind Kind() const override;
    void ApplyBound(const std::string& property, const std::string& value) override;

    void SetPoints(int count);
    void SetInnerRadius(float value);

    int Points() const { return pointCount; }
    float InnerRadius() const { return innerRadius; }

protected:
    std::vector<Point> Corners(const Rect& area) const override;

private:
    // Three points is the fewest that can read as a star rather than as a triangle; past a few
    // dozen the points are a fringe and the count stops meaning anything.
    int pointCount = 5;
    // The valleys reach this share of the way out to the points, so the shape travels with its box
    // instead of holding a size that only suits one of them.
    float innerRadius = 0.382f;
};

}  // namespace Runtime
