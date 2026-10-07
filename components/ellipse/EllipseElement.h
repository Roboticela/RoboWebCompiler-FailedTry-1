#pragma once

#include "ui/element/Element.h"

#include <string>
#include <vector>

namespace Runtime {

// The outline of an ellipse that exactly fills a box, written as a path in box-local pixels: two
// half arcs, left pole to right pole and back. An ellipse is a fact about ellipses rather than
// about this node, so anything that needs an ellipse shape draws this one rather than a second.
std::string EllipseOutline(float boxWidth, float boxHeight);

// An ellipse is one closed curve with a fill and a stroke. Its two radii are the halves of
// its box, so a circle is an ellipse whose box is square. It holds nothing and lays out
// nothing, which makes it one item in its parent's flow.
class EllipseElement : public Element {
public:
    EllipseElement();

    NodeKind Kind() const override;
    Size Measure(TextMeasurer& measurer, float availableWidth, float availableHeight, bool heightDefinite) override;
    Element* HitTest(Point point) override;
    void ApplyBound(const std::string& property, const std::string& value) override;
    bool WantsHandCursor() const override;

    void SetFill(Color value);
    void SetStroke(Color value, float thickness);
    void SetStrokeWidth(float thickness);
    void SetRotation(float degrees);
    void SetCursor(std::string value);
    void SetDisplay(std::string value);

    Color Fill() const { return fill; }
    Color StrokeColor() const { return stroke; }
    float StrokeWidth() const { return strokeWidth; }
    float Rotation() const { return rotationZ; }
    const std::string& Cursor() const { return cursor; }
    // A circle is an ellipse whose two radii match, which is the only case that can be drawn
    // as a rounded rectangle instead of a sampled path.
    bool IsCircle() const;
    // The pattern the outline is stroked with, and where in it the stroke begins. An empty pattern
    // strokes solid. A pattern is drawn only along a path, so a patterned outline is the other
    // case that cannot be drawn as a rounded rectangle.
    void SetDash(std::string value, float gap);
    void SetDashOffset(float value);
    const std::vector<float>& Dashes() const { return dashes; }
    float DashOffset() const { return dashOffset; }

protected:
    void OnPaint(Renderer& renderer) override;

private:
    Color fill{0.055f, 0.478f, 1.0f, 1.0f};
    Color stroke{0.114f, 0.114f, 0.122f, 1.0f};
    float strokeWidth = 0.0f;
    float rotationZ = 0.0f;
    std::string cursor;
    std::vector<float> dashes;
    float dashOffset = 0.0f;
};

}  // namespace Runtime
