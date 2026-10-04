#pragma once

#include "components/layout/Column.h"
#include "components/layout/LayoutBoxes.h"

#include <functional>
#include <string>

namespace Runtime {

class Container : public Column {
public:
    Container();
    NodeKind Kind() const override;
};

class WrapBox : public FlexBox {
public:
    WrapBox();
    NodeKind Kind() const override;
};

class Navbar : public Row {
public:
    Navbar();
    NodeKind Kind() const override;
    void OnPaint(Renderer& renderer) override;
};

class Header : public Column {
public:
    Header();
    NodeKind Kind() const override;
};

class Section : public Column {
public:
    Section();
    NodeKind Kind() const override;
};

class Footer : public Column {
public:
    Footer();
    NodeKind Kind() const override;
};

class UiList : public Column {
public:
    UiList();
    NodeKind Kind() const override;
};

class ListItem : public Row {
public:
    ListItem();
    NodeKind Kind() const override;
    void OnPaint(Renderer& renderer) override;
    Element* HitTest(Point point) override;
    void Activate() override;
    bool WantsHandCursor() const override;
    void SetOnClick(std::function<void()> callback);

private:
    std::function<void()> onClick;
};

class Menu : public Column {
public:
    Menu();
    NodeKind Kind() const override;
};

class MenuItem : public Element {
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

class ClipBox : public Stack {
public:
    NodeKind Kind() const override;
    void OnPaint(Renderer& renderer) override;
};

class AspectBox : public Element {
public:
    NodeKind Kind() const override;
    Size Measure(TextMeasurer& measurer, float availableWidth, float availableHeight, bool heightDefinite) override;
    void Arrange(const Rect& rect) override;
    void SetRatio(float value);

private:
    float ratio = 1.0f;
};

class OverlayBox : public Stack {
public:
    OverlayBox();
    NodeKind Kind() const override;
    void Arrange(const Rect& rect) override;
};

class Dialog : public Element {
public:
    Dialog();
    NodeKind Kind() const override;
    Size Measure(TextMeasurer& measurer, float availableWidth, float availableHeight, bool heightDefinite) override;
    void Arrange(const Rect& rect) override;
    void OnPaint(Renderer& renderer) override;
    Element* HitTest(Point point) override;
    void Activate() override;
};

class Form : public Column {
public:
    Form();
    NodeKind Kind() const override;
};

class Skeleton : public Element {
public:
    NodeKind Kind() const override;
    Size Measure(TextMeasurer& measurer, float availableWidth, float availableHeight, bool heightDefinite) override;
    void OnPaint(Renderer& renderer) override;
    void SetLines(int value);

private:
    int lines = 3;
};

class Avatar : public Element {
public:
    NodeKind Kind() const override;
    Size Measure(TextMeasurer& measurer, float availableWidth, float availableHeight, bool heightDefinite) override;
    void OnPaint(Renderer& renderer) override;
    void SetText(std::string value);

private:
    std::string text;
};

class EmptyState : public Element {
public:
    NodeKind Kind() const override;
    Size Measure(TextMeasurer& measurer, float availableWidth, float availableHeight, bool heightDefinite) override;
    void OnPaint(Renderer& renderer) override;
    void SetTitle(std::string value);
    void SetText(std::string value);

private:
    std::string title;
    std::string text;
};

class Breadcrumb : public Row {
public:
    Breadcrumb();
    NodeKind Kind() const override;
    void OnPaint(Renderer& renderer) override;
};

class Pagination : public Element {
public:
    NodeKind Kind() const override;
    Size Measure(TextMeasurer& measurer, float availableWidth, float availableHeight, bool heightDefinite) override;
    void OnPaint(Renderer& renderer) override;
    Element* HitTest(Point point) override;
    bool OnPointerDown(Point point) override;
    void SetPages(int value);
    void SetPage(int value);

private:
    Rect ChipRect(int index) const;
    int pages = 1;
    int page = 1;
};

}  // namespace Runtime
