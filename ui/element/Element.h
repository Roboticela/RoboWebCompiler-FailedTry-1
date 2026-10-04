#pragma once

#include "engine/math/Geometry.h"
#include "engine/theme/DevKitTheme.h"
#include "engine/theme/VisualStyle.h"
#include "engine/types/UiTypes.h"

#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Runtime {

class Renderer;
class TextMeasurer;

class Element {
public:
    virtual ~Element() = default;

    virtual NodeKind Kind() const = 0;
    virtual Size Measure(TextMeasurer& measurer, float availableWidth, float availableHeight, bool heightDefinite) = 0;
    virtual void Arrange(const Rect& rect);
    void Paint(Renderer& renderer);
    virtual Element* HitTest(Point point);
    virtual bool SetPointerHover(bool hovered);
    virtual bool SetPointerPressed(bool pressed);
    virtual bool OnPointerDown(Point point);
    virtual bool OnDrag(Point point);
    virtual bool OnWheel(int delta);
    virtual void Activate();
    virtual bool WantsHandCursor() const;
    virtual bool TakesTextFocus() const;
    virtual void SetFocused(bool focused);
    virtual bool OnChar(wchar_t character);
    virtual bool OnKeyDown(unsigned key);

    Element* FindById(std::string_view id);

    const std::string& Id() const;
    void SetId(std::string value);
    Length Width() const;
    Length Height() const;
    void SetWidth(Length value);
    void SetHeight(Length value);
    Rect Bounds() const;
    Size Measured() const;
    int ZIndex() const;
    void SetZIndex(int value);
    Length MinWidth() const;
    Length MaxWidth() const;
    Length MinHeight() const;
    Length MaxHeight() const;
    void SetMinWidth(Length value);
    void SetMaxWidth(Length value);
    void SetMinHeight(Length value);
    void SetMaxHeight(Length value);
    Edges Margin() const;
    void SetMargin(Edges value);
    float Opacity() const;
    void SetOpacity(float value);
    bool Visible() const;
    void SetVisible(bool value);
    Length OffsetX() const;
    Length OffsetY() const;
    void SetOffsetX(Length value);
    void SetOffsetY(Length value);
    FramePosition PositionMode() const;
    void SetPositionMode(FramePosition value);
    void SetChrome(Color background, bool enabled);
    void SetBorder(float thickness, Color color);
    void SetChromeRadius(Length value);
    void SetVisualStyle(const VisualStyle& style);
    StylePatch ActivePatch() const;
    bool HasVisualStyle() const;
    float Grow() const;
    void SetGrow(float value);
    int ColumnSpan() const;
    void SetColumnSpan(int value);
    void SetTip(std::string value);
    void SetHiddenBelow(float value);
    void SetViewport(float width, float height);
    float ViewportWidth() const;
    float ViewportHeight() const;
    void AddBinding(std::string property, std::string key);
    void SetAction(std::string value);
    void RunAction(const std::string& command);
    void SeedState(std::vector<std::pair<std::string, std::string>> states);
    void ApplyState();
    virtual bool Submit();
    virtual void ApplyBound(const std::string& property, const std::string& value);
    Element* Parent() const;
    void InvalidateLayout();
    bool TakeLayoutDirty();
    void AddChild(std::unique_ptr<Element> child);
    const std::vector<std::unique_ptr<Element>>& Children() const;

protected:
    virtual void OnPaint(Renderer& renderer);
    void PaintChildren(Renderer& renderer);
    void PaintChrome(Renderer& renderer);
    Element* HitTestChildren(Point point);
    Size FinishMeasure(float availableWidth, float availableHeight);
    Rect BorderBox(const Rect& outer) const;

    std::string id;
    Length width = Length::Fit();
    Length height = Length::Fit();
    Length minWidth = Length::Fit();
    Length maxWidth = Length::Fit();
    Length minHeight = Length::Fit();
    Length maxHeight = Length::Fit();
    Edges margin{};
    float opacity = 1.0f;
    bool visible = true;
    Length offsetX = Length::Pixels(0.0f);
    Length offsetY = Length::Pixels(0.0f);
    FramePosition positionMode = FramePosition::FLOW;
    bool chromeBackground = false;
    Color background = COLOR_BG_PRIMARY;
    float borderWidth = 0.0f;
    Color borderColor = COLOR_BORDER_DEFAULT;
    Length chromeRadius = Length::Pixels(0.0f);
    Rect bounds{};
    Size measured{};
    int zIndex = 0;
    bool layoutDirty = false;
    bool pointerHover = false;
    bool pointerPressed = false;
    bool pointerFocused = false;
    VisualStyle visual;
    float grow = 0.0f;
    int columnSpan = 1;
    std::string tip;
    float hideBelow = 0.0f;
    float viewportWidth = 0.0f;
    float viewportHeight = 0.0f;
    std::string action;
    std::vector<std::pair<std::string, std::string>> bindings;
    std::unordered_map<std::string, std::string> state;
    Element* parent = nullptr;
    std::vector<std::unique_ptr<Element>> children;
};

}  // namespace Runtime
