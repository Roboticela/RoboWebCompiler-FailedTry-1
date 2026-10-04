#pragma once

#include "components/basic/Button.h"

#include <functional>

namespace Runtime {

enum class FieldMode : std::uint8_t { TEXT, PASSWORD, AREA, NUMBER };
enum class Tone : std::uint8_t { INFO, SUCCESS, WARNING, DANGER };

class CheckBox : public Element {
public:
    NodeKind Kind() const override;
    Size Measure(TextMeasurer& measurer, float availableWidth, float availableHeight, bool heightDefinite) override;
    void OnPaint(Renderer& renderer) override;
    Element* HitTest(Point point) override;
    void Activate() override;
    bool WantsHandCursor() const override;
    void SetText(std::string value);
    void SetChecked(bool value);
    void SetEnabled(bool value);

private:
    std::string text;
    bool checked = false;
    bool enabled = true;
};

class ToggleSwitch : public Element {
public:
    NodeKind Kind() const override;
    Size Measure(TextMeasurer& measurer, float availableWidth, float availableHeight, bool heightDefinite) override;
    void OnPaint(Renderer& renderer) override;
    Element* HitTest(Point point) override;
    void Activate() override;
    bool WantsHandCursor() const override;
    void SetText(std::string value);
    void SetChecked(bool value);
    void SetEnabled(bool value);

private:
    std::string text;
    bool checked = false;
    bool enabled = true;
};

class RadioButton : public Element {
public:
    NodeKind Kind() const override;
    Size Measure(TextMeasurer& measurer, float availableWidth, float availableHeight, bool heightDefinite) override;
    void OnPaint(Renderer& renderer) override;
    Element* HitTest(Point point) override;
    void Activate() override;
    bool WantsHandCursor() const override;
    void SetText(std::string value);
    void SetChecked(bool value);
    void SetEnabled(bool value);
    void SetGroup(std::string value);
    const std::string& Group() const;

private:
    std::string text;
    std::string group;
    bool checked = false;
    bool enabled = true;
};

class Slider : public Element {
public:
    NodeKind Kind() const override;
    Size Measure(TextMeasurer& measurer, float availableWidth, float availableHeight, bool heightDefinite) override;
    void OnPaint(Renderer& renderer) override;
    Element* HitTest(Point point) override;
    bool OnPointerDown(Point point) override;
    bool OnDrag(Point point) override;
    bool WantsHandCursor() const override;
    void SetValue(float value);
    void SetRange(float minimum, float maximum);
    void SetEnabled(bool value);

private:
    void SetFromPoint(Point point);
    float Fraction() const;
    float value = 0.0f;
    float minimum = 0.0f;
    float maximum = 100.0f;
    bool enabled = true;
};

class ProgressBar : public Element {
public:
    NodeKind Kind() const override;
    Size Measure(TextMeasurer& measurer, float availableWidth, float availableHeight, bool heightDefinite) override;
    void OnPaint(Renderer& renderer) override;
    void SetValue(float value);

private:
    float value = 0.0f;
};

class TextField : public Element {
public:
    NodeKind Kind() const override;
    Size Measure(TextMeasurer& measurer, float availableWidth, float availableHeight, bool heightDefinite) override;
    void OnPaint(Renderer& renderer) override;
    Element* HitTest(Point point) override;
    bool TakesTextFocus() const override;
    void SetFocused(bool focused) override;
    bool OnChar(wchar_t character) override;
    bool OnKeyDown(unsigned key) override;
    void SetText(std::string value);
    void SetPlaceholder(std::string value);
    void SetEnabled(bool value);
    void SetMode(FieldMode value);
    void SetValidation(std::string value);
    bool Submit() override;
    void ApplyBound(const std::string& property, const std::string& value) override;

private:
    std::string DisplayText() const;
    bool CheckValidation() const;
    std::string text;
    std::string placeholder;
    FieldMode mode = FieldMode::TEXT;
    bool enabled = true;
    bool focused = false;
    bool invalid = false;
    std::string validation;
};

class OptionItem : public Element {
public:
    NodeKind Kind() const override;
    Size Measure(TextMeasurer& measurer, float availableWidth, float availableHeight, bool heightDefinite) override;
    void OnPaint(Renderer& renderer) override;
    Element* HitTest(Point point) override;
    void Activate() override;
    bool WantsHandCursor() const override;
    void SetText(std::string value);
    const std::string& Text() const;

private:
    std::string text;
};

class SelectBox : public Element {
public:
    NodeKind Kind() const override;
    Size Measure(TextMeasurer& measurer, float availableWidth, float availableHeight, bool heightDefinite) override;
    void Arrange(const Rect& rect) override;
    void OnPaint(Renderer& renderer) override;
    Element* HitTest(Point point) override;
    void Activate() override;
    bool WantsHandCursor() const override;
    void SetPlaceholder(std::string value);
    void Choose(const OptionItem& option);

private:
    std::string placeholder = "Select";
    std::string value;
    bool open = false;
};

class AlertBanner : public Element {
public:
    NodeKind Kind() const override;
    Size Measure(TextMeasurer& measurer, float availableWidth, float availableHeight, bool heightDefinite) override;
    void OnPaint(Renderer& renderer) override;
    void SetTitle(std::string value);
    void SetText(std::string value);
    void SetTone(Tone value);

private:
    std::string title;
    std::string text;
    Tone tone = Tone::INFO;
};

class Badge : public Element {
public:
    NodeKind Kind() const override;
    Size Measure(TextMeasurer& measurer, float availableWidth, float availableHeight, bool heightDefinite) override;
    void OnPaint(Renderer& renderer) override;
    void SetText(std::string value);
    void SetTone(Tone value);

private:
    std::string text;
    Tone tone = Tone::INFO;
};

class Link : public Element {
public:
    NodeKind Kind() const override;
    Size Measure(TextMeasurer& measurer, float availableWidth, float availableHeight, bool heightDefinite) override;
    void OnPaint(Renderer& renderer) override;
    Element* HitTest(Point point) override;
    void Activate() override;
    bool WantsHandCursor() const override;
    void SetText(std::string value);
    void SetOnClick(std::function<void()> callback);

private:
    std::string text;
    std::function<void()> onClick;
};

class IconButton : public Button {
public:
    IconButton();
    NodeKind Kind() const override;
};

class TabPage : public Element {
public:
    NodeKind Kind() const override;
    Size Measure(TextMeasurer& measurer, float availableWidth, float availableHeight, bool heightDefinite) override;
    void Arrange(const Rect& rect) override;
    void Activate() override;
    void SetTitle(std::string value);
    const std::string& Title() const;

private:
    std::string title;
};

class Tabs : public Element {
public:
    NodeKind Kind() const override;
    Size Measure(TextMeasurer& measurer, float availableWidth, float availableHeight, bool heightDefinite) override;
    void Arrange(const Rect& rect) override;
    void OnPaint(Renderer& renderer) override;
    Element* HitTest(Point point) override;
    void Select(TabPage& page);

private:
    int selected = 0;
    float headerHeight = 40.0f;
};

}  // namespace Runtime
