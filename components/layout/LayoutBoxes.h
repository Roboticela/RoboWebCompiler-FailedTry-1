#pragma once

#include "ui/element/Element.h"

namespace Runtime {

class Row : public Element {
public:
    NodeKind Kind() const override;
    Size Measure(TextMeasurer& measurer, float availableWidth, float availableHeight, bool heightDefinite) override;
    void Arrange(const Rect& rect) override;
    void SetPadding(Edges value);
    void SetSpacing(Length value);
    void SetAlignment(Alignment value);
    void SetJustify(JustifyContent value);

private:
    Edges padding{};
    Length spacing = Length::Pixels(0.0f);
    Alignment alignment = Alignment::CENTER;
    JustifyContent justify = JustifyContent::START;
};

class FlexBox : public Element {
public:
    NodeKind Kind() const override;
    Size Measure(TextMeasurer& measurer, float availableWidth, float availableHeight, bool heightDefinite) override;
    void Arrange(const Rect& rect) override;
    void SetDirection(FlexDirection value);
    void SetJustify(JustifyContent value);
    void SetAlignment(Alignment value);
    void SetWrap(FlexWrap value);
    void SetPadding(Edges value);
    void SetGap(Length value);

private:
    FlexDirection direction = FlexDirection::ROW;
    JustifyContent justify = JustifyContent::START;
    Alignment alignment = Alignment::START;
    FlexWrap wrap = FlexWrap::NO_WRAP;
    Edges padding{};
    Length gap = Length::Pixels(0.0f);
};

class Grid : public Element {
public:
    NodeKind Kind() const override;
    Size Measure(TextMeasurer& measurer, float availableWidth, float availableHeight, bool heightDefinite) override;
    void Arrange(const Rect& rect) override;
    void SetColumns(int value);
    void SetPadding(Edges value);
    void SetColumnGap(Length value);
    void SetRowGap(Length value);

private:
    int columns = 2;
    Edges padding{};
    Length columnGap = Length::Pixels(0.0f);
    Length rowGap = Length::Pixels(0.0f);
};

class Stack : public Element {
public:
    NodeKind Kind() const override;
    Size Measure(TextMeasurer& measurer, float availableWidth, float availableHeight, bool heightDefinite) override;
    void Arrange(const Rect& rect) override;
    void SetPadding(Edges value);

private:
    Edges padding{};
};

}  // namespace Runtime
