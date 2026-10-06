#pragma once

#include "engine/math/Color.h"
#include "ui/element/Element.h"

#include <string>
#include <vector>

namespace Runtime {

struct FrameShadow {
    Color color{0.0f, 0.0f, 0.0f, 0.16f};
    float x = 0.0f;
    float y = 0.0f;
    float blur = 0.0f;
    float spread = 0.0f;
};

class Frame : public Element {
public:
    NodeKind Kind() const override;
    Size Measure(TextMeasurer& measurer, float availableWidth, float availableHeight, bool heightDefinite) override;
    void Arrange(const Rect& rect) override;
    void OnPaint(Renderer& renderer) override;
    bool PaintsOwnBorder() const override { return true; }
    Element* HitTest(Point point) override;
    void ApplyBound(const std::string& property, const std::string& value) override;
    bool OnWheel(int delta, bool shift = false, int axis = 0) override;
    bool OnPointerDown(Point point) override;
    bool OnDrag(Point point) override;
    bool OnKeyDown(unsigned key, bool shift = false) override;
    bool Tick(float seconds) override;
    bool SetPointerHover(bool hovered) override;
    bool WantsHandCursor() const override;

    void SetName(std::string value);
    void SetClassName(std::string value);
    void SetComponentName(std::string value);
    void SetInstanceName(std::string value);
    void SetTag(std::string value);
    void SetRole(std::string value);
    void SetWidthMode(SizeMode value);
    void SetHeightMode(SizeMode value);
    void SetAspectRatio(float value);
    void SetPositionMode(FramePosition value);
    void SetAnchor(FrameAnchor value);
    void SetPadding(Edges value);
    void SetLayout(FrameLayout value);
    void SetShrink(float value);
    void SetBasis(Length value);
    void SetGap(Length value);
    void SetRowGap(Length value);
    void SetColumnGap(Length value);
    void SetAlign(Alignment value);
    void SetJustify(Alignment value);
    void SetJustifyBetween(bool value);
    void SetWrap(bool value);
    void SetColumns(int value);
    void SetRows(int value);
    void SetColumn(int value);
    void SetRow(int value);
    void SetRowSpan(int value);
    void SetBackgroundColor(Color value);
    void SetBackgroundImage(std::string value);
    void SetGradient(std::string value);
    void SetBorderStyle(std::string value);
    void SetSideBorder(int side, float width);
    void SetRadius(float value);
    void SetCornerRadius(int corner, float value);
    void SetShadow(FrameShadow value);
    void AddShadow(FrameShadow value);
    void SetOverflow(OverflowMode value);
    void SetClip(bool value);
    void SetClipRadius(float value);
    void SetClipPath(std::string value);
    void SetTranslate(float x, float y);
    void SetScale(float x, float y);
    void SetRotation(float x, float y, float z);
    void SetSkew(float x, float y);
    void SetTransformOrigin(std::string value);
    void SetDisplay(FrameDisplay value);
    void SetBlendMode(std::string value);
    void SetEffects(float blurAmount, float backdrop, float bright, float contrastAmount, float saturationAmount, float gray, float hue);
    void SetCursor(std::string value);
    void SetScrollbarVisibility(std::string value);
    void SetSmoothScrolling(bool value);
    void SetScrollStep(float value);
    void RevealScrollbars();

