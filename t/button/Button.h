#pragma once

#include "ui/element/Element.h"

#include <functional>
#include <string>

namespace Runtime {

enum class ButtonVariant : std::uint8_t {
    PRIMARY,
    SECONDARY,
    TERTIARY,
    DESTRUCTIVE,
    DESTRUCTIVE_SECONDARY,
    LINK,
};

enum class ButtonSize : std::uint8_t {
    CONTROL_LARGE,
    CONTROL_MEDIUM,
    CONTROL_SMALL,
};

enum class ButtonVisualState : std::uint8_t {
    REST,
    HOVER,
    PRESSED,
    DISABLED,
};

class Button : public Element {
public:
    Button();

    NodeKind Kind() const override;
    Size Measure(TextMeasurer& measurer, float availableWidth, float availableHeight, bool heightDefinite) override;
    void Arrange(const Rect& rect) override;
    void OnPaint(Renderer& renderer) override;
    Element* HitTest(Point point) override;
    bool WantsHandCursor() const override;
    bool Tick(float seconds) override;

    void OnPointerEnter() override;
    void OnPointerLeave() override;
    void OnPointerDown() override;
    void OnPointerUp() override;
    void OnFocus() override;
    void OnBlur() override;
    void Activate() override;

    void SetVariant(ButtonVariant value);
    void SetSize(ButtonSize value);
    void SetLabel(std::string value);
    void SetDisabled(bool value);
    void SetLoading(bool value);
    void SetFullWidth(bool value);
    void SetOnClick(std::function<void()> handler);
    void AddSlotContent(const std::string& slotName, std::unique_ptr<Element> child);

    const std::string& Label() const;
    ButtonVariant Variant() const;

private:
    Color ResolveBackground() const;
    Color ResolveTextColor() const;
    void UpdateVisualState(ButtonVisualState next);
    void ApplyChrome();
    float ControlHeight() const;
    float SidePadding() const;
    bool IconOnly() const;

    ButtonVariant variant = ButtonVariant::PRIMARY;
    ButtonSize size = ButtonSize::CONTROL_MEDIUM;
    std::string label;
    bool disabled = false;
    bool loading = false;
    bool fullWidth = false;
    ButtonVisualState visualState = ButtonVisualState::REST;
    std::function<void()> onClick;
    class Frame* root = nullptr;
    class ShapeElement* leading = nullptr;
    class ShapeElement* labelText = nullptr;
    class ShapeElement* trailing = nullptr;
    class ShapeElement* spinner = nullptr;
    float spin = 0.0f;
};

}  // namespace Runtime
