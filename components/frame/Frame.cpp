#include "components/frame/Frame.h"

#include "compiler/types/ValueParser.h"
#include "rendering/renderer/Renderer.h"

#include <algorithm>
#include <cmath>

namespace Runtime {
namespace {

bool IsFrame(const Element& element) {
    return dynamic_cast<const Frame*>(&element) != nullptr;
}

const Frame* AsFrame(const Element& element) {
    return IsFrame(element) ? static_cast<const Frame*>(&element) : nullptr;
}

Frame* AsFrame(Element& element) {
    return IsFrame(element) ? static_cast<Frame*>(&element) : nullptr;
}

bool LeavesFlow(const Element& element) {
    const FramePosition position = element.PositionMode();
    return position == FramePosition::ABSOLUTE || position == FramePosition::FIXED;
}

float GapOf(const Length& specific, const Length& shared, float available) {
    if (specific.unit != LengthUnit::FIT) {
        return specific.Resolve(available, 0.0f);
    }
    return shared.Resolve(available, 0.0f);
}

Length ModeLength(Length length, SizeMode mode) {
    switch (mode) {
    case SizeMode::FIXED:
        return Length::Pixels(length.unit == LengthUnit::PERCENT ? length.value : (length.unit == LengthUnit::FIT ? 0.0f : length.value));
    case SizeMode::FILL:
        return Length::Fill();
    case SizeMode::FIT:
        return Length::Fit();
    case SizeMode::PERCENT:
        return Length::Percent(length.unit == LengthUnit::FIT ? 100.0f : length.value);
    case SizeMode::UNSET:
        return length;
    }
    return length;
}

}  // namespace

NodeKind Frame::Kind() const {
    return NodeKind::FRAME;
}

void Frame::SetName(std::string value) { name = std::move(value); }
void Frame::SetClassName(std::string value) { className = std::move(value); }
void Frame::SetComponentName(std::string value) { componentName = std::move(value); }
void Frame::SetInstanceName(std::string value) { instanceName = std::move(value); }
void Frame::SetTag(std::string value) { tag = std::move(value); }
void Frame::SetRole(std::string value) { role = std::move(value); }
void Frame::SetWidthMode(SizeMode value) { widthMode = value; }
void Frame::SetHeightMode(SizeMode value) { heightMode = value; }
void Frame::SetAspectRatio(float value) { aspectRatio = value; }
void Frame::SetPositionMode(FramePosition value) {
    positionMode = value;
    Element::SetPositionMode(value);
}
void Frame::SetAnchor(FrameAnchor value) { anchor = value; }
void Frame::SetPadding(Edges value) { padding = value; }

float Frame::ContentX() const {
    return Bounds().x + padding.left.Resolve(Bounds().width, 0.0f);
}

float Frame::ContentY() const {
    return Bounds().y + padding.top.Resolve(Bounds().width, 0.0f) - (Scrolls() ? scrollY : 0.0f);
}

float Frame::ContentWidth() const {
    return std::max(0.0f, Bounds().width - padding.SpanX(Bounds().width));
}

float Frame::ContentHeight() const {
    return std::max(0.0f, Bounds().height - padding.SpanY(Bounds().width));
}

Edges Frame::Padding() const {
    return padding;
}
void Frame::SetLayout(FrameLayout value) { layout = value; }
void Frame::SetShrink(float value) { shrink = value; }
void Frame::SetBasis(Length value) { basis = value; }
void Frame::SetGap(Length value) { gap = value; }
void Frame::SetRowGap(Length value) { rowGap = value; }
void Frame::SetColumnGap(Length value) { columnGap = value; }
void Frame::SetAlign(Alignment value) { align = value; }
void Frame::SetJustify(Alignment value) { justify = value; }
void Frame::SetJustifyBetween(bool value) { justifyBetween = value; }
void Frame::SetWrap(bool value) { wrap = value; }
void Frame::SetColumns(int value) { columns = std::max(1, value); }
void Frame::SetRows(int value) { rows = std::max(0, value); }
void Frame::SetColumn(int value) { column = std::max(0, value); }
void Frame::SetRow(int value) { row = std::max(0, value); }
void Frame::SetRowSpan(int value) { rowSpan = std::max(1, value); }
void Frame::SetBackgroundColor(Color value) { hasBackground = true; backgroundColor = value; }
void Frame::SetBackgroundImage(std::string value) { backgroundImage = std::move(value); }
void Frame::SetGradient(std::string value) { gradient = std::move(value); }
void Frame::SetBorderStyle(std::string value) { borderStyle = std::move(value); }
void Frame::SetSideBorder(int side, float widthValue) { if (side >= 0 && side < 4) { sideBorder[side] = widthValue; } }
void Frame::SetRadius(float value) { for (float& corner : cornerRadius) { corner = value; } }
void Frame::SetCornerRadius(int corner, float value) { if (corner >= 0 && corner < 4) { cornerRadius[corner] = value; } }
void Frame::SetShadow(FrameShadow value) { shadows.clear(); shadows.push_back(value); }
void Frame::AddShadow(FrameShadow value) { shadows.push_back(value); }
void Frame::SetOverflow(OverflowMode value) { overflow = value; }
void Frame::SetClip(bool value) { clip = value; }
void Frame::SetClipRadius(float value) { clipRadius = value; }
void Frame::SetClipPath(std::string value) { clipPath = std::move(value); }
void Frame::SetTranslate(float x, float y) { translateX = x; translateY = y; }
void Frame::SetScale(float x, float y) { scaleX = x; scaleY = y; }
void Frame::SetRotation(float x, float y, float z) { rotationX = x; rotationY = y; rotationZ = z; }
void Frame::SetSkew(float x, float y) { skewX = x; skewY = y; }
void Frame::SetTransformOrigin(std::string value) { transformOrigin = std::move(value); }
void Frame::SetDisplay(FrameDisplay value) { display = value; }
void Frame::SetBlendMode(std::string value) { blendMode = std::move(value); }
void Frame::SetEffects(float blurAmount, float backdrop, float bright, float contrastAmount, float saturationAmount, float gray, float hue) {
    blur = blurAmount;
    backdropBlur = backdrop;
    brightness = bright;
    contrast = contrastAmount;
    saturation = saturationAmount;
    grayscale = gray;
    hueRotate = hue;
}
void Frame::SetCursor(std::string value) { cursor = std::move(value); }

const std::string& Frame::Name() const { return name; }
const std::string& Frame::ClassName() const { return className; }
const std::string& Frame::ComponentName() const { return componentName; }
const std::string& Frame::InstanceName() const { return instanceName; }
const std::string& Frame::Tag() const { return tag; }
const std::string& Frame::Role() const { return role; }
FrameLayout Frame::Layout() const { return layout; }
FramePosition Frame::PositionMode() const { return positionMode; }
FrameAnchor Frame::Anchor() const { return anchor; }
OverflowMode Frame::Overflow() const { return overflow; }
FrameDisplay Frame::Display() const { return display; }
const std::string& Frame::Cursor() const { return cursor; }
const std::string& Frame::BlendMode() const { return blendMode; }
const std::string& Frame::Gradient() const { return gradient; }
const std::string& Frame::BackgroundImage() const { return backgroundImage; }
Color Frame::BackgroundColor() const { return backgroundColor; }
float Frame::CornerRadius(int corner) const { return corner >= 0 && corner < 4 ? cornerRadius[corner] : 0.0f; }
const std::vector<FrameShadow>& Frame::Shadows() const { return shadows; }
float Frame::Blur() const { return blur; }
bool Frame::Clips() const { return clip || overflow == OverflowMode::HIDDEN || overflow == OverflowMode::SCROLL || overflow == OverflowMode::AUTO; }
bool Frame::OutOfFlow() const { return positionMode == FramePosition::ABSOLUTE || positionMode == FramePosition::FIXED; }
bool Frame::Scrolls() const { return overflow == OverflowMode::SCROLL || overflow == OverflowMode::AUTO; }
float Frame::MaxScroll() const { return std::max(0.0f, contentHeight - bounds.height); }
bool Frame::WantsHandCursor() const { return cursor == "pointer"; }

std::vector<Element*> Frame::PaintOrder() const {
    std::vector<Element*> order;
    order.reserve(children.size());
    for (const std::unique_ptr<Element>& child : children) {
        order.push_back(child.get());
    }
    std::stable_sort(order.begin(), order.end(), [](const Element* left, const Element* right) { return left->ZIndex() < right->ZIndex(); });
    return order;
}

Size Frame::Measure(TextMeasurer& measurer, float availableWidth, float availableHeight, bool heightDefinite) {
    if (display == FrameDisplay::NONE) {
        measured = {};
        return measured;
    }
    const Length widthLength = basis.unit != LengthUnit::FIT && Width().unit == LengthUnit::FIT ? basis : ModeLength(Width(), widthMode);
    const float widthSpace = Width().unit == LengthUnit::SCREEN && viewportWidth > 0.0f ? viewportWidth : availableWidth;
    const float heightSpace = Height().unit == LengthUnit::SCREEN && viewportHeight > 0.0f ? viewportHeight : availableHeight;
    const float outerWidth = widthLength.unit == LengthUnit::FIT ? availableWidth : widthLength.Resolve(widthSpace, availableWidth);
    const float padX = padding.SpanX(outerWidth);
    const float padY = padding.SpanY(outerWidth);
    const float innerWidth = std::max(0.0f, outerWidth - padX);
    const Length heightLength = ModeLength(Height(), heightMode);
    float innerLimit = std::max(0.0f, availableHeight - padY);
    bool childHeightKnown = heightDefinite && heightMode != SizeMode::FIT && Height().unit != LengthUnit::FIT;
    if (heightLength.unit == LengthUnit::PIXELS || heightMode == SizeMode::FIXED) {
        innerLimit = std::max(0.0f, heightLength.Resolve(heightSpace, 0.0f) - padY);
        childHeightKnown = true;
    }
    const float columnSpace = GapOf(columnGap, gap, outerWidth);
    const float rowSpace = GapOf(rowGap, gap, outerWidth);
    const bool childHeightDefinite = childHeightKnown;

    float flowWidth = 0.0f;
    float flowHeight = 0.0f;
    float lineWidth = 0.0f;
    float lineHeight = 0.0f;
    int columnIndex = 0;
    int flowCount = 0;
    const int columnCount = std::max(1, columns);
    const float cellWidth = layout == FrameLayout::GRID
                                ? std::max(0.0f, (innerWidth - columnSpace * static_cast<float>(columnCount - 1)) / static_cast<float>(columnCount))
                                : innerWidth;

    std::vector<Element*> flowChildren;
    for (const std::unique_ptr<Element>& child : children) {
        if (!child->Visible() || LeavesFlow(*child) || (AsFrame(*child) != nullptr && AsFrame(*child)->Display() == FrameDisplay::NONE)) {
            child->Measure(measurer, innerWidth, innerLimit, childHeightKnown);
            continue;
        }
        flowChildren.push_back(child.get());
    }
    const bool shareWidth = layout == FrameLayout::HORIZONTAL && !wrap;
    const bool shareHeight = layout == FrameLayout::VERTICAL && childHeightKnown;
    if (shareWidth || shareHeight) {
        int fillCount = 0;
        float fixedMain = 0.0f;
        for (Element* child : flowChildren) {
            const bool fills = shareWidth ? child->Width().unit == LengthUnit::FILL : child->Height().unit == LengthUnit::FILL;
            if (fills) {
                ++fillCount;
                continue;
            }
            const Size size = child->Measure(measurer, innerWidth, innerLimit, childHeightDefinite);
            fixedMain += shareWidth ? size.width : size.height;
        }
        const float gapSpace = shareWidth ? columnSpace : rowSpace;
        const float gaps = flowChildren.size() > 1 ? gapSpace * static_cast<float>(flowChildren.size() - 1) : 0.0f;
        const float limit = shareWidth ? innerWidth : innerLimit;
        const float free = std::max(0.0f, limit - fixedMain - gaps);
        const float share = fillCount > 0 ? free / static_cast<float>(fillCount) : limit;
        for (Element* child : flowChildren) {
            const bool fills = shareWidth ? child->Width().unit == LengthUnit::FILL : child->Height().unit == LengthUnit::FILL;
            if (!fills) {
                continue;
            }
            if (shareWidth) {
                child->Measure(measurer, share, innerLimit, childHeightDefinite);
            } else {
                child->Measure(measurer, innerWidth, share, true);
            }
        }
    } else {
        for (Element* child : flowChildren) {
            const float slot = layout == FrameLayout::GRID ? cellWidth * static_cast<float>(std::max(1, child->ColumnSpan())) : innerWidth;
            child->Measure(measurer, slot, innerLimit, childHeightDefinite);
        }
    }
    for (Element* child : flowChildren) {
        const Size childSize = child->Measured();
        if (layout == FrameLayout::HORIZONTAL || (layout == FrameLayout::WRAP && wrap) || layout == FrameLayout::WRAP) {
            const float next = lineWidth == 0.0f ? childSize.width : lineWidth + columnSpace + childSize.width;
            if ((layout == FrameLayout::WRAP || wrap) && lineWidth > 0.0f && next > innerWidth && innerWidth > 0.0f) {
                flowWidth = std::max(flowWidth, lineWidth);
                flowHeight += lineHeight + rowSpace;
                lineWidth = childSize.width;
                lineHeight = childSize.height;
            } else {
                lineWidth = next;
                lineHeight = std::max(lineHeight, childSize.height);
            }
        } else if (layout == FrameLayout::VERTICAL) {
            if (flowCount > 0) {
                flowHeight += rowSpace;
            }
            flowHeight += childSize.height;
            flowWidth = std::max(flowWidth, childSize.width);
        } else if (layout == FrameLayout::GRID) {
            int span = std::min(columnCount, std::max(1, child->ColumnSpan()));
            if (columnIndex > 0 && columnIndex + span > columnCount) {
                flowHeight += lineHeight + rowSpace;
                lineHeight = 0.0f;
                columnIndex = 0;
            }
            lineHeight = std::max(lineHeight, childSize.height);
            columnIndex += span;
            flowWidth = innerWidth;
            if (columnIndex >= columnCount) {
                flowHeight += lineHeight;
                if (columnIndex > 0) {
                    flowHeight += 0.0f;
                }
                lineHeight = 0.0f;
                columnIndex = 0;
                flowHeight += rowSpace;
            }
        } else {
            flowWidth = std::max(flowWidth, childSize.width);
            flowHeight = std::max(flowHeight, childSize.height);
        }
        ++flowCount;
    }
    if (layout == FrameLayout::HORIZONTAL || layout == FrameLayout::WRAP) {
        flowWidth = std::max(flowWidth, lineWidth);
        flowHeight += lineHeight;
    } else if (layout == FrameLayout::GRID && columnIndex != 0) {
        flowHeight += lineHeight;
    }
    if (layout == FrameLayout::GRID && flowHeight > 0.0f && rowSpace > 0.0f && columnIndex == 0 && flowCount > columnCount) {
        flowHeight -= rowSpace;
    }

    float fitWidth = flowWidth + padX;
    float fitHeight = flowHeight + padY;
    if (autoSize && flowCount == 0) {
        if (widthLength.unit == LengthUnit::FIT && hugWidth.unit == LengthUnit::PIXELS) {
            fitWidth = hugWidth.value;
        }
        if (Height().unit == LengthUnit::FIT && hugHeight.unit == LengthUnit::PIXELS) {
            fitHeight = hugHeight.value;
        }
    }
    const float resolvedWidth = widthLength.unit == LengthUnit::FIT ? fitWidth : widthLength.Resolve(widthSpace, fitWidth);
    if (aspectRatio > 0.0f && (Height().unit == LengthUnit::FIT || heightMode == SizeMode::FIT)) {
        fitHeight = resolvedWidth / aspectRatio;
    }
    measured.width = widthLength.unit == LengthUnit::FIT ? fitWidth : widthLength.Resolve(widthSpace, fitWidth);
    measured.height = ResolveDefinite(heightLength, heightSpace, fitHeight, heightDefinite || heightMode == SizeMode::FILL || heightMode == SizeMode::PERCENT || heightMode == SizeMode::FIXED);
    contentHeight = fitHeight;
    if (scrollY > std::max(0.0f, contentHeight - measured.height)) {
        scrollY = std::max(0.0f, contentHeight - measured.height);
    }
    return FinishMeasure(availableWidth, availableHeight);
}

void Frame::Arrange(const Rect& rect) {
    Rect placed = BorderBox(rect);
    if (positionMode == FramePosition::FIXED) {
        placed.x = OffsetX().Resolve(viewportWidth > 0.0f ? viewportWidth : placed.width, 0.0f, true);
        placed.y = OffsetY().Resolve(viewportHeight > 0.0f ? viewportHeight : placed.height, 0.0f, true);
    }
    placed.x += translateX;
    placed.y += translateY;
    bounds = placed;
    const float padLeft = padding.left.Resolve(bounds.width, 0.0f);
    const float padTop = padding.top.Resolve(bounds.width, 0.0f);
    const float padX = padding.SpanX(bounds.width);
    const float innerWidth = std::max(0.0f, bounds.width - padX);
    const float innerHeight = std::max(0.0f, bounds.height - padding.SpanY(bounds.width));
    const float columnSpace = GapOf(columnGap, gap, bounds.width);
    const float rowSpace = GapOf(rowGap, gap, bounds.width);
    const float originX = bounds.x + padLeft;
    const float originY = bounds.y + padTop - (Scrolls() ? scrollY : 0.0f);

    std::vector<Element*> flow;
    for (const std::unique_ptr<Element>& child : children) {
        if (!child->Visible() || LeavesFlow(*child)) {
            continue;
        }
        flow.push_back(child.get());
    }
    float contentMain = 0.0f;
    for (std::size_t index = 0; index < flow.size(); ++index) {
        if (index > 0 && (layout == FrameLayout::HORIZONTAL || layout == FrameLayout::VERTICAL)) {
            contentMain += layout == FrameLayout::HORIZONTAL ? columnSpace : rowSpace;
        }
        contentMain += layout == FrameLayout::VERTICAL ? flow[index]->Measured().height : flow[index]->Measured().width;
    }
    const float innerMain = layout == FrameLayout::VERTICAL ? std::max(0.0f, bounds.height - padding.SpanY(bounds.width)) : innerWidth;
    float cursor = layout == FrameLayout::VERTICAL ? originY : originX;
    float extra = 0.0f;
    float growSum = 0.0f;
    for (Element* child : flow) {
        growSum += child->Grow();
    }
    if (growSum <= 0.0f && justifyBetween && flow.size() > 1) {
        extra = std::max(0.0f, innerMain - contentMain) / static_cast<float>(flow.size() - 1);
    } else if (growSum <= 0.0f && justify == Alignment::CENTER) {
        cursor += std::max(0.0f, innerMain - contentMain) * 0.5f;
    } else if (growSum <= 0.0f && justify == Alignment::END) {
        cursor += std::max(0.0f, innerMain - contentMain);
    }
    const float free = std::max(0.0f, innerMain - contentMain);

    if ((layout == FrameLayout::HORIZONTAL && !wrap) || layout == FrameLayout::VERTICAL) {
        for (Element* child : flow) {
            float childWidth = child->Measured().width;
            float childHeight = child->Measured().height;
            if (child->Grow() > 0.0f && growSum > 0.0f) {
                if (layout == FrameLayout::HORIZONTAL) {
                    childWidth += free * (child->Grow() / growSum);
                } else {
                    childHeight += free * (child->Grow() / growSum);
                }
            }
            float cross = layout == FrameLayout::HORIZONTAL ? originY : originX;
            const float crossSize = layout == FrameLayout::HORIZONTAL ? childHeight : childWidth;
            const float crossLimit = layout == FrameLayout::HORIZONTAL ? std::max(0.0f, bounds.height - padding.SpanY(bounds.width)) : innerWidth;
            if (align == Alignment::STRETCH && !child->AutoSize()) {
                if (layout == FrameLayout::HORIZONTAL && child->Height().unit == LengthUnit::FIT) {
                    childHeight = crossLimit;
                } else if (layout == FrameLayout::VERTICAL && child->Width().unit == LengthUnit::FIT) {
                    childWidth = innerWidth;
                }
            } else if (align == Alignment::CENTER) {
                cross += (crossLimit - crossSize) * 0.5f;
            } else if (align == Alignment::END) {
                cross += crossLimit - crossSize;
            }
            if (layout == FrameLayout::HORIZONTAL) {
                child->Arrange(Rect{cursor, cross, childWidth, childHeight});
                cursor += childWidth + columnSpace + extra;
            } else {
                child->Arrange(Rect{cross, cursor, childWidth, childHeight});
                cursor += childHeight + rowSpace + extra;
            }
        }
    } else if (layout == FrameLayout::GRID) {
        const int columnCount = std::max(1, columns);
        const float cellWidth = columnCount <= 1 ? innerWidth : std::max(0.0f, (innerWidth - columnSpace * static_cast<float>(columnCount - 1)) / static_cast<float>(columnCount));
        int columnCursor = 0;
        float y = originY;
        float rowHeight = 0.0f;
        std::vector<Element*> rowItems;
        auto placeRow = [&]() {
            float x = originX;
            for (Element* child : rowItems) {
                const int span = std::min(columnCount, std::max(1, child->ColumnSpan()));
                child->Arrange(Rect{x, y, child->Measured().width, child->Measured().height});
                x += cellWidth * static_cast<float>(span) + columnSpace * static_cast<float>(span);
            }
            y += rowHeight + rowSpace;
            rowItems.clear();
            rowHeight = 0.0f;
            columnCursor = 0;
        };
        for (Element* child : flow) {
            const int span = std::min(columnCount, std::max(1, child->ColumnSpan()));
            if (columnCursor > 0 && columnCursor + span > columnCount) {
                placeRow();
            }
            rowItems.push_back(child);
            rowHeight = std::max(rowHeight, child->Measured().height);
            columnCursor += span;
            if (columnCursor >= columnCount) {
                placeRow();
            }
        }
        if (!rowItems.empty()) {
            placeRow();
        }
    } else if (layout == FrameLayout::WRAP || wrap) {
        float x = originX;
        float y = originY;
        float lineHeight = 0.0f;
        for (Element* child : flow) {
            const float childWidth = child->Measured().width;
            if (x > originX && x + childWidth > originX + innerWidth) {
                x = originX;
                y += lineHeight + rowSpace;
                lineHeight = 0.0f;
            }
            child->Arrange(Rect{x, y, childWidth, child->Measured().height});
            x += childWidth + columnSpace;
            lineHeight = std::max(lineHeight, child->Measured().height);
        }
    } else if (layout == FrameLayout::STACK) {
        for (Element* child : flow) {
            child->Arrange(Rect{originX, originY, child->Measured().width, child->Measured().height});
        }
    } else {
        for (Element* child : flow) {
            child->Arrange(Rect{originX + child->OffsetX().Resolve(innerWidth, 0.0f, true), originY + child->OffsetY().Resolve(innerHeight, 0.0f, true),
                                child->Measured().width, child->Measured().height});
        }
    }

    for (const std::unique_ptr<Element>& child : children) {
        if (!LeavesFlow(*child)) {
            continue;
        }
        Frame* frame = AsFrame(*child);
        float x = originX + child->OffsetX().Resolve(bounds.width, 0.0f, true);
        float y = originY + child->OffsetY().Resolve(innerHeight, 0.0f, true);
        const float childWidth = child->Measured().width;
        const float childHeight = child->Measured().height;
        if (frame != nullptr && frame->Anchor() == FrameAnchor::CENTER) {
            x = bounds.x + (bounds.width - childWidth) * 0.5f;
            y = bounds.y + (bounds.height - childHeight) * 0.5f;
        } else if (frame != nullptr && frame->Anchor() == FrameAnchor::RIGHT) {
            x = bounds.x + bounds.width - childWidth - padding.right.Resolve(bounds.width, 0.0f);
        } else if (frame != nullptr && frame->Anchor() == FrameAnchor::BOTTOM) {
            y = bounds.y + bounds.height - childHeight - padding.bottom.Resolve(bounds.width, 0.0f);
        } else if (frame != nullptr && frame->Anchor() == FrameAnchor::LEFT) {
            x = originX;
        } else if (frame != nullptr && frame->Anchor() == FrameAnchor::TOP) {
            y = bounds.y + padTop;
        }
        if (child->PositionMode() == FramePosition::FIXED) {
            x = child->OffsetX().Resolve(viewportWidth, 0.0f, true);
            y = child->OffsetY().Resolve(viewportHeight, 0.0f, true);
        }
        child->Arrange(Rect{x, y, childWidth, childHeight});
    }
    for (const std::unique_ptr<Element>& child : children) {
        const Rect natural = child->Bounds();
        const Rect pinned = child->PinnedRect(bounds, natural);
        if (pinned.x != natural.x || pinned.y != natural.y || pinned.width != natural.width || pinned.height != natural.height) {
            child->Arrange(pinned);
        }
    }
}

void Frame::ApplyBound(const std::string& property, const std::string& value) {
    Element::ApplyBound(property, value);
    if (property == "background" || property == "backgroundColor") {
        if (const std::optional<Color> color = ParseColor(value)) {
            SetBackgroundColor(*color);
        }
    } else if (property == "cursor") {
        SetCursor(value);
    }
}

void Frame::OnPaint(Renderer& renderer) {
    if (display == FrameDisplay::NONE) {
        return;
    }
    const bool turned = std::fabs(rotationZ) > 0.01f;
    if (turned) {
        renderer.PushRotation(rotationZ, bounds.x + bounds.width * 0.5f, bounds.y + bounds.height * 0.5f);
    }
    const float radius = std::max(std::max(cornerRadius[0], cornerRadius[1]), std::max(cornerRadius[2], cornerRadius[3]));
    if (hasBackground && bounds.width > 0.0f && bounds.height > 0.0f) {
        renderer.FillRectangle(bounds, backgroundColor, radius);
    }
    if (borderStyle != "none") {
        const bool uniform = sideBorder[0] == sideBorder[1] && sideBorder[1] == sideBorder[2] && sideBorder[2] == sideBorder[3];
        if (uniform && sideBorder[0] <= 0.0f && borderWidth > 0.0f) {
            renderer.StrokeRectangle(bounds, borderColor, borderWidth, radius);
        } else if (uniform && sideBorder[0] > 0.0f) {
            renderer.StrokeRectangle(bounds, borderColor, sideBorder[0], radius);
        } else {
            if (sideBorder[0] > 0.0f) {
                renderer.FillRectangle(Rect{bounds.x, bounds.y, bounds.width, sideBorder[0]}, borderColor, 0.0f);
            }
            if (sideBorder[1] > 0.0f) {
                renderer.FillRectangle(Rect{bounds.x + bounds.width - sideBorder[1], bounds.y, sideBorder[1], bounds.height}, borderColor, 0.0f);
            }
            if (sideBorder[2] > 0.0f) {
                renderer.FillRectangle(Rect{bounds.x, bounds.y + bounds.height - sideBorder[2], bounds.width, sideBorder[2]}, borderColor, 0.0f);
            }
            if (sideBorder[3] > 0.0f) {
                renderer.FillRectangle(Rect{bounds.x, bounds.y, sideBorder[3], bounds.height}, borderColor, 0.0f);
            }
        }
    }
    const bool clipChildren = Clips() || !clipPath.empty();
    if (clipChildren) {
        renderer.PushClip(bounds);
    }
    for (Element* child : PaintOrder()) {
        child->Paint(renderer);
    }
    if (clipChildren) {
        renderer.PopClip();
    }
    if (Scrolls() && MaxScroll() > 0.0f) {
        const Rect track{bounds.x + bounds.width - 14.0f, bounds.y + 6.0f, 8.0f, std::max(0.0f, bounds.height - 12.0f)};
        const float thumbHeight = std::min(track.height, std::max(28.0f, track.height * (bounds.height / std::max(bounds.height, contentHeight))));
        const float travel = std::max(0.0f, track.height - thumbHeight);
        const float y = track.y + (MaxScroll() <= 0.0f ? 0.0f : travel * (scrollY / MaxScroll()));
        renderer.FillRectangle(track, COLOR_BORDER_DEFAULT, 4.0f);
        renderer.FillRectangle(Rect{track.x, y, track.width, thumbHeight}, COLOR_TEXT_SECONDARY, 4.0f);
    }
    if (turned) {
        renderer.PopTransform();
    }
}

Element* Frame::HitTest(Point point) {
    if (!visible || display == FrameDisplay::NONE || !ContainsPoint(bounds, point)) {
        return nullptr;
    }
    const std::vector<Element*> order = PaintOrder();
    for (auto it = order.rbegin(); it != order.rend(); ++it) {
        if (Element* hit = (*it)->HitTest(point)) {
            return hit;
        }
    }
    return this;
}

bool Frame::OnWheel(int delta) {
    if (!Scrolls()) {
        return false;
    }
    const float next = std::clamp(scrollY + (delta > 0 ? -48.0f : 48.0f), 0.0f, MaxScroll());
    if (next == scrollY) {
        return false;
    }
    scrollY = next;
    InvalidateLayout();
    return true;
}

}  // namespace Runtime
