#pragma once

#include "ui/element/Element.h"

#include <string>

namespace Runtime {

// A rectangle is the simplest shape in the document: one box that carries a fill, a
// stroke, and its own radius at each corner. It takes no children and has no layout of
// its own, so every value it carries belongs to the shape itself.
class RectangleElement : public Element {
public:
    RectangleElement();

    NodeKind Kind() const override;
    Size Measure(TextMeasurer& measurer, float availableWidth, float availableHeight, bool heightDefinite) override;
    Element* HitTest(Point point) override;
    void ApplyBound(const std::string& property, const std::string& value) override;
    bool WantsHandCursor() const override;

    void SetFill(Color value);
    void SetStroke(Color value, float thickness);
    void SetRadius(float value);
    void SetCornerRadius(int corner, float value);
    void SetRotation(float degrees);
    void SetCursor(std::string value);
    void SetDisplay(std::string value);

    Color Fill() const { return fill; }
    Color StrokeColor() const { return stroke; }
    float StrokeWidth() const { return strokeWidth; }
    float CornerRadius(int corner) const;
    float Radius() const;
    float Rotation() const { return rotationZ; }
    const std::string& Cursor() const { return cursor; }

protected:
    void OnPaint(Renderer& renderer) override;

    // Paints one closed path as this shape: under this shape's turn, in this shape's fill, and
    // with this shape's stroke. A shape that is not a box builds its own outline instead of
    // the box's, and hands it here, so being filled and stroked is decided in one place.
    void PaintOutline(Renderer& renderer, const std::string& path);

private:
    // A rounded box is four independent corner arcs, so it only goes through a path when
    // the corners differ. A uniform radius keeps the platform rectangle primitive.
    bool UniformCorners() const;
    std::string CornerPath(float boxWidth, float boxHeight) const;

    Color fill{0.055f, 0.478f, 1.0f, 1.0f};
    Color stroke{0.114f, 0.114f, 0.122f, 1.0f};
    float strokeWidth = 0.0f;
    float cornerRadius[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    float rotationZ = 0.0f;
    std::string cursor;
};

}  // namespace Runtime
