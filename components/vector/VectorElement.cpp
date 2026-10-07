#include "components/vector/VectorElement.h"

#include "components/ellipse/EllipseElement.h"
#include "engine/math/Geometry.h"
#include "rendering/renderer/Renderer.h"

#include <cctype>
#include <cmath>
#include <cstdlib>
#include <string_view>

namespace Runtime {
namespace {

// A turn of less than this is the same as no turn at all, so it is not worth a transform.
constexpr float TURN_EPSILON = 0.01f;

std::string_view TrimView(std::string_view text) {
    std::size_t begin = 0;
    std::size_t end = text.size();
    const auto blank = [](char value) { return std::isspace(static_cast<unsigned char>(value)) != 0; };
    while (begin < end && blank(text[begin])) {
        ++begin;
    }
    while (end > begin && blank(text[end - 1])) {
        --end;
    }
    return text.substr(begin, end - begin);
}

}  // namespace

NodeKind VectorElement::Kind() const {
    return NodeKind::VECTOR;
}

void VectorElement::SetPathData(std::string value) {
    pathData = std::move(value);
}

const std::string& VectorElement::PathData() const {
    return pathData;
}

void VectorElement::SetShape(std::string value) {
    shape = std::string(TrimView(value));
}

const std::string& VectorElement::Shape() const {
    return shape;
}

void VectorElement::SetViewBox(float x, float y, float boxWidth, float boxHeight) {
    // A view box with no area in it says nothing about where the geometry is written, so it is
    // refused rather than kept as a box that would divide by zero on the way to the box.
    if (boxWidth <= 0.0f || boxHeight <= 0.0f) {
        return;
    }
    viewX = x;
    viewY = y;
    viewWidth = boxWidth;
    viewHeight = boxHeight;
    declaredViewBox = true;
}

void VectorElement::SetViewBox(const std::string& value) {
    const char* scan = value.c_str();
    char* end = nullptr;
    const float x = std::strtof(scan, &end);
    if (end == scan) {
        return;
    }
    scan = end;
    const float y = std::strtof(scan, &end);
    if (end == scan) {
        return;
    }
    scan = end;
    const float boxWidth = std::strtof(scan, &end);
    if (end == scan) {
        return;
    }
    scan = end;
    const float boxHeight = std::strtof(scan, &end);
    if (end == scan) {
        return;
    }
    SetViewBox(x, y, boxWidth, boxHeight);
}

Rect VectorElement::ViewBox() const {
    return Rect{viewX, viewY, viewWidth, viewHeight};
}

void VectorElement::SetLineStyle(const std::string& cap, const std::string& join, const std::string& rule) {
    if (!cap.empty()) {
        lineCap = cap == "round" ? 'r' : cap == "square" ? 's' : 'b';
    }
    if (!join.empty()) {
        lineJoin = join == "round" ? 'r' : join == "bevel" ? 'b' : 'm';
    }
    if (!rule.empty()) {
        evenOdd = rule == "evenodd";
    }
}

bool VectorElement::DrawsEllipse() const {
    return shape == "ellipse" || shape == "circle";
}

bool VectorElement::DrawsBox() const {
    return shape == "rect" || shape == "rectangle" || shape == "box";
}

bool VectorElement::HasOwnGeometry() const {
    return !pathData.empty() || DrawsEllipse() || DrawsBox();
}

Size VectorElement::Measure(TextMeasurer& measurer, float availableWidth, float availableHeight, bool heightDefinite) {
    // A vector holds the path children of an imported icon, and a child can only be placed once
    // it has been measured, whether or not the vector was given a size of its own. The size the
    // children cover together is the size the art was authored at, which is what Arrange scales
    // the art out of.
    natural = ChildrenExtent(measurer, availableWidth, availableHeight);
    return RectangleElement::Measure(measurer, availableWidth, availableHeight, heightDefinite);
}

void VectorElement::Arrange(const Rect& rect) {
    Element::Arrange(rect);
    // The art scales with the box, so growing or shrinking the vector draws the icon larger or
    // smaller instead of leaving the art at the size it was authored at inside a box that moved
    // on its own. A vector left at its authored size places every child exactly as declared.
    ArrangeChildrenScaled(Bounds(), natural);
}

Element* VectorElement::HitTest(Point point) {
    if (!Visible()) {
        return nullptr;
    }
    // The art of an imported icon belongs to the path that was drawn, so a click lands on that
    // path, and the area the art does not reach still belongs to the vector, so a click beside the
    // art moves the icon as one rather than picking a path out of it.
    if (Element* child = HitTestChildren(point)) {
        return child;
    }
    return RectangleElement::HitTest(point);
}

void VectorElement::ApplyBound(const std::string& property, const std::string& value) {
    RectangleElement::ApplyBound(property, value);
    if (property == "d") {
        SetPathData(value);
    } else if (property == "shape") {
        SetShape(value);
    } else if (property == "viewBox") {
        SetViewBox(value);
    } else if (property == "lineCap") {
        SetLineStyle(value, std::string(), std::string());
    } else if (property == "lineJoin") {
        SetLineStyle(std::string(), value, std::string());
    } else if (property == "fillRule") {
        SetLineStyle(std::string(), std::string(), value);
    }
}

void VectorElement::OnPaint(Renderer& renderer) {
    const Rect area = Bounds();
    if (area.width > 0.0f && area.height > 0.0f) {
        if (!pathData.empty()) {
            // The path was written in the view box, so it is drawn in it. A vector with no view
            // box of its own draws in box-local pixels, the space every other shape writes in.
            OutlineStyle style;
            if (declaredViewBox) {
                style.viewX = viewX;
                style.viewY = viewY;
                style.viewWidth = viewWidth;
                style.viewHeight = viewHeight;
            }
            style.evenOdd = evenOdd;
            style.lineCap = lineCap;
            style.lineJoin = lineJoin;
            PaintOutline(renderer, pathData, style);
        } else if (DrawsEllipse()) {
            // The outline an ellipse is drawn with is the ellipse's, so it is borrowed rather
            // than written a second time.
            PaintOutline(renderer, EllipseOutline(area.width, area.height));
        } else if (DrawsBox()) {
            // The box is what the rectangle underneath already draws, corners and all.
            RectangleElement::OnPaint(renderer);
        }
    }
    // The art a vector holds turns with the vector the way the art a group holds turns with the
    // group, so a turned icon draws the paths it was made of turned rather than leaving them at
    // the position they were placed at. The path a vector draws of its own already turns with the
    // box it is drawn in, so only the children are turned here.
    const float turn = Rotation();
    const bool turned = std::fabs(turn) > TURN_EPSILON;
    if (turned) {
        renderer.PushRotation(turn, area.x + area.width * 0.5f, area.y + area.height * 0.5f);
    }
    PaintChildren(renderer);
    if (turned) {
        renderer.PopTransform();
    }
}

}  // namespace Runtime
