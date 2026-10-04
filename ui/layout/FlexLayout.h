#pragma once

#include "engine/types/UiTypes.h"
#include "ui/element/Element.h"

namespace Runtime {

struct FlexMetrics {
    FlexDirection direction = FlexDirection::ROW;
    JustifyContent justify = JustifyContent::START;
    Alignment align = Alignment::START;
    FlexWrap wrap = FlexWrap::NO_WRAP;
    Edges padding{};
    Length gap = Length::Pixels(0.0f);
};

Size MeasureFlex(Element& element, TextMeasurer& measurer, float availableWidth, float availableHeight, bool heightDefinite,
                 const FlexMetrics& metrics);
void ArrangeFlex(Element& element, const Rect& rect, const FlexMetrics& metrics);

}  // namespace Runtime
