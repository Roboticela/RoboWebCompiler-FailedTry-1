#include "components/layout/LayoutBoxes.h"

#include "ui/layout/FlexLayout.h"

#include <algorithm>
#include <cmath>

namespace Runtime {
namespace {

FlexMetrics MetricsFor(const Element& element, FlexDirection direction, JustifyContent justify, Alignment align, FlexWrap wrap, Edges padding,
                       Length gap) {
    FlexMetrics metrics;
    metrics.direction = direction;
    metrics.justify = justify;
    metrics.align = align;
    metrics.wrap = wrap;
    metrics.padding = padding;
    metrics.gap = gap;
    (void)element;
    return metrics;
}

}  // namespace

NodeKind Row::Kind() const {
    return NodeKind::ROW;
}

void Row::SetPadding(Edges value) {
    padding = value;
}

void Row::SetSpacing(Length value) {
    spacing = value;
}

void Row::SetAlignment(Alignment value) {
    alignment = value;
}

void Row::SetJustify(JustifyContent value) {
    justify = value;
}

Size Row::Measure(TextMeasurer& measurer, float availableWidth, float availableHeight, bool heightDefinite) {
    measured = MeasureFlex(*this, measurer, availableWidth, availableHeight, heightDefinite,
                           MetricsFor(*this, FlexDirection::ROW, justify, alignment, FlexWrap::NO_WRAP, padding, spacing));
    return FinishMeasure(availableWidth, availableHeight);
}

void Row::Arrange(const Rect& rect) {
    bounds = BorderBox(rect);
    ArrangeFlex(*this, bounds, MetricsFor(*this, FlexDirection::ROW, justify, alignment, FlexWrap::NO_WRAP, padding, spacing));
}

NodeKind FlexBox::Kind() const {
    return NodeKind::FLEX;
}

void FlexBox::SetDirection(FlexDirection value) {
    direction = value;
}

void FlexBox::SetJustify(JustifyContent value) {
    justify = value;
}

void FlexBox::SetAlignment(Alignment value) {
    alignment = value;
}

void FlexBox::SetWrap(FlexWrap value) {
    wrap = value;
}

void FlexBox::SetPadding(Edges value) {
    padding = value;
}

void FlexBox::SetGap(Length value) {
    gap = value;
}

Size FlexBox::Measure(TextMeasurer& measurer, float availableWidth, float availableHeight, bool heightDefinite) {
    measured = MeasureFlex(*this, measurer, availableWidth, availableHeight, heightDefinite,
                           MetricsFor(*this, direction, justify, alignment, wrap, padding, gap));
    return FinishMeasure(availableWidth, availableHeight);
}

void FlexBox::Arrange(const Rect& rect) {
    bounds = BorderBox(rect);
    ArrangeFlex(*this, bounds, MetricsFor(*this, direction, justify, alignment, wrap, padding, gap));
}

NodeKind Grid::Kind() const {
    return NodeKind::GRID;
}

void Grid::SetColumns(int value) {
    columns = std::max(1, value);
}

void Grid::SetPadding(Edges value) {
    padding = value;
}

void Grid::SetColumnGap(Length value) {
    columnGap = value;
}

void Grid::SetRowGap(Length value) {
    rowGap = value;
}

Size Grid::Measure(TextMeasurer& measurer, float availableWidth, float availableHeight, bool heightDefinite) {
    const int columnCount = std::max(1, columns);
    const float outerWidth = width.unit == LengthUnit::FIT ? availableWidth : width.Resolve(availableWidth, availableWidth);
    const float padX = padding.SpanX(outerWidth);
    const float padY = padding.SpanY(outerWidth);
    const float colGap = columnGap.Resolve(outerWidth, 0.0f);
    const float lineGap = rowGap.Resolve(outerWidth, 0.0f);
    const float innerWidth = std::max(0.0f, outerWidth - padX);
    const float cellWidth = columnCount <= 1 ? innerWidth : std::max(0.0f, (innerWidth - colGap * static_cast<float>(columnCount - 1)) / static_cast<float>(columnCount));
    float contentHeight = 0.0f;
    float rowHeight = 0.0f;
    int columnIndex = 0;
    for (const std::unique_ptr<Element>& child : children) {
        int span = std::min(columnCount, child->ColumnSpan());
        if (columnIndex > 0 && columnIndex + span > columnCount) {
            if (contentHeight > 0.0f) {
                contentHeight += lineGap;
            }
            contentHeight += rowHeight;
            rowHeight = 0.0f;
            columnIndex = 0;
        }
        const float spanWidth = cellWidth * static_cast<float>(span) + colGap * static_cast<float>(span - 1);
        const Size childSize = child->Measure(measurer, spanWidth, availableHeight, false);
        rowHeight = std::max(rowHeight, childSize.height);
        columnIndex += span;
        if (columnIndex >= columnCount) {
            if (contentHeight > 0.0f) {
                contentHeight += lineGap;
            }
            contentHeight += rowHeight;
            rowHeight = 0.0f;
            columnIndex = 0;
        }
    }
    if (columnIndex != 0) {
        if (contentHeight > 0.0f) {
            contentHeight += lineGap;
        }
        contentHeight += rowHeight;
    }
    const float fitHeight = contentHeight + padY;
    measured.width = width.unit == LengthUnit::FIT ? outerWidth : width.Resolve(availableWidth, outerWidth);
    measured.height = ResolveDefinite(height, availableHeight, fitHeight, heightDefinite);
    return FinishMeasure(availableWidth, availableHeight);
}

void Grid::Arrange(const Rect& rect) {
    bounds = BorderBox(rect);
    const int columnCount = std::max(1, columns);
    const float padLeft = padding.left.Resolve(bounds.width, 0.0f);
    const float padTop = padding.top.Resolve(bounds.width, 0.0f);
    const float padX = padding.SpanX(bounds.width);
    const float colGap = columnGap.Resolve(bounds.width, 0.0f);
    const float lineGap = rowGap.Resolve(bounds.width, 0.0f);
    const float innerWidth = std::max(0.0f, bounds.width - padX);
    const float cellWidth = columnCount <= 1 ? innerWidth : std::max(0.0f, (innerWidth - colGap * static_cast<float>(columnCount - 1)) / static_cast<float>(columnCount));
    float y = bounds.y + padTop;
    int index = 0;
    while (index < static_cast<int>(children.size())) {
        int columnIndex = 0;
        const int rowStart = index;
        float rowHeight = 0.0f;
        while (index < static_cast<int>(children.size())) {
            Element& child = *children[static_cast<std::size_t>(index)];
            const int span = std::min(columnCount - columnIndex, child.ColumnSpan());
            if (columnIndex > 0 && span < 1) {
                break;
            }
            rowHeight = std::max(rowHeight, child.Measured().height);
            columnIndex += std::max(1, span);
            ++index;
            if (columnIndex >= columnCount) {
                break;
            }
        }
        columnIndex = 0;
        for (int cursor = rowStart; cursor < index; ++cursor) {
            Element& child = *children[static_cast<std::size_t>(cursor)];
            const int span = std::max(1, std::min(columnCount - columnIndex, child.ColumnSpan()));
            const float spanWidth = cellWidth * static_cast<float>(span) + colGap * static_cast<float>(std::max(0, span - 1));
            const float x = bounds.x + padLeft + static_cast<float>(columnIndex) * (cellWidth + colGap);
            child.Arrange(Rect{x, y, spanWidth, child.Measured().height});
            columnIndex += span;
        }
        y += rowHeight + lineGap;
    }
}

NodeKind Stack::Kind() const {
    return NodeKind::STACK;
}

void Stack::SetPadding(Edges value) {
    padding = value;
}

Size Stack::Measure(TextMeasurer& measurer, float availableWidth, float availableHeight, bool heightDefinite) {
    const float outerWidth = width.unit == LengthUnit::FIT ? availableWidth : width.Resolve(availableWidth, availableWidth);
    const float padX = padding.SpanX(outerWidth);
    const float padY = padding.SpanY(outerWidth);
    const float innerWidth = std::max(0.0f, outerWidth - padX);
    float widest = 0.0f;
    float tallest = 0.0f;
    for (const std::unique_ptr<Element>& child : children) {
        const Size childSize = child->Measure(measurer, innerWidth, std::max(0.0f, availableHeight - padY), heightDefinite);
        widest = std::max(widest, childSize.width);
        tallest = std::max(tallest, childSize.height);
    }
    measured.width = width.unit == LengthUnit::FIT ? widest + padX : width.Resolve(availableWidth, widest + padX);
    measured.height = ResolveDefinite(height, availableHeight, tallest + padY, heightDefinite);
    return FinishMeasure(availableWidth, availableHeight);
}

void Stack::Arrange(const Rect& rect) {
    bounds = BorderBox(rect);
    const float padLeft = padding.left.Resolve(bounds.width, 0.0f);
    const float padTop = padding.top.Resolve(bounds.width, 0.0f);
    const float padX = padding.SpanX(bounds.width);
    const float padY = padding.SpanY(bounds.width);
    const Rect content{bounds.x + padLeft, bounds.y + padTop, std::max(0.0f, bounds.width - padX),
                       std::max(0.0f, bounds.height - padY)};
    for (const std::unique_ptr<Element>& child : children) {
        child->Arrange(content);
    }
}

}  // namespace Runtime
