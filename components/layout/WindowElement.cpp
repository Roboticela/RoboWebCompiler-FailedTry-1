#include "components/layout/WindowElement.h"

#include "engine/math/Color.h"
#include "rendering/renderer/Renderer.h"

namespace Runtime {

NodeKind WindowElement::Kind() const {
    return NodeKind::WINDOW;
}

Size WindowElement::Measure(TextMeasurer& measurer, float availableWidth, float availableHeight, bool) {
    for (const std::unique_ptr<Element>& child : children) {
        child->Measure(measurer, availableWidth, availableHeight, true);
    }
    measured = Size{availableWidth, availableHeight};
    return FinishMeasure(availableWidth, availableHeight);
}

void WindowElement::Arrange(const Rect& rect) {
    bounds = BorderBox(rect);
    float y = bounds.y;
    for (const std::unique_ptr<Element>& child : children) {
        const Size size = child->Measured();
        child->Arrange(Rect{bounds.x, y, size.width, size.height});
        y += size.height;
    }
}

void WindowElement::OnPaint(Renderer& renderer) {
    renderer.FillRectangle(bounds, COLOR_WINDOW_BACKGROUND, 0.0f);
    PaintChildren(renderer);
}

Element* WindowElement::HitTest(Point point) {
    if (!visible) {
        return nullptr;
    }
    return HitTestChildren(point);
}

}  // namespace Runtime
