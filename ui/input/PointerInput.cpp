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

bool PointerInput::OnUp(Element& root, Point point) {
    Element* released = root.HitTest(point);
    bool activated = false;
    if (pressed != nullptr && pressed == released) {
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
    return hovered != nullptr && hovered->WantsHandCursor();
}

Element* PointerInput::Pressed() const {
    return pressed;
}

Element* PointerInput::Hovered() const {
    return hovered;
}

}  // namespace Runtime
