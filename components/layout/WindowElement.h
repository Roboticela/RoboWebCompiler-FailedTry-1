#pragma once

#include "ui/element/Element.h"

namespace Runtime {

class WindowElement : public Element {
public:
    NodeKind Kind() const override;
    Size Measure(TextMeasurer& measurer, float availableWidth, float availableHeight, bool heightDefinite) override;
    void Arrange(const Rect& rect) override;
    void OnPaint(Renderer& renderer) override;
    Element* HitTest(Point point) override;
};

}  // namespace Runtime
