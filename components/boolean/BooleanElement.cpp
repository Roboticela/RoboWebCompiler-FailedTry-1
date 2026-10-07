#include "components/boolean/BooleanElement.h"

#include "rendering/renderer/OutlineRecorder.h"
#include "rendering/renderer/Renderer.h"

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

namespace Runtime {

BooleanElement::BooleanElement() {
    // The rectangle underneath starts as a fixed square, because a bare rectangle has nothing
    // inside it to be sized by. A group does: its outline is the shapes it holds, so a group left
    // without a size of its own is as big as those shapes rather than as big as that square.
    SetWidth(Length::Fit());
    SetHeight(Length::Fit());
}

NodeKind BooleanElement::Kind() const {
    return NodeKind::BOOLEAN;
}

void BooleanElement::SetOperation(BooleanOperation value) {
    operation = value;
}

Size BooleanElement::Measure(TextMeasurer& measurer, float availableWidth, float availableHeight, bool) {
    // The shapes are measured first, because the box the group covers is the box they cover: a group
    // that was not given a size of its own is exactly as big as what it holds.
    natural = ChildrenExtent(measurer, availableWidth, availableHeight);
    return HuggedMeasure(availableWidth, availableHeight, natural);
}

void BooleanElement::Arrange(const Rect& rect) {
    Element::Arrange(rect);
    // The shapes scale with the box the way the art of an icon scales with its box, so growing the
    // group grows the parts it is made of instead of moving the box around them.
    ArrangeChildrenScaled(Bounds(), natural);
}

std::string BooleanElement::CombinedOutline() {
    const Rect box = Bounds();
    if (box.width <= 0.0f || box.height <= 0.0f) {
        return std::string();
    }
    std::vector<Element*> order;
    order.reserve(children.size());
    for (const std::unique_ptr<Element>& child : children) {
        order.push_back(child.get());
    }
    // The shapes are read in the order they are drawn, so the shape at the bottom of the stack is
    // the one a subtraction takes the others away from, which is the shape a person sees underneath.
    std::stable_sort(order.begin(), order.end(), [](const Element* left, const Element* right) { return left->ZIndex() < right->ZIndex(); });
    std::vector<std::vector<PathContour>> shapes;
    for (Element* child : order) {
        if (!child->Visible()) {
            continue;
        }
        // Painting a shape into a recorder is how its outline is read: a box of rounded corners, a
        // circle, a ring of sides, a path of its own, and the turn it was given all arrive as the
        // outline the shape covers, written from the corner of the box the group draws in.
        OutlineRecorder recorder;
        recorder.SetOrigin(Point{box.x, box.y});
        child->Paint(recorder);
        if (!recorder.Outlines().empty()) {
            shapes.push_back(recorder.Outlines());
        }
    }
    if (shapes.empty()) {
        return std::string();
    }
    return CombineOutlines(shapes, operation);
}

void BooleanElement::OnPaint(Renderer& renderer) {
    // The group is one shape, so it draws the outline its shapes add up to in its own fill and
    // stroke. What each shape was filled and stroked with is what that shape looked like before it
    // was combined, so none of it is drawn here: the combined outline is the shape now.
    const std::string outline = CombinedOutline();
    if (outline.empty()) {
        return;
    }
    PaintOutline(renderer, outline);
}

Element* BooleanElement::HitTest(Point point) {
    if (!Visible() || !ContainsPoint(Bounds(), point)) {
        return nullptr;
    }
    const Rect box = Bounds();
    const std::string outline = CombinedOutline();
    // A part of a shape the operation took away is not part of the group, so a click there belongs
    // to whatever stands behind the group rather than to the group.
    if (outline.empty() || !OutlinesContain(SampleContours(outline), Point{point.x - box.x, point.y - box.y})) {
        return nullptr;
    }
    // A shape keeps its own interaction in the part the combination kept, and the rest of the
    // outline belongs to the group, which is what makes a combination selectable and movable as one.
    if (Element* child = HitTestChildren(point)) {
        return child;
    }
    return this;
}

void BooleanElement::ApplyBound(const std::string& property, const std::string& value) {
    RectangleElement::ApplyBound(property, value);
    if (property == "operation") {
        operation = ParseBooleanOperation(value);
    }
}

}  // namespace Runtime
