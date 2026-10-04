#pragma once

#include "engine/math/Geometry.h"

namespace Runtime {

class Element;

class PointerInput {
public:
    bool OnMove(Element& root, Point point);
    bool OnDown(Element& root, Point point);
    bool OnUp(Element& root, Point point);
    bool OnLeave();
    bool HoverWantsHand() const;
    Element* Pressed() const;

private:
    Element* hovered = nullptr;
    Element* pressed = nullptr;
};

}  // namespace Runtime
