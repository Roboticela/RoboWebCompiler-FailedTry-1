#pragma once

#include "engine/theme/DevKitTheme.h"
#include "ui/element/Element.h"

#include <functional>

namespace Runtime {

class Button : public Element {
public:
    NodeKind Kind() const override;
    Size Measure(TextMeasurer& measurer, float availableWidth, float availableHeight, bool heightDefinite) override;
    void OnPaint(Renderer& renderer) override;
    Element* HitTest(Point point) override;
    Button* AsButton() override;
    void ApplyBound(const std::string& property, const std::string& value) override;
    bool SetPointerHover(bool value) override;
    bool SetPointerPressed(bool value) override;
    void Activate() override;
    bool WantsHandCursor() const override;

    void SetText(std::string value);
    void SetFontSize(float value);
    void SetFontWeight(int value);
    void SetBackground(Color value);
    void SetCustomBackground(bool value);
    void SetEnabled(bool value);
    void SetCornerRadius(Length value);
    void SetVariant(ButtonVariant value);
    void SetSize(ButtonSize value);
    void SetOnClick(std::function<void()> callback);

private:
    float PadX() const;
    float MinHeight() const;
    Color FillColor() const;
    Color LabelColor() const;

    std::string text;
    float fontSize = TYPE_BUTTON;
    int fontWeight = WEIGHT_MEDIUM;
    Color background = COLOR_BRAND_PRIMARY;
    bool customBackground = false;
    bool enabled = true;
    Length cornerRadius = Length::Pixels(RADIUS_PILL);
    bool pill = true;
    ButtonVariant variant = ButtonVariant::PRIMARY;
    ButtonSize size = ButtonSize::MD;
    bool hovered = false;
    bool pressed = false;
    std::function<void()> onClick;
};

}  // namespace Runtime
