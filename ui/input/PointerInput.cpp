#include "ui/input/PointerInput.h"

#include "ui/element/Element.h"

namespace Runtime {

bool PointerInput::OnMove(Element& root, Point point) {
    bool dragged = false;
    if (pressed != nullptr) {
        dragged = pressed->OnDrag(point);
        if (!dragged) {
            for (Element* node = pressed->Parent(); node != nullptr; node = node->Parent()) {
                if (node->OnDrag(point)) {
                    dragged = true;
                    break;
                }
            }
        }
    }
    Element* next = root.HitTest(point);
    if (hovered == next) {
        return dragged;
    }
    if (hovered != nullptr) {
        hovered->SetPointerHover(false);
    }
    hovered = next;
    if (hovered != nullptr) {
        hovered->SetPointerHover(true);
    }
    return true;
}

bool PointerInput::OnDown(Element& root, Point point) {
    pressed = root.HitTest(point);
    if (pressed != nullptr) {
        pressed->OnPointerDown(point);
        return true;
    }
    return false;
}

bool PointerInput::OnUp(Element& root, Point point, bool activate) {
    Element* released = root.HitTest(point);
    bool activated = false;
    if (activate && pressed != nullptr && pressed == released) {
        pressed->Activate();
        activated = true;
    }
    if (pressed != nullptr) {
        pressed->SetPointerPressed(false);
        pressed = nullptr;
        return true;
    }
    return activated;
}

bool PointerInput::OnLeave() {
    bool changed = false;
    if (hovered != nullptr) {
        hovered->SetPointerHover(false);
        hovered = nullptr;
        changed = true;
    }
    if (pressed != nullptr) {
        pressed->SetPointerPressed(false);
        pressed = nullptr;
        changed = true;
    }
    return changed;
}

bool PointerInput::HoverWantsHand() const {
    // A cursor belongs to the node that asks for it and to everything inside that node, so
    // a group, a frame, or any other container hands its cursor to its children the way a
    // group hands down every other property it carries.
    for (Element* node = hovered; node != nullptr; node = node->Parent()) {
        if (node->WantsHandCursor()) {
            return true;
        }
    }
    return false;
}

Element* PointerInput::Pressed() const {
    return pressed;
}

Element* PointerInput::Hovered() const {
    return hovered;
}

}  // namespace Runtime
