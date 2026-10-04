#include "components/layout/Surfaces.h"

#include "rendering/renderer/Renderer.h"

#include <algorithm>

namespace Runtime {

Panel::Panel() {
    SetWidth(Length::Fill());
    SetHeight(Length::Fit());
    SetPadding(Edges::All(Length::Pixels(16.0f)));
    SetSpacing(Length::Pixels(12.0f));
    SetChrome(COLOR_BG_SECONDARY, true);
    SetChromeRadius(Length::Pixels(RADIUS_LG));
}

NodeKind Panel::Kind() const {
    return NodeKind::PANEL;
}

Card::Card() {
    SetWidth(Length::Fill());
    SetHeight(Length::Fit());
    SetPadding(Edges::All(Length::Pixels(16.0f)));
    SetSpacing(Length::Pixels(12.0f));
    SetChrome(COLOR_BG_PRIMARY, true);
    SetBorder(1.0f, COLOR_BORDER_DEFAULT);
    SetChromeRadius(Length::Pixels(RADIUS_LG));
}

NodeKind Card::Kind() const {
    return NodeKind::CARD;
}

NodeKind Spacer::Kind() const {
    return NodeKind::SPACER;
}

Size Spacer::Measure(TextMeasurer&, float availableWidth, float availableHeight, bool heightDefinite) {
    const float fit = 16.0f;
    measured.width = width.unit == LengthUnit::FIT ? fit : width.Resolve(availableWidth, fit);
    measured.height = ResolveDefinite(height, availableHeight, fit, heightDefinite);
    return FinishMeasure(availableWidth, availableHeight);
}

NodeKind Divider::Kind() const {
    return NodeKind::DIVIDER;
}

void Divider::SetOrientation(bool value) {
    vertical = value;
}

void Divider::SetColor(Color value) {
    color = value;
}

Size Divider::Measure(TextMeasurer&, float availableWidth, float availableHeight, bool heightDefinite) {
    if (vertical) {
        measured.width = width.unit == LengthUnit::FIT ? 1.0f : width.Resolve(availableWidth, 1.0f);
        measured.height = ResolveDefinite(height, availableHeight, availableHeight, heightDefinite);
    } else {
        measured.width = width.unit == LengthUnit::FIT ? availableWidth : width.Resolve(availableWidth, availableWidth);
        measured.height = ResolveDefinite(height, availableHeight, 1.0f, heightDefinite);
    }
    return FinishMeasure(availableWidth, availableHeight);
}

void Divider::OnPaint(Renderer& renderer) {
    renderer.FillRectangle(bounds, color, 0.0f);
}

NodeKind AbsoluteBox::Kind() const {
    return NodeKind::ABSOLUTE;
}

Size AbsoluteBox::Measure(TextMeasurer& measurer, float availableWidth, float availableHeight, bool heightDefinite) {
    const float outerWidth = width.unit == LengthUnit::FIT ? availableWidth : width.Resolve(availableWidth, availableWidth);
    float contentWidth = 0.0f;
    float contentHeight = 0.0f;
    for (const std::unique_ptr<Element>& child : children) {
        const Size childSize = child->Measure(measurer, outerWidth, availableHeight, heightDefinite);
        const float x = child->OffsetX().Resolve(outerWidth, 0.0f);
        const float y = child->OffsetY().Resolve(availableHeight, 0.0f);
        contentWidth = std::max(contentWidth, x + childSize.width);
        contentHeight = std::max(contentHeight, y + childSize.height);
    }
    measured.width = width.unit == LengthUnit::FIT ? contentWidth : width.Resolve(availableWidth, contentWidth);
    measured.height = ResolveDefinite(height, availableHeight, contentHeight, heightDefinite);
    return FinishMeasure(availableWidth, availableHeight);
}

void AbsoluteBox::Arrange(const Rect& rect) {
    bounds = BorderBox(rect);
    for (const std::unique_ptr<Element>& child : children) {
        const Size size = child->Measured();
        const float x = bounds.x + child->OffsetX().Resolve(bounds.width, 0.0f);
        const float y = bounds.y + child->OffsetY().Resolve(bounds.height, 0.0f);
        child->Arrange(Rect{x, y, size.width, size.height});
    }
}

NodeKind SplitView::Kind() const {
    return NodeKind::SPLIT;
}

void SplitView::SetDirection(FlexDirection value) {
    direction = value;
}

void SplitView::SetRatio(float value) {
    ratio = value < 0.05f ? 0.05f : (value > 0.95f ? 0.95f : value);
}

Rect SplitView::DividerRect() const {
    constexpr float thickness = 8.0f;
    if (direction == FlexDirection::COLUMN) {
        const float y = bounds.y + (bounds.height - thickness) * ratio;
        return Rect{bounds.x, y, bounds.width, thickness};
    }
    const float x = bounds.x + (bounds.width - thickness) * ratio;
    return Rect{x, bounds.y, thickness, bounds.height};
}

Size SplitView::Measure(TextMeasurer& measurer, float availableWidth, float availableHeight, bool heightDefinite) {
    const float outerWidth = width.unit == LengthUnit::FIT ? availableWidth : width.Resolve(availableWidth, availableWidth);
    const float outerHeight = ResolveDefinite(height, availableHeight, availableHeight, heightDefinite);
    const bool horizontal = direction == FlexDirection::ROW;
    const float firstMain = (horizontal ? outerWidth : outerHeight) * ratio;
    const float secondMain = std::max(0.0f, (horizontal ? outerWidth : outerHeight) - firstMain);
    for (std::size_t index = 0; index < children.size(); ++index) {
        const float childWidth = horizontal ? (index == 0 ? firstMain : secondMain) : outerWidth;
        const float childHeight = horizontal ? outerHeight : (index == 0 ? firstMain : secondMain);
        children[index]->Measure(measurer, childWidth, childHeight, true);
    }
    measured.width = outerWidth;
    measured.height = outerHeight;
    return FinishMeasure(availableWidth, availableHeight);
}

void SplitView::Arrange(const Rect& rect) {
    bounds = BorderBox(rect);
    const bool horizontal = direction == FlexDirection::ROW;
    const float main = horizontal ? bounds.width : bounds.height;
    const float firstMain = std::max(0.0f, main * ratio - 4.0f);
    const float secondOrigin = main * ratio + 4.0f;
    const float secondMain = std::max(0.0f, main - secondOrigin);
    for (std::size_t index = 0; index < children.size() && index < 2; ++index) {
        Rect childRect = bounds;
        if (horizontal) {
            childRect.x = bounds.x + (index == 0 ? 0.0f : secondOrigin);
            childRect.width = index == 0 ? firstMain : secondMain;
        } else {
            childRect.y = bounds.y + (index == 0 ? 0.0f : secondOrigin);
            childRect.height = index == 0 ? firstMain : secondMain;
        }
        children[index]->Arrange(childRect);
    }
}

Element* SplitView::HitTest(Point point) {
    if (!visible || !ContainsPoint(bounds, point)) {
        return nullptr;
    }
    if (ContainsPoint(DividerRect(), point)) {
        return this;
    }
    if (Element* child = HitTestChildren(point)) {
        return child;
    }
    return this;
}

bool SplitView::OnPointerDown(Point point) {
    dragging = ContainsPoint(DividerRect(), point);
    return true;
}

bool SplitView::OnDrag(Point point) {
    if (!dragging) {
        return false;
    }
    const float main = direction == FlexDirection::ROW ? bounds.width : bounds.height;
    const float cursor = direction == FlexDirection::ROW ? point.x - bounds.x : point.y - bounds.y;
    if (main > 1.0f) {
        SetRatio(cursor / main);
        InvalidateLayout();
    }
    return true;
}

void SplitView::OnPaint(Renderer& renderer) {
    PaintChildren(renderer);
    renderer.FillRectangle(DividerRect(), COLOR_BORDER_DEFAULT, 4.0f);
}

NodeKind ImageView::Kind() const {
    return NodeKind::IMAGE;
}

void ImageView::SetSource(std::string value) {
    source = std::move(value);
}

Size ImageView::Measure(TextMeasurer&, float availableWidth, float availableHeight, bool heightDefinite) {
    measured.width = width.unit == LengthUnit::FIT ? 160.0f : width.Resolve(availableWidth, 160.0f);
    measured.height = ResolveDefinite(height, availableHeight, 120.0f, heightDefinite);
    return FinishMeasure(availableWidth, availableHeight);
}

void ImageView::OnPaint(Renderer& renderer) {
    renderer.FillRectangle(bounds, COLOR_BG_TERTIARY, RADIUS_MD);
    renderer.StrokeRectangle(bounds, COLOR_BORDER_DEFAULT, 1.0f, RADIUS_MD);
    TextStyle style;
    style.fontSize = TYPE_CAPTION;
    style.fontWeight = WEIGHT_MEDIUM;
    style.color = COLOR_TEXT_SECONDARY;
    style.horizontal = TextAlign::CENTER;
    style.vertical = TextAlign::CENTER;
    renderer.DrawString(source.empty() ? "Image" : source, bounds, style);
}

NodeKind ScrollView::Kind() const {
    return NodeKind::SCROLL;
}

void ScrollView::SetPadding(Edges value) {
    padding = value;
}

void ScrollView::SetSpacing(Length value) {
    spacing = value;
}

float ScrollView::MaxScroll() const {
    return std::max(0.0f, contentHeight - bounds.height);
}

Rect ScrollView::TrackRect() const {
    constexpr float trackWidth = 8.0f;
    constexpr float inset = 6.0f;
    return Rect{bounds.x + bounds.width - inset - trackWidth, bounds.y + inset, trackWidth, std::max(0.0f, bounds.height - inset * 2.0f)};
}

Rect ScrollView::ThumbRect() const {
    const Rect track = TrackRect();
    const float maxScroll = MaxScroll();
    if (maxScroll <= 0.0f || contentHeight <= 0.0f || track.height <= 0.0f) {
        return Rect{track.x, track.y, track.width, 0.0f};
    }
    const float viewportRatio = bounds.height / contentHeight;
    const float thumbHeight = std::min(track.height, std::max(28.0f, track.height * viewportRatio));
    const float travel = std::max(0.0f, track.height - thumbHeight);
    const float y = track.y + travel * (scrollY / maxScroll);
    return Rect{track.x, y, track.width, thumbHeight};
}

Size ScrollView::Measure(TextMeasurer& measurer, float availableWidth, float availableHeight, bool heightDefinite) {
    const float outerWidth = width.unit == LengthUnit::FIT ? availableWidth : width.Resolve(availableWidth, availableWidth);
    const float padX = padding.SpanX(outerWidth);
    const float padY = padding.SpanY(outerWidth);
    const float space = spacing.Resolve(outerWidth, 0.0f);
    const float innerWidth = std::max(0.0f, outerWidth - padX - 18.0f);
    contentHeight = padY;
    bool first = true;
    for (const std::unique_ptr<Element>& child : children) {
        const Size childSize = child->Measure(measurer, innerWidth, UNBOUNDED_MEASURE, false);
        if (!first) {
            contentHeight += space;
        }
        first = false;
        contentHeight += childSize.height;
    }
    const float fitHeight = contentHeight;
    measured.width = outerWidth;
    measured.height = ResolveDefinite(height, availableHeight, fitHeight, heightDefinite);
    if (scrollY > std::max(0.0f, contentHeight - measured.height)) {
        scrollY = std::max(0.0f, contentHeight - measured.height);
    }
    return FinishMeasure(availableWidth, availableHeight);
}

void ScrollView::Arrange(const Rect& rect) {
    bounds = BorderBox(rect);
    const float padLeft = padding.left.Resolve(bounds.width, 0.0f);
    const float padTop = padding.top.Resolve(bounds.width, 0.0f);
    const float space = spacing.Resolve(bounds.width, 0.0f);
    const float innerWidth = std::max(0.0f, bounds.width - padding.SpanX(bounds.width) - 18.0f);
    float y = bounds.y + padTop - scrollY;
    bool first = true;
    for (const std::unique_ptr<Element>& child : children) {
        if (!first) {
            y += space;
        }
        first = false;
        const float childHeight = child->Measured().height;
        child->Arrange(Rect{bounds.x + padLeft, y, innerWidth, childHeight});
        y += childHeight;
    }
}

void ScrollView::OnPaint(Renderer& renderer) {
    renderer.PushClip(bounds);
    PaintChildren(renderer);
    if (MaxScroll() > 0.0f) {
        const Rect track = TrackRect();
        const Rect thumb = ThumbRect();
        renderer.FillRectangle(track, COLOR_BORDER_DEFAULT, 4.0f);
        if (thumb.height > 0.0f) {
            renderer.FillRectangle(thumb, pointerHover || thumbDrag ? COLOR_TEXT_PRIMARY : COLOR_TEXT_SECONDARY, 4.0f);
        }
    }
    renderer.PopClip();
}

Element* ScrollView::HitTest(Point point) {
    if (!visible || !ContainsPoint(bounds, point)) {
        return nullptr;
    }
    if (MaxScroll() > 0.0f && (ContainsPoint(ThumbRect(), point) || ContainsPoint(TrackRect(), point))) {
        return this;
    }
    if (Element* child = HitTestChildren(point)) {
        return child;
    }
    return this;
}

bool ScrollView::OnPointerDown(Point point) {
    const Rect thumb = ThumbRect();
    thumbDrag = ContainsPoint(thumb, point);
    dragOffset = point.y - thumb.y;
    if (!thumbDrag && ContainsPoint(TrackRect(), point)) {
        const Rect track = TrackRect();
        const float travel = std::max(1.0f, track.height - thumb.height);
        scrollY = std::clamp((point.y - track.y) / travel, 0.0f, 1.0f) * MaxScroll();
        InvalidateLayout();
    }
    return true;
}

bool ScrollView::OnDrag(Point point) {
    if (!thumbDrag) {
        return false;
    }
    const Rect track = TrackRect();
    const Rect thumb = ThumbRect();
    const float travel = std::max(1.0f, track.height - thumb.height);
    const float ratio = (point.y - dragOffset - track.y) / travel;
    scrollY = std::clamp(ratio, 0.0f, 1.0f) * MaxScroll();
    InvalidateLayout();
    return true;
}

bool ScrollView::OnWheel(int delta) {
    const float next = std::clamp(scrollY + (delta > 0 ? -48.0f : 48.0f), 0.0f, MaxScroll());
    if (next == scrollY) {
        return false;
    }
    scrollY = next;
    InvalidateLayout();
    return true;
}

}  // namespace Runtime
