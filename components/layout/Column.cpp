#include "components/layout/Column.h"

#include <algorithm>

namespace Runtime {

NodeKind Column::Kind() const {
    return NodeKind::COLUMN;
}

void Column::SetPadding(Edges value) {
    padding = value;
}

void Column::SetSpacing(Length value) {
    spacing = value;
}

void Column::SetAlignment(Alignment value) {
    alignment = value;
}

Size Column::Measure(TextMeasurer& measurer, float availableWidth, float availableHeight, bool heightDefinite) {
    const float outerWidth =
        width.unit == LengthUnit::FIT ? availableWidth : width.Resolve(availableWidth, availableWidth);
    const float padX = padding.SpanX(outerWidth);
    const float padY = padding.SpanY(outerWidth);
    const float space = spacing.Resolve(outerWidth, 0.0f);
    const float innerWidth = std::max(0.0f, outerWidth - padX);
    const bool childHeightDefinite = heightDefinite && height.unit != LengthUnit::FIT;
    const float innerHeight = std::max(0.0f, availableHeight - padY);

    float fixedHeight = 0.0f;
    float maxChildWidth = 0.0f;
    int fillCount = 0;
    for (const std::unique_ptr<Element>& child : children) {
        if (childHeightDefinite && child->Height().unit == LengthUnit::FILL) {
            ++fillCount;
            continue;
        }
        const Size childSize = child->Measure(measurer, innerWidth, innerHeight, childHeightDefinite);
        fixedHeight += childSize.height;
        maxChildWidth = std::max(maxChildWidth, childSize.width);
    }
    const int childCount = static_cast<int>(children.size());
    const float gaps = childCount > 1 ? space * static_cast<float>(childCount - 1) : 0.0f;
    float fillHeight = 0.0f;
    if (fillCount > 0) {
        const float remaining = std::max(0.0f, innerHeight - fixedHeight - gaps);
        const float share = remaining / static_cast<float>(fillCount);
        fillHeight = remaining;
        for (const std::unique_ptr<Element>& child : children) {
            if (child->Height().unit != LengthUnit::FILL) {
                continue;
            }
            const Size childSize = child->Measure(measurer, innerWidth, share, true);
            maxChildWidth = std::max(maxChildWidth, childSize.width);
        }
    }
    const float contentHeight = fixedHeight + fillHeight + gaps;

    const float fitWidth = maxChildWidth + padX;
    const float fitHeight = contentHeight + padY;
    measured.width = width.unit == LengthUnit::FIT ? fitWidth : width.Resolve(availableWidth, fitWidth);
    measured.height = ResolveDefinite(height, availableHeight, fitHeight, heightDefinite);
    return FinishMeasure(availableWidth, availableHeight);
}

void Column::Arrange(const Rect& rect) {
    bounds = BorderBox(rect);
    const float padLeft = padding.left.Resolve(bounds.width, 0.0f);
    const float padRight = padding.right.Resolve(bounds.width, 0.0f);
    const float padTop = padding.top.Resolve(bounds.width, 0.0f);
    const float space = spacing.Resolve(bounds.width, 0.0f);
    const float innerWidth = std::max(0.0f, bounds.width - padLeft - padRight);
    float y = bounds.y + padTop;
    for (const std::unique_ptr<Element>& child : children) {
        float childWidth = child->Measured().width;
        const float childHeight = child->Measured().height;
        if (alignment == Alignment::STRETCH && child->Width().unit == LengthUnit::FIT) {
            childWidth = innerWidth;
        }
        float x = bounds.x + padLeft;
        if (alignment == Alignment::CENTER) {
            x = bounds.x + padLeft + (innerWidth - childWidth) * 0.5f;
        } else if (alignment == Alignment::END) {
            x = bounds.x + padLeft + innerWidth - childWidth;
        }
        child->Arrange(Rect{x, y, childWidth, childHeight});
        y += childHeight + space;
    }
}

}  // namespace Runtime
