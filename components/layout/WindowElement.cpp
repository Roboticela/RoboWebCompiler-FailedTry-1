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
    float flowY = bounds.y;
    for (const std::unique_ptr<Element>& child : children) {
        const Size size = child->Measured();
        if (child->PositionMode() == FramePosition::ABSOLUTE) {
            const float x = bounds.x + child->OffsetX().Resolve(bounds.width, 0.0f, true);
            const float y = bounds.y + child->OffsetY().Resolve(bounds.height, 0.0f, true);
            child->Arrange(Rect{x, y, size.width, size.height});
            continue;
        }
        child->Arrange(Rect{bounds.x, flowY, size.width, size.height});
        if (child->PositionMode() != FramePosition::FIXED) {
            flowY += size.height;
        }
    }
    for (const std::unique_ptr<Element>& child : children) {
        const Rect natural = child->Bounds();
        const Rect pinned = child->PinnedRect(bounds, natural);
        if (pinned.x != natural.x || pinned.y != natural.y || pinned.width != natural.width || pinned.height != natural.height) {
            child->Arrange(pinned);
        }
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
