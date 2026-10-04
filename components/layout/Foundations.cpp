#include "components/layout/Foundations.h"

#include "rendering/renderer/Renderer.h"

#include <algorithm>

namespace Runtime {

Container::Container() {
    SetWidth(Length::Fill());
    SetHeight(Length::Fit());
    SetPadding(Edges::All(Length::Pixels(16.0f)));
    SetSpacing(Length::Pixels(12.0f));
}

NodeKind Container::Kind() const {
    return NodeKind::CONTAINER;
}

WrapBox::WrapBox() {
    SetWidth(Length::Fill());
    SetHeight(Length::Fit());
    SetDirection(FlexDirection::ROW);
    SetWrap(FlexWrap::WRAP);
    SetGap(Length::Pixels(12.0f));
    SetAlignment(Alignment::CENTER);
}

NodeKind WrapBox::Kind() const {
    return NodeKind::WRAP;
}

Navbar::Navbar() {
    SetWidth(Length::Fill());
    SetHeight(Length::Pixels(52.0f));
    SetPadding(Edges{Length::Pixels(0.0f), Length::Pixels(16.0f), Length::Pixels(0.0f), Length::Pixels(16.0f)});
    SetSpacing(Length::Pixels(12.0f));
    SetAlignment(Alignment::CENTER);
    SetChrome(COLOR_BG_PRIMARY, true);
}

NodeKind Navbar::Kind() const {
    return NodeKind::NAVBAR;
}

void Navbar::OnPaint(Renderer& renderer) {
    PaintChildren(renderer);
    renderer.FillRectangle(Rect{bounds.x, bounds.y + bounds.height - 1.0f, bounds.width, 1.0f}, COLOR_BORDER_DEFAULT, 0.0f);
}

Header::Header() {
    SetWidth(Length::Fill());
    SetHeight(Length::Fit());
    SetSpacing(Length::Pixels(8.0f));
    SetPadding(Edges::All(Length::Pixels(24.0f)));
}

NodeKind Header::Kind() const {
    return NodeKind::HEADER;
}

Section::Section() {
    SetWidth(Length::Fill());
    SetHeight(Length::Fit());
    SetSpacing(Length::Pixels(12.0f));
}

NodeKind Section::Kind() const {
    return NodeKind::SECTION;
}

Footer::Footer() {
    SetWidth(Length::Fill());
    SetHeight(Length::Fit());
    SetPadding(Edges::All(Length::Pixels(24.0f)));
    SetSpacing(Length::Pixels(8.0f));
}

NodeKind Footer::Kind() const {
    return NodeKind::FOOTER;
}

UiList::UiList() {
    SetWidth(Length::Fill());
    SetHeight(Length::Fit());
    SetSpacing(Length::Pixels(0.0f));
    SetChrome(COLOR_BG_PRIMARY, true);
    SetBorder(1.0f, COLOR_BORDER_DEFAULT);
    SetChromeRadius(Length::Pixels(RADIUS_MD));
}

NodeKind UiList::Kind() const {
    return NodeKind::LIST;
}

ListItem::ListItem() {
    SetWidth(Length::Fill());
    SetHeight(Length::Pixels(44.0f));
    SetPadding(Edges{Length::Pixels(0.0f), Length::Pixels(12.0f), Length::Pixels(0.0f), Length::Pixels(12.0f)});
    SetAlignment(Alignment::CENTER);
    SetSpacing(Length::Pixels(8.0f));
}

NodeKind ListItem::Kind() const {
    return NodeKind::LIST_ITEM;
}

void ListItem::SetOnClick(std::function<void()> callback) {
    onClick = std::move(callback);
}

void ListItem::OnPaint(Renderer& renderer) {
    if (pointerHover) {
        renderer.FillRectangle(bounds, COLOR_BG_SECONDARY, 0.0f);
    }
    PaintChildren(renderer);
}

Element* ListItem::HitTest(Point point) {
    return visible && ContainsPoint(bounds, point) ? this : nullptr;
}

void ListItem::Activate() {
    if (!action.empty()) {
        RunAction(action);
    }
    if (onClick) {
        onClick();
    }
}

bool ListItem::WantsHandCursor() const {
    return true;
}

Menu::Menu() {
    SetWidth(Length::Pixels(220.0f));
    SetHeight(Length::Fit());
    SetPadding(Edges::All(Length::Pixels(6.0f)));
    SetSpacing(Length::Pixels(2.0f));
    SetChrome(COLOR_BG_PRIMARY, true);
    SetBorder(1.0f, COLOR_BORDER_DEFAULT);
    SetChromeRadius(Length::Pixels(RADIUS_MD));
    SetZIndex(Z_CONTEXT_MENU);
}

NodeKind Menu::Kind() const {
    return NodeKind::MENU;
}

NodeKind MenuItem::Kind() const {
    return NodeKind::MENU_ITEM;
}

void MenuItem::SetText(std::string value) {
    text = std::move(value);
}

void MenuItem::SetOnClick(std::function<void()> callback) {
    onClick = std::move(callback);
}

Size MenuItem::Measure(TextMeasurer& measurer, float availableWidth, float availableHeight, bool heightDefinite) {
    const Size textSize = measurer.MeasureText(text, TYPE_BODY, UNBOUNDED_MEASURE);
    measured.width = width.unit == LengthUnit::FIT ? std::max(160.0f, textSize.width + 24.0f) : width.Resolve(availableWidth, textSize.width + 24.0f);
    measured.height = ResolveDefinite(height, availableHeight, 36.0f, heightDefinite);
    return FinishMeasure(availableWidth, availableHeight);
}

void MenuItem::OnPaint(Renderer& renderer) {
    if (pointerHover) {
        renderer.FillRectangle(bounds, COLOR_BG_SECONDARY, RADIUS_SM);
    }
    TextStyle style;
    style.fontSize = TYPE_BODY;
    style.color = COLOR_TEXT_PRIMARY;
    style.vertical = TextAlign::CENTER;
    renderer.DrawString(text, Rect{bounds.x + 12.0f, bounds.y, std::max(0.0f, bounds.width - 16.0f), bounds.height}, style);
}

Element* MenuItem::HitTest(Point point) {
    return visible && ContainsPoint(bounds, point) ? this : nullptr;
}

void MenuItem::Activate() {
    if (!action.empty()) {
        RunAction(action);
    }
    if (onClick) {
        onClick();
    }
}

bool MenuItem::WantsHandCursor() const {
    return true;
}

NodeKind ClipBox::Kind() const {
    return NodeKind::CLIP;
}

void ClipBox::OnPaint(Renderer& renderer) {
    renderer.PushClip(bounds);
    PaintChildren(renderer);
    renderer.PopClip();
}

NodeKind AspectBox::Kind() const {
    return NodeKind::ASPECT;
}

void AspectBox::SetRatio(float value) {
    ratio = value < 0.01f ? 0.01f : value;
}

Size AspectBox::Measure(TextMeasurer& measurer, float availableWidth, float availableHeight, bool heightDefinite) {
    const float outerWidth = width.unit == LengthUnit::FIT ? availableWidth : width.Resolve(availableWidth, availableWidth);
    const float aspectHeight = outerWidth / ratio;
    measured.width = outerWidth;
    measured.height = height.unit == LengthUnit::FIT ? aspectHeight : ResolveDefinite(height, availableHeight, aspectHeight, heightDefinite);
    for (const std::unique_ptr<Element>& child : children) {
        child->Measure(measurer, measured.width, measured.height, true);
    }
    return FinishMeasure(availableWidth, availableHeight);
}

void AspectBox::Arrange(const Rect& rect) {
    bounds = BorderBox(rect);
    for (const std::unique_ptr<Element>& child : children) {
        child->Arrange(bounds);
    }
}

OverlayBox::OverlayBox() {
    SetWidth(Length::Fill());
    SetHeight(Length::Fill());
}

NodeKind OverlayBox::Kind() const {
    return NodeKind::OVERLAY;
}

void OverlayBox::Arrange(const Rect& rect) {
    bounds = BorderBox(rect);
    for (const std::unique_ptr<Element>& child : children) {
        const Size size = child->Measured();
        const float childWidth = child->Width().unit == LengthUnit::FILL ? bounds.width : size.width;
        const float childHeight = child->Height().unit == LengthUnit::FILL ? bounds.height : size.height;
        const float x = bounds.x + (bounds.width - childWidth) * 0.5f;
        const float y = bounds.y + (bounds.height - childHeight) * 0.5f;
        child->Arrange(Rect{x, y, childWidth, childHeight});
    }
}

Dialog::Dialog() {
    SetWidth(Length::Fill());
    SetHeight(Length::Fill());
    SetZIndex(Z_MODAL);
}

NodeKind Dialog::Kind() const {
    return NodeKind::DIALOG;
}

Size Dialog::Measure(TextMeasurer& measurer, float availableWidth, float availableHeight, bool) {
    const float boxWidth = viewportWidth > 0.0f ? viewportWidth : availableWidth;
    const float boxHeight = viewportHeight > 0.0f ? viewportHeight : availableHeight;
    for (const std::unique_ptr<Element>& child : children) {
        child->Measure(measurer, boxWidth, boxHeight, true);
    }
    measured = {};
    return measured;
}

void Dialog::Arrange(const Rect& rect) {
    if (!visible) {
        bounds = {};
        return;
    }
    const float boxWidth = viewportWidth > 0.0f ? viewportWidth : rect.width;
    const float boxHeight = viewportHeight > 0.0f ? viewportHeight : rect.height;
    bounds = Rect{0.0f, 0.0f, boxWidth, boxHeight};
    for (const std::unique_ptr<Element>& child : children) {
        const Size size = child->Measured();
        const float childWidth = child->Width().unit == LengthUnit::FILL ? bounds.width * 0.9f : size.width;
        const float childHeight = size.height;
        child->Arrange(Rect{bounds.x + (bounds.width - childWidth) * 0.5f, bounds.y + (bounds.height - childHeight) * 0.5f, childWidth, childHeight});
    }
}

void Dialog::OnPaint(Renderer& renderer) {
    renderer.FillRectangle(bounds, Color{0.0f, 0.0f, 0.0f, 0.5f}, 0.0f);
    PaintChildren(renderer);
}

Element* Dialog::HitTest(Point point) {
    if (!visible || bounds.width <= 0.0f || !ContainsPoint(bounds, point)) {
        return nullptr;
    }
    if (Element* child = HitTestChildren(point)) {
        return child;
    }
    return this;
}

void Dialog::Activate() {
    SetVisible(false);
    InvalidateLayout();
}

Form::Form() {
    SetWidth(Length::Fill());
    SetHeight(Length::Fit());
    SetSpacing(Length::Pixels(12.0f));
}

NodeKind Form::Kind() const {
    return NodeKind::FORM;
}

NodeKind Skeleton::Kind() const {
    return NodeKind::SKELETON;
}

void Skeleton::SetLines(int value) {
    lines = value < 1 ? 1 : value;
}

Size Skeleton::Measure(TextMeasurer&, float availableWidth, float availableHeight, bool heightDefinite) {
    measured.width = width.unit == LengthUnit::FIT ? availableWidth : width.Resolve(availableWidth, availableWidth);
    const float fit = static_cast<float>(lines) * 16.0f + static_cast<float>(lines - 1) * 8.0f;
    measured.height = ResolveDefinite(height, availableHeight, fit, heightDefinite);
    return FinishMeasure(availableWidth, availableHeight);
}

void Skeleton::OnPaint(Renderer& renderer) {
    const float lineHeight = 12.0f;
    float y = bounds.y;
    for (int index = 0; index < lines; ++index) {
        const float lineWidth = index == lines - 1 ? bounds.width * 0.62f : bounds.width;
        renderer.FillRectangle(Rect{bounds.x, y, lineWidth, lineHeight}, COLOR_BG_TERTIARY, 6.0f);
        y += lineHeight + 8.0f;
    }
}

NodeKind Avatar::Kind() const {
    return NodeKind::AVATAR;
}

void Avatar::SetText(std::string value) {
    text = std::move(value);
}

Size Avatar::Measure(TextMeasurer&, float availableWidth, float availableHeight, bool heightDefinite) {
    measured.width = width.unit == LengthUnit::FIT ? 40.0f : width.Resolve(availableWidth, 40.0f);
    measured.height = ResolveDefinite(height, availableHeight, measured.width, heightDefinite);
    return FinishMeasure(availableWidth, availableHeight);
}

void Avatar::OnPaint(Renderer& renderer) {
    const float radius = std::min(bounds.width, bounds.height) * 0.5f;
    renderer.FillRectangle(bounds, COLOR_BRAND_PRIMARY_DISABLED, radius);
    TextStyle style;
    style.fontSize = 14.0f;
    style.fontWeight = WEIGHT_SEMIBOLD;
    style.color = COLOR_BRAND_PRIMARY;
    style.horizontal = TextAlign::CENTER;
    style.vertical = TextAlign::CENTER;
    std::string initials;
    for (char character : text) {
        if (character != ' ' && initials.size() < 2) {
            initials.push_back(character);
        }
    }
    renderer.DrawString(initials.empty() ? "?" : initials, bounds, style);
}

NodeKind EmptyState::Kind() const {
    return NodeKind::EMPTY;
}

void EmptyState::SetTitle(std::string value) {
    title = std::move(value);
}

void EmptyState::SetText(std::string value) {
    text = std::move(value);
}

Size EmptyState::Measure(TextMeasurer&, float availableWidth, float availableHeight, bool heightDefinite) {
    measured.width = width.unit == LengthUnit::FIT ? availableWidth : width.Resolve(availableWidth, availableWidth);
    measured.height = ResolveDefinite(height, availableHeight, 120.0f, heightDefinite);
    return FinishMeasure(availableWidth, availableHeight);
}

void EmptyState::OnPaint(Renderer& renderer) {
    renderer.FillRectangle(bounds, COLOR_BG_SECONDARY, RADIUS_LG);
    TextStyle titleStyle;
    titleStyle.fontSize = TYPE_BODY;
    titleStyle.fontWeight = WEIGHT_SEMIBOLD;
    titleStyle.color = COLOR_TEXT_PRIMARY;
    titleStyle.horizontal = TextAlign::CENTER;
    renderer.DrawString(title, Rect{bounds.x + 16.0f, bounds.y + 28.0f, std::max(0.0f, bounds.width - 32.0f), 24.0f}, titleStyle);
    TextStyle body;
    body.fontSize = TYPE_CAPTION;
    body.color = COLOR_TEXT_SECONDARY;
    body.horizontal = TextAlign::CENTER;
    renderer.DrawString(text, Rect{bounds.x + 16.0f, bounds.y + 56.0f, std::max(0.0f, bounds.width - 32.0f), 32.0f}, body);
}

Breadcrumb::Breadcrumb() {
    SetWidth(Length::Fit());
    SetHeight(Length::Fit());
    SetSpacing(Length::Pixels(16.0f));
    SetAlignment(Alignment::CENTER);
}

NodeKind Breadcrumb::Kind() const {
    return NodeKind::BREADCRUMB;
}

void Breadcrumb::OnPaint(Renderer& renderer) {
    PaintChildren(renderer);
    TextStyle style;
    style.fontSize = TYPE_BODY;
    style.color = COLOR_TEXT_TERTIARY;
    style.vertical = TextAlign::CENTER;
    for (std::size_t index = 1; index < children.size(); ++index) {
        const Rect previous = children[index - 1]->Bounds();
        renderer.DrawString("/", Rect{previous.x + previous.width, previous.y, 16.0f, previous.height}, style);
    }
}

NodeKind Pagination::Kind() const {
    return NodeKind::PAGINATION;
}

void Pagination::SetPages(int value) {
    pages = value < 1 ? 1 : value;
    if (page > pages) {
        page = pages;
    }
}

void Pagination::SetPage(int value) {
    page = value < 1 ? 1 : (value > pages ? pages : value);
}

Rect Pagination::ChipRect(int index) const {
    return Rect{bounds.x + static_cast<float>(index) * 36.0f, bounds.y, 32.0f, bounds.height};
}

Size Pagination::Measure(TextMeasurer&, float availableWidth, float availableHeight, bool heightDefinite) {
    const float fit = static_cast<float>(pages) * 36.0f;
    measured.width = width.unit == LengthUnit::FIT ? fit : width.Resolve(availableWidth, fit);
    measured.height = ResolveDefinite(height, availableHeight, 32.0f, heightDefinite);
    return FinishMeasure(availableWidth, availableHeight);
}

void Pagination::OnPaint(Renderer& renderer) {
    for (int index = 0; index < pages; ++index) {
        const Rect chip = ChipRect(index);
        const bool active = index + 1 == page;
        renderer.FillRectangle(chip, active ? COLOR_BRAND_PRIMARY : COLOR_BG_SECONDARY, RADIUS_SM);
        TextStyle style;
        style.fontSize = 13.0f;
        style.fontWeight = WEIGHT_MEDIUM;
        style.color = active ? COLOR_TEXT_ON_BRAND : COLOR_TEXT_PRIMARY;
        style.horizontal = TextAlign::CENTER;
        style.vertical = TextAlign::CENTER;
        renderer.DrawString(std::to_string(index + 1), chip, style);
    }
}

Element* Pagination::HitTest(Point point) {
    return visible && ContainsPoint(bounds, point) ? this : nullptr;
}

bool Pagination::OnPointerDown(Point point) {
    for (int index = 0; index < pages; ++index) {
        if (ContainsPoint(ChipRect(index), point)) {
            page = index + 1;
            return true;
        }
    }
    return true;
}

}  // namespace Runtime
