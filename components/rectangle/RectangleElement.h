#pragma once

#include "ui/element/Element.h"

#include <string>
#include <vector>

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
    // The pattern the outline is stroked with, and where in it the stroke begins. An empty pattern
    // strokes solid.
    void SetDash(std::string value, float gap);
    void SetDashOffset(float value);
    const std::vector<float>& Dashes() const { return dashes; }
    float DashOffset() const { return dashOffset; }

protected:
    // The view box one outline is written in and the way its stroke is drawn. A rectangle and the
    // shapes built on it write their outline in box-local pixels, which is the box itself, so they
    // take the default. A vector writes its geometry in a view box of its own and says so. The
    // pattern the stroke is drawn with is not here: a pattern belongs to the stroke, and a shape
    // has one stroke, so every outline it draws is patterned with the same one.
    struct OutlineStyle {
        float viewX = 0.0f;
        float viewY = 0.0f;
        // Zero means the box itself: the space box-local pixels are written in.
        float viewWidth = 0.0f;
        float viewHeight = 0.0f;
        bool evenOdd = false;
        char lineCap = 'b';
        char lineJoin = 'r';
    };

    void OnPaint(Renderer& renderer) override;

    // Paints one closed path as this shape: under this shape's turn, in this shape's fill, and
    // with this shape's stroke. A shape that is not a box builds its own outline instead of
    // the box's, and hands it here, so being filled and stroked is decided in one place.
    void PaintOutline(Renderer& renderer, const std::string& path);
    void PaintOutline(Renderer& renderer, const std::string& path, const OutlineStyle& style);

    // The outline of this box written in box-local pixels: the rectangle with its own four corner
    // radii. It is the shape this node draws, so it is also the shape a caller cutting something
    // off at this node hands over, which is how a picture is held inside the rounded box it sits in
    // rather than inside the box around it.
    std::string CornerPath(float boxWidth, float boxHeight) const;

private:
    // A rounded box is four independent corner arcs, so it only goes through a path when
    // the corners differ. A uniform radius keeps the platform rectangle primitive, unless the
    // stroke was given a pattern: a pattern is drawn by the platform only along a path.
    bool UniformCorners() const;

    Color fill{0.055f, 0.478f, 1.0f, 1.0f};
    Color stroke{0.114f, 0.114f, 0.122f, 1.0f};
    float strokeWidth = 0.0f;
    float cornerRadius[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    float rotationZ = 0.0f;
    std::string cursor;
    std::vector<float> dashes;
    float dashOffset = 0.0f;
};

}  // namespace Runtime
