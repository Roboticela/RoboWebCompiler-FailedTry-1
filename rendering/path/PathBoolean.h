#pragma once

#include "engine/math/Geometry.h"
#include "rendering/path/PathCurve.h"

#include <string>
#include <string_view>
#include <vector>

namespace Runtime {

// What a boolean group does with the outlines of the shapes it holds.
enum class BooleanOperation {
    // Everything any shape covers.
    UNION,
    // The first shape, less everything the shapes after it cover.
    SUBTRACT,
    // Only what every shape covers.
    INTERSECT,
    // What an odd number of shapes covers, which is the parts that overlap when there are two.
    EXCLUDE,
};

BooleanOperation ParseBooleanOperation(std::string_view text);
const char* BooleanOperationName(BooleanOperation operation);

// True when a point is inside the region a shape's outlines describe: the points whose winding
// number around those outlines is not zero, which is the region an outline nested inside another
// carves its holes out of.
bool OutlinesContain(const std::vector<PathContour>& outlines, Point point);

// Combines the outlines of one or more shapes into a single outline, written in the path syntax the
// renderer reads.
//
// Each shape is handed over as the outlines it is made of, in any orientation: the shape covers the
// points whose winding number around its outlines is not zero, an outline nested inside another is
// a hole in it, and an outline left open is closed, which is what lets a line take part as the
// thinnest shape it can be. A shape with no area, such as a straight line or an empty path, covers
// nothing and takes no part in the combination.
//
// SUBTRACT answers with the first shape less every shape after it; the other three treat every
// shape the same way, so there is no first shape to them.
//
// The answer is written as one loop per boundary, with the loops that bound a hole wound the other
// way round, so it is drawn with the nonzero fill rule the shapes it came from were drawn with.
// Outlines are read as the polylines the platform flattens curves into, which is the same shape the
// renderer would have drawn: combining them adds no error the drawing did not already have.
//
// A boundary is placed by asking which shapes cover the space just off each side of it, at a
// distance that is a thousandth of the size of the whole combination. Two boundaries that pass
// closer together than that are read as one, so a shape thinner than that is at the mercy of the
// shapes beside it rather than of itself.
std::string CombineOutlines(const std::vector<std::vector<PathContour>>& shapes, BooleanOperation operation);

}  // namespace Runtime
