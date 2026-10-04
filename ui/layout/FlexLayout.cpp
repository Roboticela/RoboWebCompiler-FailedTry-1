#include "ui/layout/FlexLayout.h"

#include <algorithm>
#include <vector>

namespace Runtime {
namespace {

struct FlexLine {
    int first = 0;
    int count = 0;
    float main = 0.0f;
    float cross = 0.0f;
};

float MainSize(const Size& size, bool horizontal) {
    return horizontal ? size.width : size.height;
}

float CrossSize(const Size& size, bool horizontal) {
    return horizontal ? size.height : size.width;
}

}  // namespace

Size MeasureFlex(Element& element, TextMeasurer& measurer, float availableWidth, float availableHeight, bool heightDefinite,
                 const FlexMetrics& metrics) {
    const bool horizontal = metrics.direction == FlexDirection::ROW;
    const float outerWidth = element.Width().unit == LengthUnit::FIT ? availableWidth : element.Width().Resolve(availableWidth, availableWidth);
    const float padX = metrics.padding.SpanX(outerWidth);
    const float padY = metrics.padding.SpanY(outerWidth);
    const float gap = metrics.gap.Resolve(outerWidth, 0.0f);
    const float innerWidth = std::max(0.0f, outerWidth - padX);
    const float innerHeight = std::max(0.0f, availableHeight - padY);
    const float childLimit = horizontal ? innerWidth : innerHeight;

    float contentMain = 0.0f;
    float contentCross = 0.0f;
    const bool childHeightDefinite = heightDefinite && element.Height().unit != LengthUnit::FIT;
    const bool mainDefinite = horizontal || childHeightDefinite;
    const float mainAvailable = horizontal ? innerWidth : innerHeight;
    if (metrics.wrap == FlexWrap::NO_WRAP && mainDefinite) {
        float fixed = 0.0f;
        int fillCount = 0;
        const int childCount = static_cast<int>(element.Children().size());
        for (const std::unique_ptr<Element>& child : element.Children()) {
            const Length mainLength = horizontal ? child->Width() : child->Height();
            if (mainLength.unit == LengthUnit::FILL) {
                ++fillCount;
                continue;
            }
            const Size childSize = child->Measure(measurer, innerWidth, horizontal ? innerHeight : innerHeight, childHeightDefinite);
            fixed += MainSize(childSize, horizontal);
            contentCross = std::max(contentCross, CrossSize(childSize, horizontal));
        }
        const float gaps = childCount > 1 ? gap * static_cast<float>(childCount - 1) : 0.0f;
        float fillMain = 0.0f;
        if (fillCount > 0) {
            const float remaining = std::max(0.0f, mainAvailable - fixed - gaps);
            const float share = remaining / static_cast<float>(fillCount);
            fillMain = remaining;
            for (const std::unique_ptr<Element>& child : element.Children()) {
                const Length mainLength = horizontal ? child->Width() : child->Height();
                if (mainLength.unit != LengthUnit::FILL) {
                    continue;
                }
                const Size childSize = horizontal ? child->Measure(measurer, share, innerHeight, childHeightDefinite)
                                                   : child->Measure(measurer, innerWidth, share, true);
                contentCross = std::max(contentCross, CrossSize(childSize, horizontal));
            }
        }
        contentMain = fixed + fillMain + gaps;
    } else {
        float lineMain = 0.0f;
        float lineCross = 0.0f;
        int lineCount = 0;
        for (const std::unique_ptr<Element>& child : element.Children()) {
            const Size childSize = child->Measure(measurer, innerWidth, innerHeight, childHeightDefinite);
            const float main = MainSize(childSize, horizontal);
            const float cross = CrossSize(childSize, horizontal);
            const bool wrap = metrics.wrap == FlexWrap::WRAP && lineCount > 0 && lineMain + gap + main > childLimit && childLimit > 0.0f;
            if (wrap) {
                contentMain = std::max(contentMain, lineMain);
                contentCross += lineCross;
                if (contentCross > 0.0f) {
                    contentCross += gap;
                }
                lineMain = main;
                lineCross = cross;
                lineCount = 1;
            } else {
                if (lineCount > 0) {
                    lineMain += gap;
                }
                lineMain += main;
                lineCross = std::max(lineCross, cross);
                ++lineCount;
            }
        }
        contentMain = std::max(contentMain, lineMain);
        contentCross += lineCross;
    }

    const float fitWidth = padX + (horizontal ? contentMain : contentCross);
    const float fitHeight = padY + (horizontal ? contentCross : contentMain);
    Size measured;
    measured.width = element.Width().unit == LengthUnit::FIT ? fitWidth : element.Width().Resolve(availableWidth, fitWidth);
    measured.height = ResolveDefinite(element.Height(), availableHeight, fitHeight, heightDefinite);
    return measured;
}

void ArrangeFlex(Element& element, const Rect& rect, const FlexMetrics& metrics) {
    const bool horizontal = metrics.direction == FlexDirection::ROW;
    const float padLeft = metrics.padding.left.Resolve(rect.width, 0.0f);
    const float padTop = metrics.padding.top.Resolve(rect.width, 0.0f);
    const float padX = metrics.padding.SpanX(rect.width);
    const float padY = metrics.padding.SpanY(rect.width);
    const float gap = metrics.gap.Resolve(rect.width, 0.0f);
    const float innerWidth = std::max(0.0f, rect.width - padX);
    const float innerHeight = std::max(0.0f, rect.height - padY);
    const float innerMain = horizontal ? innerWidth : innerHeight;
    const float innerCross = horizontal ? innerHeight : innerWidth;

    std::vector<FlexLine> lines;
    FlexLine line;
    for (int index = 0; index < static_cast<int>(element.Children().size()); ++index) {
        const Size size = element.Children()[static_cast<std::size_t>(index)]->Measured();
        const float main = MainSize(size, horizontal);
        const float cross = CrossSize(size, horizontal);
        const float next = line.count == 0 ? main : line.main + gap + main;
        if (metrics.wrap == FlexWrap::WRAP && line.count > 0 && next > innerMain) {
            lines.push_back(line);
            line = FlexLine{index, 1, main, cross};
        } else {
            if (line.count == 0) {
                line.first = index;
            }
            line.main = next;
            line.cross = std::max(line.cross, cross);
            ++line.count;
        }
    }
    if (line.count > 0 || element.Children().empty()) {
        if (line.count > 0) {
            lines.push_back(line);
        }
    }

    float crossCursor = horizontal ? rect.y + padTop : rect.x + padLeft;
    for (const FlexLine& item : lines) {
        const float free = std::max(0.0f, innerMain - item.main);
        float growSum = 0.0f;
        for (int offset = 0; offset < item.count; ++offset) {
            growSum += element.Children()[static_cast<std::size_t>(item.first + offset)]->Grow();
        }
        float mainCursor = horizontal ? rect.x + padLeft : rect.y + padTop;
        float extra = 0.0f;
        if (growSum <= 0.0f && metrics.justify == JustifyContent::CENTER) {
            mainCursor += free * 0.5f;
        } else if (growSum <= 0.0f && metrics.justify == JustifyContent::END) {
            mainCursor += free;
        } else if (growSum <= 0.0f && metrics.justify == JustifyContent::SPACE_BETWEEN && item.count > 1) {
            extra = free / static_cast<float>(item.count - 1);
        }
        for (int offset = 0; offset < item.count; ++offset) {
            Element& child = *element.Children()[static_cast<std::size_t>(item.first + offset)];
            const Size size = child.Measured();
            float childMain = MainSize(size, horizontal);
            if (growSum > 0.0f && child.Grow() > 0.0f) {
                childMain += free * (child.Grow() / growSum);
            }
            float childCross = CrossSize(size, horizontal);
            if (metrics.align == Alignment::STRETCH && (horizontal ? child.Height().unit : child.Width().unit) == LengthUnit::FIT) {
                childCross = item.cross;
            }
            float crossPos = crossCursor;
            if (metrics.align == Alignment::CENTER) {
                crossPos += (item.cross - childCross) * 0.5f;
            } else if (metrics.align == Alignment::END) {
                crossPos += item.cross - childCross;
            }
            if (horizontal) {
                child.Arrange(Rect{mainCursor, crossPos, childMain, childCross});
                mainCursor += childMain + gap + extra;
            } else {
                child.Arrange(Rect{crossPos, mainCursor, childCross, childMain});
                mainCursor += childMain + gap + extra;
            }
        }
        crossCursor += item.cross + gap;
    }
    (void)innerCross;
}

}  // namespace Runtime
