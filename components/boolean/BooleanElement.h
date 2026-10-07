#pragma once

#include "components/rectangle/RectangleElement.h"
#include "rendering/path/PathBoolean.h"

#include <string>

namespace Runtime {

// A boolean group is one shape made out of the shapes it holds. Union keeps everything they cover,
// subtract takes the shape at the bottom less the ones above it, intersect keeps only what they all
// cover, and exclude keeps the parts an odd number of them cover, which for two shapes is where they
// overlap.
//
// It is built on the rectangle because it is a shape: it carries a box, a fill, a stroke, a pattern
// to stroke it with, a turn, a cursor, a display, and the values every element carries, and it is
// measured, arranged, drawn, and hit the way the rectangle underneath is. What it brings is an
// outline that is not written down anywhere: the shapes it holds are painted into an outline
// recorder, their outlines are combined by the operation, and the answer is the shape this draws.
//
// A group that was not given a size of its own is as big as the shapes it holds, so a combination
// can be wrapped around two shapes that are already placed and moved as one. The fill and the stroke
// each shape carried are not drawn: they are what the shape looked like on its own, and the group
// draws the combined outline in its own fill and stroke. The shapes are still there, and still keep
// their own interaction where the combination kept them.
class BooleanElement : public RectangleElement {
public:
    // A group left without a size of its own is as big as the shapes it holds, so it does not start
    // as the square a bare rectangle starts as.
    BooleanElement();

    NodeKind Kind() const override;
    Size Measure(TextMeasurer& measurer, float availableWidth, float availableHeight, bool heightDefinite) override;
    void Arrange(const Rect& rect) override;
    Element* HitTest(Point point) override;
    void ApplyBound(const std::string& property, const std::string& value) override;

    void SetOperation(BooleanOperation operation);
    BooleanOperation Operation() const { return operation; }

protected:
    void OnPaint(Renderer& renderer) override;

private:
    // The outline everything the group holds adds up to, in box-local pixels, or nothing when the
    // shapes add up to no shape at all.
    std::string CombinedOutline();

    BooleanOperation operation = BooleanOperation::UNION;
    // The size the shapes cover together, which is the size the group was authored at and the size
    // Arrange scales them out of.
    Size natural{0.0f, 0.0f};
};

}  // namespace Runtime