    const std::string& Name() const;
    const std::string& ClassName() const;
    const std::string& ComponentName() const;
    const std::string& InstanceName() const;
    const std::string& Tag() const;
    const std::string& Role() const;
    FrameLayout Layout() const;
    FramePosition PositionMode() const;
    FrameAnchor Anchor() const;
    OverflowMode Overflow() const;
    FrameDisplay Display() const;
    const std::string& Cursor() const;
    const std::string& BlendMode() const;
    const std::string& Gradient() const;
    const std::string& BackgroundImage() const;
    const std::string& ScrollbarVisibility() const;
    Color BackgroundColor() const;
    float CornerRadius(int corner) const;
    const std::vector<FrameShadow>& Shadows() const;
    float Blur() const;
    bool Clips() const;
    float ContentX() const;
    float ContentY() const;
    float ContentWidth() const;
    float ContentHeight() const;
    Edges Padding() const;

private:
    bool OutOfFlow() const;
    bool Scrolls() const;
    float MaxScrollX() const;
    float MaxScrollY() const;
    float MaxScroll() const;
    bool ScrollChrome() const;
    bool BarHorizontal() const;
    void ScrollBy(float dx, float dy);
    void ScrollTo(float x, float y);
    void BeginPan(Point point);
    bool StepArrow(bool increment);
    bool PageTrack(Point point);
    bool DragThumb(Point point);
    bool DragPan(Point point);
    void PlaceScrollbars();
    void PlaceOneScrollbar(Frame& track, bool horizontal, bool both);
    bool ScrollbarWanted() const;
    bool ScrollbarHot() const;
    std::vector<Element*> PaintOrder() const;
    void PaintScrollArrow(Renderer& renderer);
    void PaintScrollThumb(Renderer& renderer);
    void PaintAutoScrollbar(Renderer& renderer);
    void PaintThumbPill(Renderer& renderer, const Rect& track, const Color& color);

    std::string name;
    std::string className;
    std::string componentName;
    std::string instanceName;
    std::string tag;
    std::string role;
    SizeMode widthMode = SizeMode::UNSET;
    SizeMode heightMode = SizeMode::UNSET;
    float aspectRatio = 0.0f;
    FramePosition positionMode = FramePosition::FLOW;
    FrameAnchor anchor = FrameAnchor::NONE;
    Edges padding{};
    FrameLayout layout = FrameLayout::NONE;
    float shrink = 1.0f;
    Length basis = Length::Fit();
    Length gap = Length::Pixels(0.0f);
    Length rowGap = Length::Fit();
    Length columnGap = Length::Fit();
    Alignment align = Alignment::START;
    Alignment justify = Alignment::START;
    bool justifyBetween = false;
    bool wrap = false;
    int columns = 1;
    int rows = 0;
    int column = 0;
    int row = 0;
    int rowSpan = 1;
    bool hasBackground = false;
    Color backgroundColor = COLOR_BG_PRIMARY;
    std::string backgroundImage;
    std::string gradient;
    std::string borderStyle = "solid";
    float sideBorder[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    float cornerRadius[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    std::vector<FrameShadow> shadows;
    OverflowMode overflow = OverflowMode::VISIBLE;
    bool clip = false;
    float clipRadius = 0.0f;
    std::string clipPath;
    float translateX = 0.0f;
    float translateY = 0.0f;
    float scaleX = 1.0f;
    float scaleY = 1.0f;
    float rotationX = 0.0f;
    float rotationY = 0.0f;
    float rotationZ = 0.0f;
    float skewX = 0.0f;
    float skewY = 0.0f;
    std::string transformOrigin = "center";
    FrameDisplay display = FrameDisplay::NORMAL;
    std::string blendMode = "normal";
    float blur = 0.0f;
    float backdropBlur = 0.0f;
    float brightness = 1.0f;
    float contrast = 1.0f;
    float saturation = 1.0f;
    float grayscale = 0.0f;
    float hueRotate = 0.0f;
    std::string cursor = "default";
    std::string scrollbarVisibility = "auto";
    bool smoothScrolling = false;
    float scrollStep = 48.0f;
    float contentWidth = 0.0f;
    float contentHeight = 0.0f;
    float scrollX = 0.0f;
    float scrollY = 0.0f;
    float scrollTargetX = 0.0f;
    float scrollTargetY = 0.0f;
    float scrollGrab = 0.0f;
    float scrollBarOpacity = 1.0f;
    float scrollReveal = 0.0f;
    float arrowRepeat = 0.0f;
    bool panActive = false;
    Point panOrigin{};
    float panScrollX = 0.0f;
    float panScrollY = 0.0f;
    float panVelX = 0.0f;
    float panVelY = 0.0f;
    float inertiaVelX = 0.0f;
    float inertiaVelY = 0.0f;
};

}  // namespace Runtime
