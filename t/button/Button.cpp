#include "t/button/Button.h"

#include "compiler/types/ValueParser.h"
#include "components/frame/Frame.h"
#include "components/primitive/PrimitiveElement.h"

#include <cmath>

namespace Runtime {
namespace {

Color Token(const char* name) {
    const std::optional<Color> color = ParseColor(name);
    return color.has_value() ? *color : Color{};
}

Color MixColor(Color from, Color toward, float amount) {
    return Color{from.r + (toward.r - from.r) * amount, from.g + (toward.g - from.g) * amount, from.b + (toward.b - from.b) * amount,
                 from.a + (toward.a - from.a) * amount};
}

}  // namespace

Button::Button() {
    auto frame = std::make_unique<Frame>();
    frame->SetId("root");
    frame->SetLayout(FrameLayout::HORIZONTAL);
    frame->SetAlign(Alignment::CENTER);
    frame->SetJustify(Alignment::CENTER);
    frame->SetGap(Length::Pixels(8.0f));
    frame->SetRadius(980.0f);
    frame->SetCursor("pointer");
    frame->AddBinding("background", "visualBackground", BindMode::REACTIVE);
    frame->AddBinding("cursor", "visualCursor", BindMode::REACTIVE);

    auto leadingSlot = std::make_unique<ShapeElement>(NodeKind::SLOT);
    leadingSlot->SetText("leadingIcon");
    leadingSlot->AddBinding("visible", "loading", BindMode::REACTIVE, true);

    auto text = std::make_unique<ShapeElement>(NodeKind::TEXT);
    text->SetId("label");
    text->SetFont(17.0f, 500, Token("$color.onPrimary"), TextAlign::CENTER);
    text->AddBinding("value", "label", BindMode::REACTIVE);
    text->AddBinding("textColor", "visualTextColor", BindMode::REACTIVE);
    text->AddBinding("visible", "loading", BindMode::REACTIVE, true);
    text->AddBinding("underline", "isHovered", BindMode::REACTIVE);

    auto trailingSlot = std::make_unique<ShapeElement>(NodeKind::SLOT);
    trailingSlot->SetText("trailingIcon");
    trailingSlot->AddBinding("visible", "loading", BindMode::REACTIVE, true);

    auto spinMark = std::make_unique<ShapeElement>(NodeKind::VECTOR);
    spinMark->SetId("spinner");
    spinMark->SetShapeName("spinnerArc");
    spinMark->SetWidth(Length::Pixels(16.0f));
    spinMark->SetHeight(Length::Pixels(16.0f));
    spinMark->SetVisible(false);
    spinMark->SetStroke(Token("$color.onPrimary"), 2.0f);
    spinMark->AddBinding("visible", "loading", BindMode::REACTIVE);
    spinMark->AddBinding("stroke", "visualTextColor", BindMode::REACTIVE);

    leading = leadingSlot.get();
    labelText = text.get();
    trailing = trailingSlot.get();
    spinner = spinMark.get();
    frame->AddChild(std::move(leadingSlot));
    frame->AddChild(std::move(text));
    frame->AddChild(std::move(trailingSlot));
    frame->AddChild(std::move(spinMark));
    root = frame.get();
    AddChild(std::move(frame));
    ApplyChrome();
}

NodeKind Button::Kind() const {
    return NodeKind::INSTANCE;
}

float Button::ControlHeight() const {
    switch (size) {
    case ButtonSize::CONTROL_LARGE:
        return 50.0f;
    case ButtonSize::CONTROL_SMALL:
        return 32.0f;
    case ButtonSize::CONTROL_MEDIUM:
        return 44.0f;
    }
    return 44.0f;
}

float Button::SidePadding() const {
    switch (size) {
    case ButtonSize::CONTROL_LARGE:
        return 24.0f;
    case ButtonSize::CONTROL_SMALL:
        return 12.0f;
    case ButtonSize::CONTROL_MEDIUM:
        return 16.0f;
    }
    return 16.0f;
}

bool Button::IconOnly() const {
    const bool hasIcon = (leading != nullptr && !leading->Children().empty()) || (trailing != nullptr && !trailing->Children().empty());
    return label.empty() && hasIcon;
}

Color Button::ResolveBackground() const {
    const Color transparent = Token("transparent");
    const Color surface = Token("$color.surfaceVariant");
    Color rest = Token("$color.primary");
    switch (variant) {
    case ButtonVariant::PRIMARY:
        rest = Token("$color.primary");
        break;
    case ButtonVariant::DESTRUCTIVE:
        rest = Token("$color.error");
        break;
    case ButtonVariant::SECONDARY:
    case ButtonVariant::TERTIARY:
    case ButtonVariant::DESTRUCTIVE_SECONDARY:
    case ButtonVariant::LINK:
        rest = transparent;
        break;
    }
    if (visualState == ButtonVisualState::DISABLED) {
        return rest;
    }
    if (variant == ButtonVariant::PRIMARY && visualState == ButtonVisualState::HOVER) {
        return Token("$color.primaryHover");
    }
    if (variant == ButtonVariant::PRIMARY && visualState == ButtonVisualState::PRESSED) {
        return Token("$color.primaryActive");
    }
    if (visualState == ButtonVisualState::HOVER || visualState == ButtonVisualState::PRESSED) {
        return MixColor(rest, surface, 0.5f);
    }
    return rest;
}

Color Button::ResolveTextColor() const {
    switch (variant) {
    case ButtonVariant::PRIMARY:
    case ButtonVariant::DESTRUCTIVE:
        return Token("$color.onPrimary");
    case ButtonVariant::DESTRUCTIVE_SECONDARY:
        return Token("$color.error");
    case ButtonVariant::LINK:
        return Token("$color.primary");
    case ButtonVariant::SECONDARY:
    case ButtonVariant::TERTIARY:
        return Token("$color.foreground");
    }
    return Token("$color.foreground");
}

void Button::ApplyChrome() {
    if (root == nullptr || labelText == nullptr || spinner == nullptr) {
        return;
    }
    const float controlHeight = ControlHeight();
    const float side = SidePadding();
    const bool blocked = disabled || loading;
    root->SetHeight(Length::Pixels(controlHeight));
    root->SetPadding(Edges{Length::Pixels(0.0f), Length::Pixels(side), Length::Pixels(0.0f), Length::Pixels(side)});
    if (fullWidth) {
        root->SetWidth(Length::Fill());
        SetWidth(Length::Fill());
    } else if (IconOnly()) {
        root->SetWidth(Length::Pixels(controlHeight));
        SetWidth(Length::Pixels(controlHeight));
    } else {
        root->SetWidth(Length::Fit());
        SetWidth(Length::Fit());
    }
    SetHeight(Length::Pixels(controlHeight));
    root->SetCursor(blocked ? "notAllowed" : "pointer");
    root->SetBackgroundColor(ResolveBackground());
    if (variant == ButtonVariant::SECONDARY) {
        root->SetBorder(1.0f, Token("$color.border"));
    } else if (variant == ButtonVariant::DESTRUCTIVE_SECONDARY) {
        root->SetBorder(1.0f, Token("$color.error"));
    } else {
        root->SetBorder(0.0f, Token("$color.border"));
    }
    const Color textColor = ResolveTextColor();
    const bool hovered = visualState == ButtonVisualState::HOVER || visualState == ButtonVisualState::PRESSED;
    labelText->SetFont(17.0f, 500, textColor, TextAlign::CENTER);
    spinner->SetStroke(textColor, 2.0f);
    if (!label.empty()) {
        SetAccessibleName(label);
        root->SetName(label);
    }
    labelText->SetText(label);
    labelText->SetTextColor(textColor);
    labelText->SetUnderline(variant == ButtonVariant::LINK && hovered);
    labelText->SetVisible(!loading && !label.empty());
    spinner->SetVisible(loading);
    if (leading != nullptr) {
        leading->SetVisible(!loading && !leading->Children().empty());
    }
    if (trailing != nullptr) {
        trailing->SetVisible(!loading && !trailing->Children().empty());
    }
}

void Button::UpdateVisualState(ButtonVisualState next) {
    if (disabled) {
        next = ButtonVisualState::DISABLED;
    }
    visualState = next;
    ApplyChrome();
    InvalidateLayout();
}

void Button::SetVariant(ButtonVariant value) {
    variant = value;
    UpdateVisualState(visualState);
}

void Button::SetSize(ButtonSize value) {
    size = value;
    UpdateVisualState(visualState);
}

void Button::SetLabel(std::string value) {
    label = std::move(value);
    UpdateVisualState(visualState);
}

void Button::SetDisabled(bool value) {
    disabled = value;
    UpdateVisualState(disabled ? ButtonVisualState::DISABLED : (pointerHover ? ButtonVisualState::HOVER : ButtonVisualState::REST));
}

void Button::SetLoading(bool value) {
    loading = value;
    UpdateVisualState(visualState);
}

void Button::SetFullWidth(bool value) {
    fullWidth = value;
    UpdateVisualState(visualState);
}

void Button::SetOnClick(std::function<void()> handler) {
    onClick = std::move(handler);
}

void Button::AddSlotContent(const std::string& slotName, std::unique_ptr<Element> child) {
    ShapeElement* slot = slotName == "trailingIcon" ? trailing : leading;
    if (slot == nullptr || child == nullptr) {
        return;
    }
    slot->AddChild(std::move(child));
    ApplyChrome();
}

const std::string& Button::Label() const {
    return label;
}

ButtonVariant Button::Variant() const {
    return variant;
}

Size Button::Measure(TextMeasurer& measurer, float availableWidth, float availableHeight, bool heightDefinite) {
    ApplyChrome();
    if (root == nullptr) {
        measured = {};
        return FinishMeasure(availableWidth, availableHeight);
    }
    const Size content = root->Measure(measurer, availableWidth, availableHeight, heightDefinite);
    measured = Size{content.width, ControlHeight()};
    return FinishMeasure(availableWidth, availableHeight);
}

void Button::Arrange(const Rect& rect) {
    Element::Arrange(rect);
    if (root != nullptr) {
        root->Arrange(Rect{Bounds().x, Bounds().y, Bounds().width, Bounds().height});
    }
}

void Button::OnPaint(Renderer& renderer) {
    ApplyChrome();
    PaintChildren(renderer);
}

Element* Button::HitTest(Point point) {
    if (!Visible() || Bounds().width <= 0.0f || Bounds().height <= 0.0f) {
        return nullptr;
    }
    if (point.x >= Bounds().x && point.y >= Bounds().y && point.x < Bounds().x + Bounds().width && point.y < Bounds().y + Bounds().height) {
        return this;
    }
    return nullptr;
}

bool Button::WantsHandCursor() const {
    return !disabled && !loading;
}

bool Button::Tick(float seconds) {
    bool changed = false;
    if (loading && spinner != nullptr && seconds > 0.0f) {
        spin = std::fmod(spin + seconds * 360.0f, 360.0f);
        spinner->SetSpin(spin);
        changed = true;
    }
    return Element::Tick(seconds) || changed;
}

void Button::OnPointerEnter() {
    if (disabled || loading) {
        return;
    }
    UpdateVisualState(ButtonVisualState::HOVER);
}

void Button::OnPointerLeave() {
    if (disabled || loading) {
        return;
    }
    UpdateVisualState(ButtonVisualState::REST);
}

void Button::OnPointerDown() {
    if (disabled || loading) {
        return;
    }
    UpdateVisualState(ButtonVisualState::PRESSED);
}

void Button::OnPointerUp() {
    if (disabled || loading) {
        return;
    }
    UpdateVisualState(pointerHover ? ButtonVisualState::HOVER : ButtonVisualState::REST);
}

void Button::OnFocus() {}

void Button::OnBlur() {}

void Button::Activate() {
    if (disabled || loading) {
        return;
    }
    if (onClick) {
        onClick();
    }
    Element::Activate();
}

}  // namespace Runtime
