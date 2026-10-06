#include "components/layout/BoardElement.h"

namespace Runtime {

BoardElement::BoardElement() {
    SetBackgroundColor(COLOR_WINDOW_BACKGROUND);
    SetLayout(FrameLayout::VERTICAL);
}

NodeKind BoardElement::Kind() const {
    return NodeKind::BOARD;
}

}  // namespace Runtime
