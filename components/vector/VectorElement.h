#pragma once

#include "components/rectangle/RectangleElement.h"

#include <string>

namespace Runtime {

// A vector is a shape whose outline is path geometry rather than a box. It carries the path an
// icon is drawn with, written in a view box of its own, and it draws that path scaled out of the
// view box and into the box it was given. Everything else about it belongs to the rectangle
// underneath: the box, the fill, the stroke, the corner radius, the turn, the cursor, the
// appearance, the hit test of the box, and the bindings.
//
// It also carries the path children an imported SVG turns into. A vector scales its children with
// its box the way a group does: the size the art was authored at is held, and a box bigger or
// smaller than that is the art scaled out of it. So the document tree stays the layer tree and a
// click on the art lands on the child that was drawn, at whatever size the art was drawn.
//
// The shape it draws of its own is named by `shape`: a box, an ellipse, or nothing at all, which
// is what a vector holding only children draws. The ellipse it draws is the outline the ellipse
// component draws, and the box it draws is the box the rectangle underneath already draws, so
// neither outline is written twice.
class VectorElement : public RectangleElement {
public:
    NodeKind Kind() const override;
    Size Measure(TextMeasurer& measurer, float availableWidth, float availableHeight, bool heightDefinite) override;
    void Arrange(const Rect& rect) override;
    Element* HitTest(Point point) override;
    void ApplyBound(const std::string& property, const std::string& value) override;

    void SetPathData(std::string value);
    const std::string& PathData() const;
    void SetShape(std::string value);
    const std::string& Shape() const;
    void SetViewBox(float x, float y, float boxWidth, float boxHeight);
    // The view box is four numbers in one attribute, the way a path is one string of commands.
    void SetViewBox(const std::string& value);
    Rect ViewBox() const;
    bool HasViewBox() const { return declaredViewBox; }
    // Only the names it is given change the style, so a binding on one of the three leaves the
    // other two as they were.
    void SetLineStyle(const std::string& cap, const std::string& join, const std::string& rule);

    // True when the vector draws art of its own rather than only the children it holds.
    bool HasOwnGeometry() const;
    bool DrawsEllipse() const;
    bool DrawsBox() const;

protected:
    void OnPaint(Renderer& renderer) override;

private:
    std::string pathData;
    std::string shape;
    float viewX = 0.0f;
    float viewY = 0.0f;
    float viewWidth = 0.0f;
    float viewHeight = 0.0f;
    bool declaredViewBox = false;
    char lineCap = 'b';
    char lineJoin = 'r';
    bool evenOdd = false;
    // The size the children cover together when each one keeps the size it declares, which is the
    // size the art was authored at. The box the vector is finally given is scaled out of it.
    Size natural{0.0f, 0.0f};
};

}  // namespace Runtime
