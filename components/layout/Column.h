#pragma once

#include "ui/element/Element.h"

namespace Runtime {

class Column : public Element {
public:
    NodeKind Kind() const override;
    Size Measure(TextMeasurer& measurer, float availableWidth, float availableHeight, bool heightDefinite) override;
    void Arrange(const Rect& rect) override;

    void SetPadding(Edges value);
    void SetSpacing(Length value);
    void SetAlignment(Alignment value);

private:
    Edges padding{};
    Length spacing = Length::Pixels(0.0f);
    Alignment alignment = Alignment::START;
};

}  // namespace Runtime
