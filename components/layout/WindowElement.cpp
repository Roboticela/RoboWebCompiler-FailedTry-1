#include "components/layout/WindowElement.h"

#include "engine/math/Color.h"

namespace Runtime {

WindowElement::WindowElement() {
    SetBackgroundColor(COLOR_WINDOW_BACKGROUND);
    SetLayout(FrameLayout::VERTICAL);
}

NodeKind WindowElement::Kind() const {
    return NodeKind::WINDOW;
}

Element* WindowElement::HitTest(Point point) {
    if (Parent() == nullptr) {
        if (!Visible()) {
            return nullptr;
        }
        return HitTestChildren(point);
    }
    return Frame::HitTest(point);
}

void WindowElement::SetTitle(std::string value) {
    title = std::move(value);
}

const std::string& WindowElement::Title() const {
    return title;
}

}  // namespace Runtime
