#include "components/basic/Button.h"

#include "engine/theme/DevKitTheme.h"
#include "rendering/renderer/Renderer.h"

#include <algorithm>

namespace Runtime {

NodeKind Button::Kind() const {
    return NodeKind::BUTTON;
}

void Button::SetText(std::string value) {
    text = std::move(value);
}

void Button::SetFontSize(float value) {
    fontSize = value;
}

void Button::SetFontWeight(int value) {
    fontWeight = value;
}

void Button::SetBackground(Color value) {
    background = value;
    customBackground = true;
}

void Button::SetCustomBackground(bool value) {
    customBackground = value;
}

void Button::SetVariant(ButtonVariant value) {
    variant = value;
}

void Button::SetSize(ButtonSize value) {
    size = value;
}

float Button::PadX() const {
    if (size == ButtonSize::SM) {
        return BUTTON_PAD_SM;
    }
    if (size == ButtonSize::LG) {
        return BUTTON_PAD_LG;
    }
    return BUTTON_PAD_MD;
}

float Button::MinHeight() const {
    if (size == ButtonSize::SM) {
        return BUTTON_HEIGHT_SM;
    }
    if (size == ButtonSize::LG) {
        return BUTTON_HEIGHT_LG;
    }
    return BUTTON_HEIGHT_MD;
}

Color Button::FillColor() const {
    const StylePatch patch = ActivePatch();
    if (patch.background) {
        return patch.backgroundColor;
    }
    if (customBackground) {
        if (!enabled) {
            Color faded = background;
            faded.a *= 0.55f;
            return faded;
        }
        if (pressed) {
            return ScaleColor(background, 0.82f);
        }
        if (hovered) {
            return ScaleColor(background, 0.92f);
        }
        return background;
    }
    if (!enabled) {
        return variant == ButtonVariant::DANGER ? COLOR_DANGER : COLOR_BRAND_PRIMARY_DISABLED;
    }
    if (variant == ButtonVariant::DANGER) {
        return pressed || hovered ? COLOR_DANGER_HOVER : COLOR_DANGER;
    }
    if (variant == ButtonVariant::SECONDARY) {
        return pressed || hovered ? COLOR_BRAND_PRIMARY : COLOR_BG_PRIMARY;
    }
    if (variant == ButtonVariant::ACTION) {
        return hovered ? COLOR_BG_SECONDARY : COLOR_BG_PRIMARY;
    }
    if (variant == ButtonVariant::GHOST) {
        return Color{0.0f, 0.0f, 0.0f, 0.0f};
    }
    if (pressed) {
        return COLOR_BRAND_PRIMARY_ACTIVE;
    }
    if (hovered) {
        return COLOR_BRAND_PRIMARY_HOVER;
    }
    return COLOR_BRAND_PRIMARY;
}

Color Button::LabelColor() const {
    const StylePatch patch = ActivePatch();
    if (patch.foreground) {
        return patch.foregroundColor;
    }
    if (customBackground) {
        const Color fill = FillColor();
        const float luminance = 0.2126f * fill.r + 0.7152f * fill.g + 0.0722f * fill.b;
        return luminance > 0.62f ? COLOR_TEXT_PRIMARY : COLOR_TEXT_ON_BRAND;
    }
    if (variant == ButtonVariant::SECONDARY) {
        return pressed || hovered ? COLOR_TEXT_ON_BRAND : COLOR_BRAND_PRIMARY;
    }
    if (variant == ButtonVariant::ACTION) {
        return COLOR_TEXT_PRIMARY;
    }
    if (variant == ButtonVariant::GHOST) {
        return hovered || pressed ? COLOR_BRAND_PRIMARY : COLOR_TEXT_TERTIARY;
    }
    return COLOR_TEXT_ON_BRAND;
}

void Button::SetEnabled(bool value) {
    enabled = value;
}

void Button::SetCornerRadius(Length value) {
    cornerRadius = value;
    pill = false;
}

void Button::SetOnClick(std::function<void()> callback) {
    onClick = std::move(callback);
}

Button* Button::AsButton() {
    return this;
}

void Button::ApplyBound(const std::string& property, const std::string& value) {
    Element::ApplyBound(property, value);
    if (property == "text" || property == "value") {
        SetText(value);
    }
    if (property == "enabled") {
        SetEnabled(value != "false" && value != "0");
    }
}

bool Button::SetPointerHover(bool value) {
    if (!enabled) {
        value = false;
    }
    const bool changed = Element::SetPointerHover(value);
    hovered = value;
    return changed;
}

bool Button::SetPointerPressed(bool value) {
    if (!enabled) {
        value = false;
    }
    const bool changed = Element::SetPointerPressed(value);
    pressed = value;
    return changed;
}

void Button::Activate() {
    if (!enabled) {
        return;
    }
    if (!action.empty()) {
        RunAction(action);
    }
    if (onClick) {
        onClick();
    }
}

bool Button::WantsHandCursor() const {
    return enabled;
}

Size Button::Measure(TextMeasurer& measurer, float availableWidth, float availableHeight, bool heightDefinite) {
    const Size textSize = measurer.MeasureText(text, fontSize, UNBOUNDED_MEASURE);
    const Size fit{textSize.width + PadX() * 2.0f, std::max(MinHeight(), textSize.height + BUTTON_PADDING_Y * 2.0f)};
    measured.width = width.Resolve(availableWidth, fit.width);
    measured.height = ResolveDefinite(height, availableHeight, fit.height, heightDefinite);
    return FinishMeasure(availableWidth, availableHeight);
}

void Button::OnPaint(Renderer& renderer) {
    const Color fill = FillColor();
    const float radius = pill ? std::min(bounds.height, bounds.width) * 0.5f
                              : cornerRadius.Resolve(std::min(bounds.width, bounds.height) * 0.5f, RADIUS_MD);
    if (fill.a > 0.0f) {
        renderer.FillRectangle(bounds, fill, radius);
    }
    if (!customBackground && (variant == ButtonVariant::SECONDARY || variant == ButtonVariant::ACTION)) {
        const Color border = variant == ButtonVariant::SECONDARY ? (hovered || pressed ? COLOR_BRAND_PRIMARY : COLOR_BRAND_PRIMARY)
                                                                  : (hovered ? COLOR_BORDER_STRONG : COLOR_BORDER_DEFAULT);
        renderer.StrokeRectangle(bounds, border, 1.0f, radius);
    }
    renderer.PushClip(bounds);
    TextStyle style;
    style.fontSize = fontSize;
    style.fontWeight = fontWeight;
    style.color = LabelColor();
    if (!enabled) {
        style.color.a *= 0.7f;
    }
    style.horizontal = TextAlign::CENTER;
    style.vertical = TextAlign::CENTER;
    renderer.DrawString(text, bounds, style);
    renderer.PopClip();
}

Element* Button::HitTest(Point point) {
    if (!visible) {
        return nullptr;
    }
    if (ContainsPoint(bounds, point)) {
        return this;
    }
    return nullptr;
}

}  // namespace Runtime
