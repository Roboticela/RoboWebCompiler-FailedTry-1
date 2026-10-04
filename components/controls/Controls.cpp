#include "components/controls/Controls.h"

#include "engine/core/Log.h"
#include "rendering/renderer/Renderer.h"

#include <algorithm>

namespace Runtime {
namespace {

Color ToneFill(Tone tone) {
    switch (tone) {
    case Tone::SUCCESS:
        return Color{229.0f / 255.0f, 246.0f / 255.0f, 234.0f / 255.0f, 1.0f};
    case Tone::WARNING:
        return Color{255.0f / 255.0f, 244.0f / 255.0f, 229.0f / 255.0f, 1.0f};
    case Tone::DANGER:
        return Color{255.0f / 255.0f, 232.0f / 255.0f, 230.0f / 255.0f, 1.0f};
    case Tone::INFO:
        return Color{232.0f / 255.0f, 242.0f / 255.0f, 255.0f / 255.0f, 1.0f};
    }
    return COLOR_BG_SECONDARY;
}

Color ToneInk(Tone tone) {
    switch (tone) {
    case Tone::SUCCESS:
        return Color{29.0f / 255.0f, 122.0f / 255.0f, 52.0f / 255.0f, 1.0f};
    case Tone::WARNING:
        return Color{154.0f / 255.0f, 103.0f / 255.0f, 0.0f, 1.0f};
    case Tone::DANGER:
        return COLOR_DANGER;
    case Tone::INFO:
        return COLOR_BRAND_PRIMARY;
    }
    return COLOR_TEXT_PRIMARY;
}

void ClearRadioGroup(Element& node, const std::string& group, RadioButton* keep) {
    if (auto* radio = dynamic_cast<RadioButton*>(&node)) {
        if (radio != keep && radio->Group() == group) {
            radio->SetChecked(false);
        }
    }
    for (const std::unique_ptr<Element>& child : node.Children()) {
        ClearRadioGroup(*child, group, keep);
    }
}

std::string AppendCode(std::string text, wchar_t character) {
    if (character < 128) {
        text.push_back(static_cast<char>(character));
        return text;
    }
    const auto code = static_cast<unsigned>(character);
    if (code < 0x800) {
        text.push_back(static_cast<char>(0xC0 | (code >> 6)));
        text.push_back(static_cast<char>(0x80 | (code & 0x3F)));
    }
    return text;
}

}  // namespace

NodeKind CheckBox::Kind() const {
    return NodeKind::CHECKBOX;
}

void CheckBox::SetText(std::string value) {
    text = std::move(value);
}

void CheckBox::SetChecked(bool value) {
    checked = value;
}

void CheckBox::SetEnabled(bool value) {
    enabled = value;
}

Size CheckBox::Measure(TextMeasurer& measurer, float availableWidth, float availableHeight, bool heightDefinite) {
    const Size textSize = measurer.MeasureText(text, TYPE_BODY, UNBOUNDED_MEASURE);
    const float fit = 18.0f + (text.empty() ? 0.0f : 8.0f + textSize.width);
    measured.width = width.unit == LengthUnit::FIT ? fit : width.Resolve(availableWidth, fit);
    measured.height = ResolveDefinite(height, availableHeight, 28.0f, heightDefinite);
    return FinishMeasure(availableWidth, availableHeight);
}

void CheckBox::OnPaint(Renderer& renderer) {
    const Rect box{bounds.x, bounds.y + (bounds.height - 18.0f) * 0.5f, 18.0f, 18.0f};
    if (checked) {
        renderer.FillRectangle(box, enabled ? COLOR_BRAND_PRIMARY : COLOR_BRAND_PRIMARY_DISABLED, RADIUS_SM);
        TextStyle mark;
        mark.fontSize = 12.0f;
        mark.fontWeight = WEIGHT_SEMIBOLD;
        mark.color = COLOR_TEXT_ON_BRAND;
        mark.horizontal = TextAlign::CENTER;
        mark.vertical = TextAlign::CENTER;
        renderer.DrawString("✓", box, mark);
    } else {
        renderer.FillRectangle(box, COLOR_BG_PRIMARY, RADIUS_SM);
        renderer.StrokeRectangle(box, COLOR_BORDER_STRONG, 1.0f, RADIUS_SM);
    }
    if (!text.empty()) {
        TextStyle style;
        style.fontSize = TYPE_BODY;
        style.color = enabled ? COLOR_TEXT_PRIMARY : COLOR_TEXT_TERTIARY;
        style.vertical = TextAlign::CENTER;
        renderer.DrawString(text, Rect{box.x + 26.0f, bounds.y, std::max(0.0f, bounds.width - 26.0f), bounds.height}, style);
    }
}

Element* CheckBox::HitTest(Point point) {
    if (visible && ContainsPoint(bounds, point)) {
        return this;
    }
    return nullptr;
}

void CheckBox::Activate() {
    if (enabled) {
        checked = !checked;
    }
}

bool CheckBox::WantsHandCursor() const {
    return enabled;
}

NodeKind ToggleSwitch::Kind() const {
    return NodeKind::SWITCH;
}

void ToggleSwitch::SetText(std::string value) {
    text = std::move(value);
}

void ToggleSwitch::SetChecked(bool value) {
    checked = value;
}

void ToggleSwitch::SetEnabled(bool value) {
    enabled = value;
}

Size ToggleSwitch::Measure(TextMeasurer& measurer, float availableWidth, float availableHeight, bool heightDefinite) {
    const Size textSize = measurer.MeasureText(text, TYPE_BODY, UNBOUNDED_MEASURE);
    const float fit = 44.0f + (text.empty() ? 0.0f : 8.0f + textSize.width);
    measured.width = width.unit == LengthUnit::FIT ? fit : width.Resolve(availableWidth, fit);
    measured.height = ResolveDefinite(height, availableHeight, 32.0f, heightDefinite);
    return FinishMeasure(availableWidth, availableHeight);
}

void ToggleSwitch::OnPaint(Renderer& renderer) {
    const Rect track{bounds.x, bounds.y + (bounds.height - 26.0f) * 0.5f, 44.0f, 26.0f};
    const Color trackColor = checked ? (enabled ? COLOR_BRAND_PRIMARY : COLOR_BRAND_PRIMARY_DISABLED) : COLOR_BG_TERTIARY;
    renderer.FillRectangle(track, trackColor, 13.0f);
    const float knobX = checked ? track.x + track.width - 22.0f : track.x + 2.0f;
    renderer.FillRectangle(Rect{knobX, track.y + 2.0f, 22.0f, 22.0f}, COLOR_BG_PRIMARY, 11.0f);
    if (!text.empty()) {
        TextStyle style;
        style.fontSize = TYPE_BODY;
        style.color = COLOR_TEXT_PRIMARY;
        style.vertical = TextAlign::CENTER;
        renderer.DrawString(text, Rect{track.x + 52.0f, bounds.y, std::max(0.0f, bounds.width - 52.0f), bounds.height}, style);
    }
}

Element* ToggleSwitch::HitTest(Point point) {
    return visible && ContainsPoint(bounds, point) ? this : nullptr;
}

void ToggleSwitch::Activate() {
    if (enabled) {
        checked = !checked;
    }
}

bool ToggleSwitch::WantsHandCursor() const {
    return enabled;
}

NodeKind RadioButton::Kind() const {
    return NodeKind::RADIO;
}

void RadioButton::SetText(std::string value) {
    text = std::move(value);
}

void RadioButton::SetChecked(bool value) {
    checked = value;
}

void RadioButton::SetEnabled(bool value) {
    enabled = value;
}

void RadioButton::SetGroup(std::string value) {
    group = std::move(value);
}

const std::string& RadioButton::Group() const {
    return group;
}

Size RadioButton::Measure(TextMeasurer& measurer, float availableWidth, float availableHeight, bool heightDefinite) {
    const Size textSize = measurer.MeasureText(text, TYPE_BODY, UNBOUNDED_MEASURE);
    const float fit = 18.0f + (text.empty() ? 0.0f : 8.0f + textSize.width);
    measured.width = width.unit == LengthUnit::FIT ? fit : width.Resolve(availableWidth, fit);
    measured.height = ResolveDefinite(height, availableHeight, 28.0f, heightDefinite);
    return FinishMeasure(availableWidth, availableHeight);
}

void RadioButton::OnPaint(Renderer& renderer) {
    const Rect ring{bounds.x, bounds.y + (bounds.height - 18.0f) * 0.5f, 18.0f, 18.0f};
    renderer.FillRectangle(ring, COLOR_BG_PRIMARY, 9.0f);
    renderer.StrokeRectangle(ring, checked ? COLOR_BRAND_PRIMARY : COLOR_BORDER_STRONG, checked ? 2.0f : 1.0f, 9.0f);
    if (checked) {
        renderer.FillRectangle(Rect{ring.x + 5.0f, ring.y + 5.0f, 8.0f, 8.0f}, enabled ? COLOR_BRAND_PRIMARY : COLOR_BRAND_PRIMARY_DISABLED, 4.0f);
    }
    if (!text.empty()) {
        TextStyle style;
        style.fontSize = TYPE_BODY;
        style.color = COLOR_TEXT_PRIMARY;
        style.vertical = TextAlign::CENTER;
        renderer.DrawString(text, Rect{ring.x + 26.0f, bounds.y, std::max(0.0f, bounds.width - 26.0f), bounds.height}, style);
    }
}

Element* RadioButton::HitTest(Point point) {
    return visible && ContainsPoint(bounds, point) ? this : nullptr;
}

void RadioButton::Activate() {
    if (!enabled) {
        return;
    }
    checked = true;
    Element* root = this;
    while (root->Parent() != nullptr) {
        root = root->Parent();
    }
    ClearRadioGroup(*root, group, this);
}

bool RadioButton::WantsHandCursor() const {
    return enabled;
}

NodeKind Slider::Kind() const {
    return NodeKind::SLIDER;
}

void Slider::SetValue(float next) {
    value = next < minimum ? minimum : (next > maximum ? maximum : next);
}

void Slider::SetRange(float nextMinimum, float nextMaximum) {
    minimum = nextMinimum;
    maximum = nextMaximum <= nextMinimum ? nextMinimum + 1.0f : nextMaximum;
    SetValue(value);
}

void Slider::SetEnabled(bool next) {
    enabled = next;
}

float Slider::Fraction() const {
    const float span = maximum - minimum;
    if (span <= 0.0f) {
        return 0.0f;
    }
    return (value - minimum) / span;
}

void Slider::SetFromPoint(Point point) {
    const float trackWidth = std::max(1.0f, bounds.width - 16.0f);
    const float fraction = std::clamp((point.x - bounds.x - 8.0f) / trackWidth, 0.0f, 1.0f);
    SetValue(minimum + fraction * (maximum - minimum));
}

Size Slider::Measure(TextMeasurer&, float availableWidth, float availableHeight, bool heightDefinite) {
    measured.width = width.unit == LengthUnit::FIT ? availableWidth : width.Resolve(availableWidth, availableWidth);
    measured.height = ResolveDefinite(height, availableHeight, 32.0f, heightDefinite);
    return FinishMeasure(availableWidth, availableHeight);
}

void Slider::OnPaint(Renderer& renderer) {
    const float trackY = bounds.y + bounds.height * 0.5f - 2.0f;
    const Rect track{bounds.x + 8.0f, trackY, std::max(0.0f, bounds.width - 16.0f), 4.0f};
    renderer.FillRectangle(track, COLOR_BG_TERTIARY, 2.0f);
    Rect fill = track;
    fill.width *= Fraction();
    renderer.FillRectangle(fill, enabled ? COLOR_BRAND_PRIMARY : COLOR_BRAND_PRIMARY_DISABLED, 2.0f);
    const float knobX = track.x + track.width * Fraction() - 8.0f;
    renderer.FillRectangle(Rect{knobX, bounds.y + bounds.height * 0.5f - 8.0f, 16.0f, 16.0f}, COLOR_BG_PRIMARY, 8.0f);
    renderer.StrokeRectangle(Rect{knobX, bounds.y + bounds.height * 0.5f - 8.0f, 16.0f, 16.0f}, COLOR_BRAND_PRIMARY, 2.0f, 8.0f);
}

Element* Slider::HitTest(Point point) {
    return visible && ContainsPoint(bounds, point) ? this : nullptr;
}

bool Slider::OnPointerDown(Point point) {
    if (enabled) {
        SetFromPoint(point);
    }
    return true;
}

bool Slider::OnDrag(Point point) {
    if (!enabled) {
        return false;
    }
    SetFromPoint(point);
    return true;
}

bool Slider::WantsHandCursor() const {
    return enabled;
}

NodeKind ProgressBar::Kind() const {
    return NodeKind::PROGRESS;
}

void ProgressBar::SetValue(float next) {
    value = next < 0.0f ? 0.0f : (next > 100.0f ? 100.0f : next);
}

Size ProgressBar::Measure(TextMeasurer&, float availableWidth, float availableHeight, bool heightDefinite) {
    measured.width = width.unit == LengthUnit::FIT ? availableWidth : width.Resolve(availableWidth, availableWidth);
    measured.height = ResolveDefinite(height, availableHeight, 8.0f, heightDefinite);
    return FinishMeasure(availableWidth, availableHeight);
}

void ProgressBar::OnPaint(Renderer& renderer) {
    renderer.FillRectangle(bounds, COLOR_BG_TERTIARY, bounds.height * 0.5f);
    Rect fill = bounds;
    fill.width *= value / 100.0f;
    renderer.FillRectangle(fill, COLOR_BRAND_PRIMARY, bounds.height * 0.5f);
}

NodeKind TextField::Kind() const {
    return NodeKind::TEXT_FIELD;
}

void TextField::SetText(std::string value) {
    text = std::move(value);
}

void TextField::SetPlaceholder(std::string value) {
    placeholder = std::move(value);
}

void TextField::SetEnabled(bool value) {
    enabled = value;
}

void TextField::SetMode(FieldMode value) {
    mode = value;
}

std::string TextField::DisplayText() const {
    if (mode != FieldMode::PASSWORD) {
        return text;
    }
    return std::string(text.size(), '*');
}

Size TextField::Measure(TextMeasurer&, float availableWidth, float availableHeight, bool heightDefinite) {
    const float fitHeight = mode == FieldMode::AREA ? 96.0f : 44.0f;
    measured.width = width.unit == LengthUnit::FIT ? std::min(availableWidth, 320.0f) : width.Resolve(availableWidth, 320.0f);
    measured.height = ResolveDefinite(height, availableHeight, fitHeight, heightDefinite);
    return FinishMeasure(availableWidth, availableHeight);
}

void TextField::OnPaint(Renderer& renderer) {
    renderer.FillRectangle(bounds, enabled ? COLOR_BG_PRIMARY : COLOR_BG_SECONDARY, RADIUS_MD);
    const Color border = invalid ? COLOR_DANGER : (focused ? COLOR_BRAND_PRIMARY : COLOR_BORDER_DEFAULT);
    renderer.StrokeRectangle(bounds, border, focused || invalid ? 1.5f : 1.0f, RADIUS_MD);
    const Rect content{bounds.x + 12.0f, bounds.y, std::max(0.0f, bounds.width - 24.0f), bounds.height};
    TextStyle style;
    style.fontSize = TYPE_BODY;
    style.color = text.empty() ? COLOR_TEXT_TERTIARY : COLOR_TEXT_PRIMARY;
    style.vertical = mode == FieldMode::AREA ? TextAlign::START : TextAlign::CENTER;
    const std::string shown = text.empty() ? placeholder : DisplayText();
    renderer.PushClip(bounds);
    renderer.DrawString(shown, mode == FieldMode::AREA ? Rect{content.x, content.y + 10.0f, content.width, content.height - 16.0f} : content, style);
    if (focused && !text.empty()) {
        renderer.FillRectangle(Rect{content.x + 2.0f, bounds.y + bounds.height - 10.0f, std::min(content.width, 80.0f), 1.0f}, COLOR_BRAND_PRIMARY, 0.0f);
    }
    renderer.PopClip();
}

Element* TextField::HitTest(Point point) {
    return visible && enabled && ContainsPoint(bounds, point) ? this : nullptr;
}

bool TextField::TakesTextFocus() const {
    return enabled;
}

void TextField::SetFocused(bool value) {
    Element::SetFocused(value);
    focused = value;
}

void TextField::SetValidation(std::string value) {
    validation = std::move(value);
}

void TextField::ApplyBound(const std::string& property, const std::string& value) {
    Element::ApplyBound(property, value);
    if (property == "text" || property == "value") {
        SetText(value);
    }
}

bool TextField::CheckValidation() const {
    if (validation.empty()) {
        return true;
    }
    if (validation == "required") {
        return !text.empty();
    }
    if (validation == "email") {
        const std::size_t at = text.find('@');
        return at != std::string::npos && at > 0 && text.find('.', at) != std::string::npos;
    }
    if (validation == "number") {
        if (text.empty()) {
            return false;
        }
        int dots = 0;
        for (char character : text) {
            if (character == '.') {
                ++dots;
            } else if (character < '0' || character > '9') {
                return false;
            }
        }
        return dots <= 1;
    }
    return true;
}

bool TextField::Submit() {
    invalid = !CheckValidation();
    return !invalid && Element::Submit();
}

bool TextField::OnChar(wchar_t character) {
    if (!focused || !enabled || character < 32) {
        return false;
    }
    if (mode == FieldMode::NUMBER && (character < L'0' || character > L'9') && character != L'.' && character != L'-') {
        return false;
    }
    text = AppendCode(std::move(text), character);
    return true;
}

bool TextField::OnKeyDown(unsigned key) {
    if (!focused || !enabled || key != 0x08 || text.empty()) {
        return false;
    }
    while (!text.empty() && (static_cast<unsigned char>(text.back()) & 0xC0) == 0x80) {
        text.pop_back();
    }
    if (!text.empty()) {
        text.pop_back();
    }
    return true;
}

NodeKind OptionItem::Kind() const {
    return NodeKind::OPTION;
}

void OptionItem::SetText(std::string value) {
    text = std::move(value);
}

const std::string& OptionItem::Text() const {
    return text;
}

Size OptionItem::Measure(TextMeasurer&, float availableWidth, float, bool) {
    measured = Size{availableWidth, 36.0f};
    return measured;
}

void OptionItem::OnPaint(Renderer& renderer) {
    renderer.FillRectangle(bounds, COLOR_BG_PRIMARY, 0.0f);
    TextStyle style;
    style.fontSize = TYPE_BODY;
    style.color = COLOR_TEXT_PRIMARY;
    style.vertical = TextAlign::CENTER;
    renderer.DrawString(text, Rect{bounds.x + 12.0f, bounds.y, std::max(0.0f, bounds.width - 16.0f), bounds.height}, style);
}

Element* OptionItem::HitTest(Point point) {
    return visible && bounds.height > 0.0f && ContainsPoint(bounds, point) ? this : nullptr;
}

void OptionItem::Activate() {
    if (auto* select = dynamic_cast<SelectBox*>(Parent())) {
        select->Choose(*this);
    }
}

bool OptionItem::WantsHandCursor() const {
    return true;
}

NodeKind SelectBox::Kind() const {
    return NodeKind::SELECT;
}

void SelectBox::SetPlaceholder(std::string next) {
    placeholder = std::move(next);
}

void SelectBox::Choose(const OptionItem& option) {
    value = option.Text();
    open = false;
    InvalidateLayout();
}

Size SelectBox::Measure(TextMeasurer& measurer, float availableWidth, float availableHeight, bool heightDefinite) {
    for (const std::unique_ptr<Element>& child : children) {
        child->Measure(measurer, availableWidth, 36.0f, true);
    }
    measured.width = width.unit == LengthUnit::FIT ? std::min(availableWidth, 320.0f) : width.Resolve(availableWidth, 320.0f);
    measured.height = ResolveDefinite(height, availableHeight, 44.0f, heightDefinite);
    return FinishMeasure(availableWidth, availableHeight);
}

void SelectBox::Arrange(const Rect& rect) {
    bounds = BorderBox(rect);
    float y = bounds.y + bounds.height;
    for (const std::unique_ptr<Element>& child : children) {
        if (open) {
            child->Arrange(Rect{bounds.x, y, bounds.width, 36.0f});
            y += 36.0f;
        } else {
            child->Arrange(Rect{0.0f, 0.0f, 0.0f, 0.0f});
        }
    }
}

void SelectBox::OnPaint(Renderer& renderer) {
    renderer.FillRectangle(bounds, COLOR_BG_PRIMARY, RADIUS_MD);
    renderer.StrokeRectangle(bounds, COLOR_BORDER_DEFAULT, 1.0f, RADIUS_MD);
    TextStyle style;
    style.fontSize = TYPE_BODY;
    style.color = value.empty() ? COLOR_TEXT_TERTIARY : COLOR_TEXT_PRIMARY;
    style.vertical = TextAlign::CENTER;
    renderer.DrawString(value.empty() ? placeholder : value, Rect{bounds.x + 12.0f, bounds.y, std::max(0.0f, bounds.width - 36.0f), bounds.height}, style);
    renderer.DrawString("▾", Rect{bounds.x + bounds.width - 28.0f, bounds.y, 20.0f, bounds.height}, style);
    if (open) {
        PaintChildren(renderer);
        const float menuHeight = static_cast<float>(children.size()) * 36.0f;
        renderer.StrokeRectangle(Rect{bounds.x, bounds.y + bounds.height, bounds.width, menuHeight}, COLOR_BORDER_DEFAULT, 1.0f, 0.0f);
    }
}

Element* SelectBox::HitTest(Point point) {
    if (!visible) {
        return nullptr;
    }
    if (open) {
        if (Element* child = HitTestChildren(point)) {
            return child;
        }
    }
    return ContainsPoint(bounds, point) ? this : nullptr;
}

void SelectBox::Activate() {
    open = !open;
    InvalidateLayout();
}

bool SelectBox::WantsHandCursor() const {
    return true;
}

NodeKind AlertBanner::Kind() const {
    return NodeKind::ALERT;
}

void AlertBanner::SetTitle(std::string value) {
    title = std::move(value);
}

void AlertBanner::SetText(std::string value) {
    text = std::move(value);
}

void AlertBanner::SetTone(Tone value) {
    tone = value;
}

Size AlertBanner::Measure(TextMeasurer&, float availableWidth, float availableHeight, bool heightDefinite) {
    measured.width = width.unit == LengthUnit::FIT ? availableWidth : width.Resolve(availableWidth, availableWidth);
    measured.height = ResolveDefinite(height, availableHeight, title.empty() ? 56.0f : 72.0f, heightDefinite);
    return FinishMeasure(availableWidth, availableHeight);
}

void AlertBanner::OnPaint(Renderer& renderer) {
    renderer.FillRectangle(bounds, ToneFill(tone), RADIUS_LG);
    renderer.FillRectangle(Rect{bounds.x, bounds.y, 4.0f, bounds.height}, ToneInk(tone), 0.0f);
    TextStyle titleStyle;
    titleStyle.fontSize = TYPE_BODY;
    titleStyle.fontWeight = WEIGHT_SEMIBOLD;
    titleStyle.color = ToneInk(tone);
    renderer.DrawString(title, Rect{bounds.x + 16.0f, bounds.y + 10.0f, std::max(0.0f, bounds.width - 28.0f), 24.0f}, titleStyle);
    TextStyle body;
    body.fontSize = 15.0f;
    body.color = COLOR_TEXT_PRIMARY;
    renderer.DrawString(text, Rect{bounds.x + 16.0f, bounds.y + 34.0f, std::max(0.0f, bounds.width - 28.0f), std::max(0.0f, bounds.height - 42.0f)}, body);
}

NodeKind Badge::Kind() const {
    return NodeKind::BADGE;
}

void Badge::SetText(std::string value) {
    text = std::move(value);
}

void Badge::SetTone(Tone value) {
    tone = value;
}

Size Badge::Measure(TextMeasurer& measurer, float availableWidth, float availableHeight, bool heightDefinite) {
    const Size textSize = measurer.MeasureText(text, TYPE_CAPTION, UNBOUNDED_MEASURE);
    const float fit = textSize.width + 16.0f;
    measured.width = width.unit == LengthUnit::FIT ? fit : width.Resolve(availableWidth, fit);
    measured.height = ResolveDefinite(height, availableHeight, 24.0f, heightDefinite);
    return FinishMeasure(availableWidth, availableHeight);
}

void Badge::OnPaint(Renderer& renderer) {
    renderer.FillRectangle(bounds, ToneFill(tone), bounds.height * 0.5f);
    TextStyle style;
    style.fontSize = TYPE_CAPTION;
    style.fontWeight = WEIGHT_MEDIUM;
    style.color = ToneInk(tone);
    style.horizontal = TextAlign::CENTER;
    style.vertical = TextAlign::CENTER;
    renderer.DrawString(text, bounds, style);
}

NodeKind Link::Kind() const {
    return NodeKind::LINK;
}

void Link::SetText(std::string value) {
    text = std::move(value);
}

void Link::SetOnClick(std::function<void()> callback) {
    onClick = std::move(callback);
}

Size Link::Measure(TextMeasurer& measurer, float availableWidth, float availableHeight, bool heightDefinite) {
    const Size textSize = measurer.MeasureText(text, TYPE_BODY, UNBOUNDED_MEASURE);
    measured.width = width.unit == LengthUnit::FIT ? textSize.width : width.Resolve(availableWidth, textSize.width);
    measured.height = ResolveDefinite(height, availableHeight, textSize.height, heightDefinite);
    return FinishMeasure(availableWidth, availableHeight);
}

void Link::OnPaint(Renderer& renderer) {
    TextStyle style;
    style.fontSize = TYPE_BODY;
    style.fontWeight = WEIGHT_MEDIUM;
    style.color = COLOR_BRAND_PRIMARY;
    style.vertical = TextAlign::CENTER;
    renderer.DrawString(text, bounds, style);
    renderer.FillRectangle(Rect{bounds.x, bounds.y + bounds.height - 1.0f, bounds.width, 1.0f}, COLOR_BRAND_PRIMARY, 0.0f);
}

Element* Link::HitTest(Point point) {
    return visible && ContainsPoint(bounds, point) ? this : nullptr;
}

void Link::Activate() {
    if (onClick) {
        onClick();
    }
}

bool Link::WantsHandCursor() const {
    return true;
}

IconButton::IconButton() {
    SetWidth(Length::Pixels(32.0f));
    SetHeight(Length::Pixels(32.0f));
    SetSize(ButtonSize::SM);
}

NodeKind IconButton::Kind() const {
    return NodeKind::ICON_BUTTON;
}

NodeKind TabPage::Kind() const {
    return NodeKind::TAB;
}

void TabPage::SetTitle(std::string value) {
    title = std::move(value);
}

const std::string& TabPage::Title() const {
    return title;
}

Size TabPage::Measure(TextMeasurer& measurer, float availableWidth, float availableHeight, bool heightDefinite) {
    float content = 0.0f;
    for (const std::unique_ptr<Element>& child : children) {
        const Size childSize = child->Measure(measurer, availableWidth, availableHeight, heightDefinite);
        content += childSize.height;
    }
    measured = Size{availableWidth, content};
    return measured;
}

void TabPage::Arrange(const Rect& rect) {
    bounds = rect;
    float y = rect.y;
    for (const std::unique_ptr<Element>& child : children) {
        const float childHeight = child->Measured().height;
        child->Arrange(Rect{rect.x, y, rect.width, childHeight});
        y += childHeight;
    }
}

void TabPage::Activate() {
    if (auto* tabs = dynamic_cast<Tabs*>(Parent())) {
        tabs->Select(*this);
    }
}

NodeKind Tabs::Kind() const {
    return NodeKind::TABS;
}

Size Tabs::Measure(TextMeasurer& measurer, float availableWidth, float availableHeight, bool heightDefinite) {
    const float outerWidth = width.unit == LengthUnit::FIT ? availableWidth : width.Resolve(availableWidth, availableWidth);
    float body = 0.0f;
    if (selected >= 0 && selected < static_cast<int>(children.size())) {
        body = children[static_cast<std::size_t>(selected)]->Measure(measurer, outerWidth, std::max(0.0f, availableHeight - headerHeight), heightDefinite).height;
    }
    for (std::size_t index = 0; index < children.size(); ++index) {
        if (static_cast<int>(index) != selected) {
            children[index]->Measure(measurer, outerWidth, 0.0f, false);
        }
    }
    measured.width = outerWidth;
    measured.height = ResolveDefinite(height, availableHeight, headerHeight + body, heightDefinite);
    return FinishMeasure(availableWidth, availableHeight);
}

void Tabs::Arrange(const Rect& rect) {
    bounds = BorderBox(rect);
    const Rect body{bounds.x, bounds.y + headerHeight, bounds.width, std::max(0.0f, bounds.height - headerHeight)};
    for (std::size_t index = 0; index < children.size(); ++index) {
        if (static_cast<int>(index) == selected) {
            children[index]->Arrange(body);
        } else {
            children[index]->Arrange(Rect{0.0f, 0.0f, 0.0f, 0.0f});
        }
    }
}

void Tabs::OnPaint(Renderer& renderer) {
    const float tabWidth = children.empty() ? bounds.width : bounds.width / static_cast<float>(children.size());
    for (std::size_t index = 0; index < children.size(); ++index) {
        const auto* page = dynamic_cast<const TabPage*>(children[index].get());
        const Rect tab{bounds.x + tabWidth * static_cast<float>(index), bounds.y, tabWidth, headerHeight};
        const bool active = static_cast<int>(index) == selected;
        TextStyle style;
        style.fontSize = TYPE_BODY;
        style.fontWeight = active ? WEIGHT_SEMIBOLD : WEIGHT_REGULAR;
        style.color = active ? COLOR_BRAND_PRIMARY : COLOR_TEXT_SECONDARY;
        style.horizontal = TextAlign::CENTER;
        style.vertical = TextAlign::CENTER;
        renderer.DrawString(page != nullptr ? page->Title() : "Tab", tab, style);
        if (active) {
            renderer.FillRectangle(Rect{tab.x + 12.0f, tab.y + tab.height - 2.0f, std::max(0.0f, tab.width - 24.0f), 2.0f}, COLOR_BRAND_PRIMARY, 1.0f);
        }
    }
    renderer.FillRectangle(Rect{bounds.x, bounds.y + headerHeight - 1.0f, bounds.width, 1.0f}, COLOR_BORDER_DEFAULT, 0.0f);
    if (selected >= 0 && selected < static_cast<int>(children.size())) {
        children[static_cast<std::size_t>(selected)]->Paint(renderer);
    }
}

Element* Tabs::HitTest(Point point) {
    if (!visible || !ContainsPoint(bounds, point)) {
        return nullptr;
    }
    if (point.y < bounds.y + headerHeight && !children.empty()) {
        const float tabWidth = bounds.width / static_cast<float>(children.size());
        int index = static_cast<int>((point.x - bounds.x) / tabWidth);
        if (index < 0) {
            index = 0;
        }
        if (index >= static_cast<int>(children.size())) {
            index = static_cast<int>(children.size()) - 1;
        }
        return children[static_cast<std::size_t>(index)].get();
    }
    return HitTestChildren(point);
}

void Tabs::Select(TabPage& page) {
    for (std::size_t index = 0; index < children.size(); ++index) {
        if (children[index].get() == &page) {
            selected = static_cast<int>(index);
            InvalidateLayout();
            return;
        }
    }
}

}  // namespace Runtime
