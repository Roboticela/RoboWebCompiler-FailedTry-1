#pragma once

#include "components/layout/Column.h"

namespace Runtime {

class Panel : public Column {
public:
    Panel();
    NodeKind Kind() const override;
};

class Card : public Column {
public:
    Card();
    NodeKind Kind() const override;
};

class Spacer : public Element {
public:
    NodeKind Kind() const override;
    Size Measure(TextMeasurer& measurer, float availableWidth, float availableHeight, bool heightDefinite) override;
};

class Divider : public Element {
public:
    NodeKind Kind() const override;
    Size Measure(TextMeasurer& measurer, float availableWidth, float availableHeight, bool heightDefinite) override;
    void OnPaint(Renderer& renderer) override;
    void SetOrientation(bool vertical);
    void SetColor(Color value);

private:
    bool vertical = false;
    Color color = COLOR_BORDER_DEFAULT;
};

class AbsoluteBox : public Element {
public:
    NodeKind Kind() const override;
    Size Measure(TextMeasurer& measurer, float availableWidth, float availableHeight, bool heightDefinite) override;
    void Arrange(const Rect& rect) override;
};

class SplitView : public Element {
public:
    NodeKind Kind() const override;
    Size Measure(TextMeasurer& measurer, float availableWidth, float availableHeight, bool heightDefinite) override;
    void Arrange(const Rect& rect) override;
    Element* HitTest(Point point) override;
    bool OnPointerDown(Point point) override;
    bool OnDrag(Point point) override;
    void OnPaint(Renderer& renderer) override;
    void SetDirection(FlexDirection value);
    void SetRatio(float value);

private:
    Rect DividerRect() const;
    FlexDirection direction = FlexDirection::ROW;
    float ratio = 0.5f;
    bool dragging = false;
};

class ImageView : public Element {
public:
    NodeKind Kind() const override;
    Size Measure(TextMeasurer& measurer, float availableWidth, float availableHeight, bool heightDefinite) override;
    void OnPaint(Renderer& renderer) override;
    void SetSource(std::string value);

private:
    std::string source;
};

class ScrollView : public Element {
public:
    NodeKind Kind() const override;
    Size Measure(TextMeasurer& measurer, float availableWidth, float availableHeight, bool heightDefinite) override;
    void Arrange(const Rect& rect) override;
    void OnPaint(Renderer& renderer) override;
    Element* HitTest(Point point) override;
    bool OnPointerDown(Point point) override;
    bool OnDrag(Point point) override;
    bool OnWheel(int delta) override;
    void SetPadding(Edges value);
    void SetSpacing(Length value);

private:
    float MaxScroll() const;
    Rect TrackRect() const;
    Rect ThumbRect() const;
    Edges padding{};
    Length spacing = Length::Pixels(0.0f);
    float contentHeight = 0.0f;
    float scrollY = 0.0f;
    bool thumbDrag = false;
    float dragOffset = 0.0f;
};

}  // namespace Runtime
