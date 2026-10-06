#pragma once

#include "engine/math/Geometry.h"

namespace Runtime {

class Element;

class PointerInput {
public:
    bool OnMove(Element& root, Point point);
    bool OnDown(Element& root, Point point);
    bool OnUp(Element& root, Point point, bool activate = true);
    bool OnLeave();
    bool HoverWantsHand() const;
    Element* Pressed() const;
    Element* Hovered() const;

private:
    Element* hovered = nullptr;
    Element* pressed = nullptr;
};

}  // namespace Runtime
