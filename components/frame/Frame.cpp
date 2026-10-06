#include "components/frame/Frame.h"

#include "compiler/types/ValueParser.h"
#include "rendering/renderer/Renderer.h"

#include <algorithm>
#include <cmath>

namespace Runtime {
namespace {

bool IsFrame(const Element& element) {
    return dynamic_cast<const Frame*>(&element) != nullptr;
}

const Frame* AsFrame(const Element& element) {
    return IsFrame(element) ? static_cast<const Frame*>(&element) : nullptr;
}

Frame* AsFrame(Element& element) {
    return IsFrame(element) ? static_cast<Frame*>(&element) : nullptr;
}

bool LeavesFlow(const Element& element) {
    const FramePosition position = element.PositionMode();
    return position == FramePosition::ABSOLUTE || position == FramePosition::FIXED;
}

Frame* DirectRole(Frame& root, const std::string& role) {
    for (const std::unique_ptr<Element>& child : root.Children()) {
        Frame* frame = AsFrame(*child);
        if (frame != nullptr && frame->Role() == role) {
            return frame;
        }
    }
    return nullptr;
}

std::vector<Frame*> DirectRoles(Frame& root, const std::string& role) {
    std::vector<Frame*> found;
    for (const std::unique_ptr<Element>& child : root.Children()) {
        Frame* frame = AsFrame(*child);
        if (frame != nullptr && frame->Role() == role) {
            found.push_back(frame);
        }
    }
    return found;
}

bool FrameScrolls(const Frame& frame) {
    const OverflowMode mode = frame.Overflow();
    return mode == OverflowMode::SCROLL || mode == OverflowMode::AUTO;
}

bool IsScrollChrome(const std::string& role) {
    return role == "scrollbar" || role == "thumb" || role == "decrement" || role == "increment";
}

Frame* ScrollOwnerOf(Frame& self) {
    if (FrameScrolls(self)) {
        return &self;
    }
    for (Element* node = self.Parent(); node != nullptr; node = node->Parent()) {
        Frame* frame = AsFrame(*node);
        if (frame != nullptr && FrameScrolls(*frame)) {
            return frame;
        }
    }
    return nullptr;
}

bool DescendantHovered(const Element& root) {
    if (root.PointerHover()) {
        return true;
    }
    for (const std::unique_ptr<Element>& child : root.Children()) {
        if (DescendantHovered(*child)) {
            return true;
        }
    }
    return false;
}

bool DescendantPressed(const Element& root) {
    if (root.PointerPressed()) {
        return true;
    }
    for (const std::unique_ptr<Element>& child : root.Children()) {
        if (DescendantPressed(*child)) {
            return true;
        }
    }
    return false;
}

constexpr unsigned KEY_PRIOR = 0x21;
constexpr unsigned KEY_NEXT = 0x22;
constexpr unsigned KEY_END = 0x23;
constexpr unsigned KEY_HOME = 0x24;
constexpr unsigned KEY_LEFT = 0x25;
constexpr unsigned KEY_UP = 0x26;
constexpr unsigned KEY_RIGHT = 0x27;
constexpr unsigned KEY_DOWN = 0x28;
constexpr unsigned KEY_SPACE = 0x20;

float GapOf(const Length& specific, const Length& shared, float available) {
    if (specific.unit != LengthUnit::FIT) {
        return specific.Resolve(available, 0.0f);
    }
    return shared.Resolve(available, 0.0f);
}

Length ModeLength(Length length, SizeMode mode) {
    switch (mode) {
    case SizeMode::FIXED:
        return Length::Pixels(length.unit == LengthUnit::PERCENT ? length.value : (length.unit == LengthUnit::FIT ? 0.0f : length.value));
    case SizeMode::FILL:
        return Length::Fill();
    case SizeMode::FIT:
        return Length::Fit();
    case SizeMode::PERCENT:
        return Length::Percent(length.unit == LengthUnit::FIT ? 100.0f : length.value);
    case SizeMode::UNSET:
        return length;
    }
    return length;
}

}  // namespace

NodeKind Frame::Kind() const {
    return NodeKind::FRAME;
}

void Frame::SetName(std::string value) { name = std::move(value); }
void Frame::SetClassName(std::string value) { className = std::move(value); }
void Frame::SetComponentName(std::string value) { componentName = std::move(value); }
void Frame::SetInstanceName(std::string value) { instanceName = std::move(value); }
void Frame::SetTag(std::string value) { tag = std::move(value); }
void Frame::SetRole(std::string value) { role = std::move(value); }
void Frame::SetWidthMode(SizeMode value) { widthMode = value; }
void Frame::SetHeightMode(SizeMode value) { heightMode = value; }
void Frame::SetAspectRatio(float value) { aspectRatio = value; }
void Frame::SetPositionMode(FramePosition value) {
    positionMode = value;
    Element::SetPositionMode(value);
}
void Frame::SetAnchor(FrameAnchor value) { anchor = value; }
void Frame::SetPadding(Edges value) { padding = value; }

float Frame::ContentX() const {
    return Bounds().x + padding.left.Resolve(Bounds().width, 0.0f) - (Scrolls() ? scrollX : 0.0f);
}

float Frame::ContentY() const {
    return Bounds().y + padding.top.Resolve(Bounds().width, 0.0f) - (Scrolls() ? scrollY : 0.0f);
}

float Frame::ContentWidth() const {
    return std::max(0.0f, Bounds().width - padding.SpanX(Bounds().width));
}

float Frame::ContentHeight() const {
    return std::max(0.0f, Bounds().height - padding.SpanY(Bounds().width));
}

Edges Frame::Padding() const {
    return padding;
}
void Frame::SetLayout(FrameLayout value) { layout = value; }
void Frame::SetShrink(float value) { shrink = value; }
void Frame::SetBasis(Length value) { basis = value; }
void Frame::SetGap(Length value) { gap = value; }
void Frame::SetRowGap(Length value) { rowGap = value; }
void Frame::SetColumnGap(Length value) { columnGap = value; }
void Frame::SetAlign(Alignment value) { align = value; }
void Frame::SetJustify(Alignment value) { justify = value; }
void Frame::SetJustifyBetween(bool value) { justifyBetween = value; }
void Frame::SetWrap(bool value) { wrap = value; }
void Frame::SetColumns(int value) { columns = std::max(1, value); }
void Frame::SetRows(int value) { rows = std::max(0, value); }
void Frame::SetColumn(int value) { column = std::max(0, value); }
void Frame::SetRow(int value) { row = std::max(0, value); }
void Frame::SetRowSpan(int value) { rowSpan = std::max(1, value); }
void Frame::SetBackgroundColor(Color value) { hasBackground = true; backgroundColor = value; }
void Frame::SetBackgroundImage(std::string value) { backgroundImage = std::move(value); }
void Frame::SetGradient(std::string value) { gradient = std::move(value); }
void Frame::SetBorderStyle(std::string value) { borderStyle = std::move(value); }
void Frame::SetSideBorder(int side, float widthValue) { if (side >= 0 && side < 4) { sideBorder[side] = widthValue; } }
void Frame::SetRadius(float value) { for (float& corner : cornerRadius) { corner = value; } }
void Frame::SetCornerRadius(int corner, float value) { if (corner >= 0 && corner < 4) { cornerRadius[corner] = value; } }
void Frame::SetShadow(FrameShadow value) { shadows.clear(); shadows.push_back(value); }
void Frame::AddShadow(FrameShadow value) { shadows.push_back(value); }
void Frame::SetOverflow(OverflowMode value) { overflow = value; }
void Frame::SetClip(bool value) { clip = value; }
void Frame::SetClipRadius(float value) { clipRadius = value; }
void Frame::SetClipPath(std::string value) { clipPath = std::move(value); }
void Frame::SetTranslate(float x, float y) { translateX = x; translateY = y; }
void Frame::SetScale(float x, float y) { scaleX = x; scaleY = y; }
void Frame::SetRotation(float x, float y, float z) { rotationX = x; rotationY = y; rotationZ = z; }
void Frame::SetSkew(float x, float y) { skewX = x; skewY = y; }
void Frame::SetTransformOrigin(std::string value) { transformOrigin = std::move(value); }
void Frame::SetDisplay(FrameDisplay value) { display = value; }
void Frame::SetBlendMode(std::string value) { blendMode = std::move(value); }
void Frame::SetEffects(float blurAmount, float backdrop, float bright, float contrastAmount, float saturationAmount, float gray, float hue) {
    blur = blurAmount;
    backdropBlur = backdrop;
    brightness = bright;
    contrast = contrastAmount;
    saturation = saturationAmount;
    grayscale = gray;
    hueRotate = hue;
}
void Frame::SetCursor(std::string value) { cursor = std::move(value); }
void Frame::SetScrollbarVisibility(std::string value) { scrollbarVisibility = std::move(value); }
void Frame::SetSmoothScrolling(bool value) { smoothScrolling = value; }
void Frame::SetScrollStep(float value) { scrollStep = std::max(1.0f, value); }
void Frame::RevealScrollbars() { scrollReveal = 1.25f; }

const std::string& Frame::Name() const { return name; }
const std::string& Frame::ClassName() const { return className; }
const std::string& Frame::ComponentName() const { return componentName; }
const std::string& Frame::InstanceName() const { return instanceName; }
const std::string& Frame::Tag() const { return tag; }
const std::string& Frame::Role() const { return role; }
FrameLayout Frame::Layout() const { return layout; }
FramePosition Frame::PositionMode() const { return positionMode; }
FrameAnchor Frame::Anchor() const { return anchor; }
OverflowMode Frame::Overflow() const { return overflow; }
FrameDisplay Frame::Display() const { return display; }
const std::string& Frame::Cursor() const { return cursor; }
const std::string& Frame::ScrollbarVisibility() const { return scrollbarVisibility; }
const std::string& Frame::BlendMode() const { return blendMode; }
const std::string& Frame::Gradient() const { return gradient; }
const std::string& Frame::BackgroundImage() const { return backgroundImage; }
Color Frame::BackgroundColor() const { return backgroundColor; }
float Frame::CornerRadius(int corner) const { return corner >= 0 && corner < 4 ? cornerRadius[corner] : 0.0f; }
const std::vector<FrameShadow>& Frame::Shadows() const { return shadows; }
float Frame::Blur() const { return blur; }
bool Frame::Clips() const { return clip || overflow == OverflowMode::HIDDEN || overflow == OverflowMode::SCROLL || overflow == OverflowMode::AUTO; }
bool Frame::OutOfFlow() const { return positionMode == FramePosition::ABSOLUTE || positionMode == FramePosition::FIXED; }
bool Frame::Scrolls() const { return overflow == OverflowMode::SCROLL || overflow == OverflowMode::AUTO; }
float Frame::MaxScrollX() const { return std::max(0.0f, contentWidth - bounds.width); }
float Frame::MaxScrollY() const { return std::max(0.0f, contentHeight - bounds.height); }
float Frame::MaxScroll() const { return MaxScrollY(); }
bool Frame::ScrollChrome() const { return IsScrollChrome(role); }
bool Frame::BarHorizontal() const {
    if (orientation == "horizontal" || orientation == "scrollX") {
        return true;
    }
    if (orientation == "vertical" || orientation == "scrollY") {
        return false;
    }
    return Measured().width > Measured().height + 0.5f;
}
bool Frame::WantsHandCursor() const { return cursor == "pointer"; }

void Frame::ScrollTo(float x, float y) {
    const float nextX = std::clamp(x, 0.0f, MaxScrollX());
    const float nextY = std::clamp(y, 0.0f, MaxScrollY());
    if (smoothScrolling) {
        scrollTargetX = nextX;
        scrollTargetY = nextY;
    } else {
        scrollX = nextX;
        scrollY = nextY;
        scrollTargetX = nextX;
        scrollTargetY = nextY;
    }
    RevealScrollbars();
    InvalidateLayout();
}

void Frame::ScrollBy(float dx, float dy) {
    if (smoothScrolling) {
        ScrollTo(scrollTargetX + dx, scrollTargetY + dy);
    } else {
        ScrollTo(scrollX + dx, scrollY + dy);
    }
}

void Frame::BeginPan(Point point) {
    panActive = true;
    panOrigin = point;
    panScrollX = scrollX;
    panScrollY = scrollY;
    RevealScrollbars();
}

bool Frame::StepArrow(bool increment) {
    Frame* owner = ScrollOwnerOf(*this);
    if (owner == nullptr) {
        return false;
    }
    Frame* track = role == "scrollbar" ? this : (Parent() != nullptr ? AsFrame(*Parent()) : nullptr);
    const bool horizontal = track != nullptr && track->BarHorizontal();
    const float step = owner->scrollStep;
    const float delta = increment ? step : -step;
    if (horizontal) {
        owner->ScrollBy(delta, 0.0f);
    } else {
        owner->ScrollBy(0.0f, delta);
    }
    arrowRepeat = 0.35f;
    return true;
}

bool Frame::PageTrack(Point point) {
    Frame* owner = ScrollOwnerOf(*this);
    Frame* thumb = DirectRole(*this, "thumb");
    if (owner == nullptr || thumb == nullptr) {
        return false;
    }
    const bool horizontal = BarHorizontal();
    if (horizontal) {
        const float page = std::max(24.0f, owner->Bounds().width * 0.85f);
        const float next = point.x < thumb->Bounds().x ? owner->scrollX - page : owner->scrollX + page;
        owner->ScrollTo(next, owner->scrollY);
    } else {
        const float page = std::max(24.0f, owner->Bounds().height * 0.85f);
        const float next = point.y < thumb->Bounds().y ? owner->scrollY - page : owner->scrollY + page;
        owner->ScrollTo(owner->scrollX, next);
    }
    return true;
}

bool Frame::DragThumb(Point point) {
    Frame* track = Parent() == nullptr ? nullptr : AsFrame(*Parent());
    Frame* owner = ScrollOwnerOf(*this);
    if (track == nullptr || owner == nullptr) {
        return false;
    }
    const bool horizontal = track->BarHorizontal();
    Frame* decrement = DirectRole(*track, "decrement");
    Frame* increment = DirectRole(*track, "increment");
    if (horizontal) {
        const float lead = decrement != nullptr ? decrement->Bounds().width : 0.0f;
        const float trail = increment != nullptr ? increment->Bounds().width : 0.0f;
        const float rail = std::max(0.0f, track->Bounds().width - lead - trail);
        const float travel = std::max(0.0f, rail - bounds.width);
        const float left = track->Bounds().x + lead;
        const float x = std::clamp(point.x - scrollGrab, left, left + travel);
        const float along = travel <= 0.0f ? 0.0f : (x - left) / travel;
        owner->ScrollTo(along * owner->MaxScrollX(), owner->scrollY);
    } else {
        const float lead = decrement != nullptr ? decrement->Bounds().height : 0.0f;
        const float trail = increment != nullptr ? increment->Bounds().height : 0.0f;
        const float rail = std::max(0.0f, track->Bounds().height - lead - trail);
        const float travel = std::max(0.0f, rail - bounds.height);
        const float top = track->Bounds().y + lead;
        const float y = std::clamp(point.y - scrollGrab, top, top + travel);
        const float along = travel <= 0.0f ? 0.0f : (y - top) / travel;
        owner->ScrollTo(owner->scrollX, along * owner->MaxScrollY());
    }
    return true;
}

bool Frame::DragPan(Point point) {
    if (!panActive || !Scrolls()) {
        return false;
    }
    ScrollTo(panScrollX + (panOrigin.x - point.x), panScrollY + (panOrigin.y - point.y));
    return true;
}

bool Frame::OnPointerDown(Point point) {
    Element::OnPointerDown(point);
    if (role == "thumb") {
        Frame* track = Parent() != nullptr ? AsFrame(*Parent()) : nullptr;
        const bool horizontal = track != nullptr && track->BarHorizontal();
        scrollGrab = horizontal ? point.x - bounds.x : point.y - bounds.y;
        if (Frame* owner = ScrollOwnerOf(*this)) {
            owner->RevealScrollbars();
        }
        return true;
    }
    if (role == "decrement") {
        return StepArrow(false);
    }
    if (role == "increment") {
        return StepArrow(true);
    }
    if (role == "scrollbar") {
        return PageTrack(point);
    }
    if (Frame* owner = ScrollOwnerOf(*this)) {
        if (!IsScrollChrome(role)) {
            owner->BeginPan(point);
        }
        owner->RevealScrollbars();
    }
    return true;
}

bool Frame::OnDrag(Point point) {
    if (role == "thumb") {
        return DragThumb(point);
    }
    if (panActive && Scrolls()) {
        return DragPan(point);
    }
    return false;
}

bool Frame::OnKeyDown(unsigned key) {
    if (!Scrolls()) {
        return false;
    }
    const float pageY = std::max(24.0f, bounds.height * 0.85f);
    switch (key) {
    case KEY_UP:
        ScrollBy(0.0f, -scrollStep);
        return true;
    case KEY_DOWN:
        ScrollBy(0.0f, scrollStep);
        return true;
    case KEY_LEFT:
        ScrollBy(-scrollStep, 0.0f);
        return true;
    case KEY_RIGHT:
        ScrollBy(scrollStep, 0.0f);
        return true;
    case KEY_PRIOR:
        ScrollBy(0.0f, -pageY);
        return true;
    case KEY_NEXT:
    case KEY_SPACE:
        ScrollBy(0.0f, pageY);
        return true;
    case KEY_HOME:
        ScrollTo(0.0f, 0.0f);
        return true;
    case KEY_END:
        ScrollTo(MaxScrollX(), MaxScrollY());
        return true;
    default:
        return false;
    }
}

bool Frame::SetPointerHover(bool hovered) {
    const bool changed = Element::SetPointerHover(hovered);
    if (hovered) {
        if (Frame* owner = ScrollOwnerOf(*this)) {
            owner->RevealScrollbars();
        }
    }
    return changed;
}

bool Frame::Tick(float seconds) {
    bool changed = Element::Tick(seconds);
    if (role == "decrement" || role == "increment") {
        if (PointerPressed()) {
            arrowRepeat -= seconds;
            if (arrowRepeat <= 0.0f) {
                StepArrow(role == "increment");
                arrowRepeat = 0.05f;
                changed = true;
            }
        } else {
            arrowRepeat = 0.0f;
        }
    }
    if (!Scrolls()) {
        return changed;
    }
    if (smoothScrolling) {
        const float blend = std::clamp(seconds * 14.0f, 0.0f, 1.0f);
        const float nextX = scrollX + (scrollTargetX - scrollX) * blend;
        const float nextY = scrollY + (scrollTargetY - scrollY) * blend;
        if (std::fabs(nextX - scrollX) > 0.05f || std::fabs(nextY - scrollY) > 0.05f) {
            scrollX = nextX;
            scrollY = nextY;
            InvalidateLayout();
            changed = true;
        } else if (scrollX != scrollTargetX || scrollY != scrollTargetY) {
            scrollX = scrollTargetX;
            scrollY = scrollTargetY;
            InvalidateLayout();
            changed = true;
        }
    }
    if (scrollbarVisibility == "auto") {
        if (scrollReveal > 0.0f) {
            scrollReveal = std::max(0.0f, scrollReveal - seconds);
            changed = true;
        }
        const float target = (scrollReveal > 0.0f || ScrollbarHot()) && ScrollbarWanted() ? 1.0f : 0.0f;
        const float next = scrollBarOpacity + (target - scrollBarOpacity) * std::clamp(seconds * 10.0f, 0.0f, 1.0f);
        if (std::fabs(next - scrollBarOpacity) > 0.01f) {
            scrollBarOpacity = next;
            changed = true;
        } else {
            scrollBarOpacity = target;
        }
    } else if (scrollbarVisibility == "hidden") {
        scrollBarOpacity = 0.0f;
    } else {
        scrollBarOpacity = ScrollbarWanted() ? 1.0f : 0.0f;
    }
    if (panActive && !DescendantPressed(*this) && !PointerPressed()) {
        panActive = false;
    }
    return changed;
}

bool Frame::ScrollbarWanted() const {
    return MaxScrollX() > 0.5f || MaxScrollY() > 0.5f;
}

bool Frame::ScrollbarHot() const {
    return DescendantHovered(*this);
}

std::vector<Element*> Frame::PaintOrder() const {
    std::vector<Element*> order;
    order.reserve(children.size());
    for (const std::unique_ptr<Element>& child : children) {
        order.push_back(child.get());
    }
    std::stable_sort(order.begin(), order.end(), [](const Element* left, const Element* right) { return left->ZIndex() < right->ZIndex(); });
    return order;
}

Size Frame::Measure(TextMeasurer& measurer, float availableWidth, float availableHeight, bool heightDefinite) {
    if (display == FrameDisplay::NONE) {
        measured = {};
        return measured;
    }
    const Length widthLength = basis.unit != LengthUnit::FIT && Width().unit == LengthUnit::FIT ? basis : ModeLength(Width(), widthMode);
    const float widthSpace = Width().unit == LengthUnit::SCREEN && viewportWidth > 0.0f ? viewportWidth : availableWidth;
    const float heightSpace = Height().unit == LengthUnit::SCREEN && viewportHeight > 0.0f ? viewportHeight : availableHeight;
    const float outerWidth = widthLength.unit == LengthUnit::FIT ? availableWidth : widthLength.Resolve(widthSpace, availableWidth);
    const float padX = padding.SpanX(outerWidth);
    const float padY = padding.SpanY(outerWidth);
    const float innerWidth = std::max(0.0f, outerWidth - padX);
    const Length heightLength = ModeLength(Height(), heightMode);
    float innerLimit = std::max(0.0f, availableHeight - padY);
    bool childHeightKnown = heightDefinite && heightMode != SizeMode::FIT && Height().unit != LengthUnit::FIT;
    if (heightLength.unit == LengthUnit::PIXELS || heightMode == SizeMode::FIXED) {
        innerLimit = std::max(0.0f, heightLength.Resolve(heightSpace, 0.0f) - padY);
        childHeightKnown = true;
    }
    const float columnSpace = GapOf(columnGap, gap, outerWidth);
    const float rowSpace = GapOf(rowGap, gap, outerWidth);
    const bool childHeightDefinite = childHeightKnown;

    float flowWidth = 0.0f;
    float flowHeight = 0.0f;
    float lineWidth = 0.0f;
    float lineHeight = 0.0f;
    int columnIndex = 0;
    int flowCount = 0;
    const int columnCount = std::max(1, columns);
    const float cellWidth = layout == FrameLayout::GRID
                                ? std::max(0.0f, (innerWidth - columnSpace * static_cast<float>(columnCount - 1)) / static_cast<float>(columnCount))
                                : innerWidth;

    std::vector<Element*> flowChildren;
    for (const std::unique_ptr<Element>& child : children) {
        if (!child->Visible() || LeavesFlow(*child) || (AsFrame(*child) != nullptr && AsFrame(*child)->Display() == FrameDisplay::NONE)) {
            child->Measure(measurer, innerWidth, innerLimit, childHeightKnown);
            continue;
        }
        flowChildren.push_back(child.get());
    }
    const bool shareWidth = layout == FrameLayout::HORIZONTAL && !wrap;
    const bool shareHeight = layout == FrameLayout::VERTICAL && childHeightKnown;
    if (shareWidth || shareHeight) {
        int fillCount = 0;
        float fixedMain = 0.0f;
        for (Element* child : flowChildren) {
            const bool fills = shareWidth ? child->Width().unit == LengthUnit::FILL : child->Height().unit == LengthUnit::FILL;
            if (fills) {
                ++fillCount;
                continue;
            }
            const Size size = child->Measure(measurer, innerWidth, innerLimit, childHeightDefinite);
            fixedMain += shareWidth ? size.width : size.height;
        }
        const float gapSpace = shareWidth ? columnSpace : rowSpace;
        const float gaps = flowChildren.size() > 1 ? gapSpace * static_cast<float>(flowChildren.size() - 1) : 0.0f;
        const float limit = shareWidth ? innerWidth : innerLimit;
        const float free = std::max(0.0f, limit - fixedMain - gaps);
        const float share = fillCount > 0 ? free / static_cast<float>(fillCount) : limit;
        for (Element* child : flowChildren) {
            const bool fills = shareWidth ? child->Width().unit == LengthUnit::FILL : child->Height().unit == LengthUnit::FILL;
            if (!fills) {
                continue;
            }
            if (shareWidth) {
                child->Measure(measurer, share, innerLimit, childHeightDefinite);
            } else {
                child->Measure(measurer, innerWidth, share, true);
            }
        }
    } else {
        for (Element* child : flowChildren) {
            const float slot = layout == FrameLayout::GRID ? cellWidth * static_cast<float>(std::max(1, child->ColumnSpan())) : innerWidth;
            child->Measure(measurer, slot, innerLimit, childHeightDefinite);
        }
    }
    for (Element* child : flowChildren) {
        const Size childSize = child->Measured();
        if (layout == FrameLayout::HORIZONTAL || (layout == FrameLayout::WRAP && wrap) || layout == FrameLayout::WRAP) {
            const float next = lineWidth == 0.0f ? childSize.width : lineWidth + columnSpace + childSize.width;
            if ((layout == FrameLayout::WRAP || wrap) && lineWidth > 0.0f && next > innerWidth && innerWidth > 0.0f) {
                flowWidth = std::max(flowWidth, lineWidth);
                flowHeight += lineHeight + rowSpace;
                lineWidth = childSize.width;
                lineHeight = childSize.height;
            } else {
                lineWidth = next;
                lineHeight = std::max(lineHeight, childSize.height);
            }
        } else if (layout == FrameLayout::VERTICAL) {
            if (flowCount > 0) {
                flowHeight += rowSpace;
            }
            flowHeight += childSize.height;
            flowWidth = std::max(flowWidth, childSize.width);
        } else if (layout == FrameLayout::GRID) {
            int span = std::min(columnCount, std::max(1, child->ColumnSpan()));
            if (columnIndex > 0 && columnIndex + span > columnCount) {
                flowHeight += lineHeight + rowSpace;
                lineHeight = 0.0f;
                columnIndex = 0;
            }
            lineHeight = std::max(lineHeight, childSize.height);
            columnIndex += span;
            flowWidth = innerWidth;
            if (columnIndex >= columnCount) {
                flowHeight += lineHeight;
                if (columnIndex > 0) {
                    flowHeight += 0.0f;
                }
                lineHeight = 0.0f;
                columnIndex = 0;
                flowHeight += rowSpace;
            }
        } else {
            flowWidth = std::max(flowWidth, childSize.width);
            flowHeight = std::max(flowHeight, childSize.height);
        }
        ++flowCount;
    }
    if (layout == FrameLayout::HORIZONTAL || layout == FrameLayout::WRAP) {
        flowWidth = std::max(flowWidth, lineWidth);
        flowHeight += lineHeight;
    } else if (layout == FrameLayout::GRID && columnIndex != 0) {
        flowHeight += lineHeight;
    }
    if (layout == FrameLayout::GRID && flowHeight > 0.0f && rowSpace > 0.0f && columnIndex == 0 && flowCount > columnCount) {
        flowHeight -= rowSpace;
    }

    float fitWidth = flowWidth + padX;
    float fitHeight = flowHeight + padY;
    if (autoSize && flowCount == 0) {
        if (widthLength.unit == LengthUnit::FIT && hugWidth.unit == LengthUnit::PIXELS) {
            fitWidth = hugWidth.value;
        }
        if (Height().unit == LengthUnit::FIT && hugHeight.unit == LengthUnit::PIXELS) {
            fitHeight = hugHeight.value;
        }
    }
    const float resolvedWidth = widthLength.unit == LengthUnit::FIT ? fitWidth : widthLength.Resolve(widthSpace, fitWidth);
    if (aspectRatio > 0.0f && (Height().unit == LengthUnit::FIT || heightMode == SizeMode::FIT)) {
        fitHeight = resolvedWidth / aspectRatio;
    }
    measured.width = widthLength.unit == LengthUnit::FIT ? fitWidth : widthLength.Resolve(widthSpace, fitWidth);
    measured.height = ResolveDefinite(heightLength, heightSpace, fitHeight, heightDefinite || heightMode == SizeMode::FILL || heightMode == SizeMode::PERCENT || heightMode == SizeMode::FIXED);
    contentWidth = fitWidth;
    contentHeight = fitHeight;
    const float maxX = std::max(0.0f, contentWidth - measured.width);
    const float maxY = std::max(0.0f, contentHeight - measured.height);
    scrollX = std::clamp(scrollX, 0.0f, maxX);
    scrollY = std::clamp(scrollY, 0.0f, maxY);
    scrollTargetX = std::clamp(scrollTargetX, 0.0f, maxX);
    scrollTargetY = std::clamp(scrollTargetY, 0.0f, maxY);
    return FinishMeasure(availableWidth, availableHeight);
}

void Frame::Arrange(const Rect& rect) {
    Rect placed = BorderBox(rect);
    if (positionMode == FramePosition::FIXED) {
        placed.x = OffsetX().Resolve(viewportWidth > 0.0f ? viewportWidth : placed.width, 0.0f, true);
        placed.y = OffsetY().Resolve(viewportHeight > 0.0f ? viewportHeight : placed.height, 0.0f, true);
    }
    placed.x += translateX;
    placed.y += translateY;
    bounds = placed;
    const float padLeft = padding.left.Resolve(bounds.width, 0.0f);
    const float padTop = padding.top.Resolve(bounds.width, 0.0f);
    const float padX = padding.SpanX(bounds.width);
    const float innerWidth = std::max(0.0f, bounds.width - padX);
    const float innerHeight = std::max(0.0f, bounds.height - padding.SpanY(bounds.width));
    const float columnSpace = GapOf(columnGap, gap, bounds.width);
    const float rowSpace = GapOf(rowGap, gap, bounds.width);
    const float originX = bounds.x + padLeft - (Scrolls() ? scrollX : 0.0f);
    const float originY = bounds.y + padTop - (Scrolls() ? scrollY : 0.0f);

    std::vector<Element*> flow;
    for (const std::unique_ptr<Element>& child : children) {
        if (!child->Visible() || LeavesFlow(*child)) {
            continue;
        }
        flow.push_back(child.get());
    }
    float contentMain = 0.0f;
    for (std::size_t index = 0; index < flow.size(); ++index) {
        if (index > 0 && (layout == FrameLayout::HORIZONTAL || layout == FrameLayout::VERTICAL)) {
            contentMain += layout == FrameLayout::HORIZONTAL ? columnSpace : rowSpace;
        }
        contentMain += layout == FrameLayout::VERTICAL ? flow[index]->Measured().height : flow[index]->Measured().width;
    }
    const float innerMain = layout == FrameLayout::VERTICAL ? std::max(0.0f, bounds.height - padding.SpanY(bounds.width)) : innerWidth;
    float cursor = layout == FrameLayout::VERTICAL ? originY : originX;
    float extra = 0.0f;
    float growSum = 0.0f;
    for (Element* child : flow) {
        growSum += child->Grow();
    }
    if (growSum <= 0.0f && justifyBetween && flow.size() > 1) {
        extra = std::max(0.0f, innerMain - contentMain) / static_cast<float>(flow.size() - 1);
    } else if (growSum <= 0.0f && justify == Alignment::CENTER) {
        cursor += std::max(0.0f, innerMain - contentMain) * 0.5f;
    } else if (growSum <= 0.0f && justify == Alignment::END) {
        cursor += std::max(0.0f, innerMain - contentMain);
    }
    const float free = std::max(0.0f, innerMain - contentMain);

    if ((layout == FrameLayout::HORIZONTAL && !wrap) || layout == FrameLayout::VERTICAL) {
        for (Element* child : flow) {
            float childWidth = child->Measured().width;
            float childHeight = child->Measured().height;
            if (child->Grow() > 0.0f && growSum > 0.0f) {
                if (layout == FrameLayout::HORIZONTAL) {
                    childWidth += free * (child->Grow() / growSum);
                } else {
                    childHeight += free * (child->Grow() / growSum);
                }
            }
            float cross = layout == FrameLayout::HORIZONTAL ? originY : originX;
            const float crossSize = layout == FrameLayout::HORIZONTAL ? childHeight : childWidth;
            const float crossLimit = layout == FrameLayout::HORIZONTAL ? std::max(0.0f, bounds.height - padding.SpanY(bounds.width)) : innerWidth;
            if (align == Alignment::STRETCH && !child->AutoSize()) {
                if (layout == FrameLayout::HORIZONTAL && child->Height().unit == LengthUnit::FIT) {
                    childHeight = crossLimit;
                } else if (layout == FrameLayout::VERTICAL && child->Width().unit == LengthUnit::FIT) {
                    childWidth = innerWidth;
                }
            } else if (align == Alignment::CENTER) {
                cross += (crossLimit - crossSize) * 0.5f;
            } else if (align == Alignment::END) {
                cross += crossLimit - crossSize;
            }
            if (layout == FrameLayout::HORIZONTAL) {
                child->Arrange(Rect{cursor, cross, childWidth, childHeight});
                cursor += childWidth + columnSpace + extra;
            } else {
                child->Arrange(Rect{cross, cursor, childWidth, childHeight});
                cursor += childHeight + rowSpace + extra;
            }
        }
    } else if (layout == FrameLayout::GRID) {
        const int columnCount = std::max(1, columns);
        const float cellWidth = columnCount <= 1 ? innerWidth : std::max(0.0f, (innerWidth - columnSpace * static_cast<float>(columnCount - 1)) / static_cast<float>(columnCount));
        int columnCursor = 0;
        float y = originY;
        float rowHeight = 0.0f;
        std::vector<Element*> rowItems;
        auto placeRow = [&]() {
            float x = originX;
            for (Element* child : rowItems) {
                const int span = std::min(columnCount, std::max(1, child->ColumnSpan()));
                child->Arrange(Rect{x, y, child->Measured().width, child->Measured().height});
                x += cellWidth * static_cast<float>(span) + columnSpace * static_cast<float>(span);
            }
            y += rowHeight + rowSpace;
            rowItems.clear();
            rowHeight = 0.0f;
            columnCursor = 0;
        };
        for (Element* child : flow) {
            const int span = std::min(columnCount, std::max(1, child->ColumnSpan()));
            if (columnCursor > 0 && columnCursor + span > columnCount) {
                placeRow();
            }
            rowItems.push_back(child);
            rowHeight = std::max(rowHeight, child->Measured().height);
            columnCursor += span;
            if (columnCursor >= columnCount) {
                placeRow();
            }
        }
        if (!rowItems.empty()) {
            placeRow();
        }
    } else if (layout == FrameLayout::WRAP || wrap) {
        float x = originX;
        float y = originY;
        float lineHeight = 0.0f;
        for (Element* child : flow) {
            const float childWidth = child->Measured().width;
            if (x > originX && x + childWidth > originX + innerWidth) {
                x = originX;
                y += lineHeight + rowSpace;
                lineHeight = 0.0f;
            }
            child->Arrange(Rect{x, y, childWidth, child->Measured().height});
            x += childWidth + columnSpace;
            lineHeight = std::max(lineHeight, child->Measured().height);
        }
    } else if (layout == FrameLayout::STACK) {
        for (Element* child : flow) {
            child->Arrange(Rect{originX, originY, child->Measured().width, child->Measured().height});
        }
    } else {
        for (Element* child : flow) {
            child->Arrange(Rect{originX + child->OffsetX().Resolve(innerWidth, 0.0f, true), originY + child->OffsetY().Resolve(innerHeight, 0.0f, true),
                                child->Measured().width, child->Measured().height});
        }
    }

    for (const std::unique_ptr<Element>& child : children) {
        if (!LeavesFlow(*child)) {
            continue;
        }
        Frame* frame = AsFrame(*child);
        float x = originX + child->OffsetX().Resolve(bounds.width, 0.0f, true);
        float y = originY + child->OffsetY().Resolve(innerHeight, 0.0f, true);
        const float childWidth = child->Measured().width;
        const float childHeight = child->Measured().height;
        if (frame != nullptr && frame->Anchor() == FrameAnchor::CENTER) {
            x = bounds.x + (bounds.width - childWidth) * 0.5f;
            y = bounds.y + (bounds.height - childHeight) * 0.5f;
        } else if (frame != nullptr && frame->Anchor() == FrameAnchor::RIGHT) {
            x = bounds.x + bounds.width - childWidth - padding.right.Resolve(bounds.width, 0.0f);
        } else if (frame != nullptr && frame->Anchor() == FrameAnchor::BOTTOM) {
            y = bounds.y + bounds.height - childHeight - padding.bottom.Resolve(bounds.width, 0.0f);
        } else if (frame != nullptr && frame->Anchor() == FrameAnchor::LEFT) {
            x = originX;
        } else if (frame != nullptr && frame->Anchor() == FrameAnchor::TOP) {
            y = bounds.y + padTop;
        }
        if (child->PositionMode() == FramePosition::FIXED) {
            x = child->OffsetX().Resolve(viewportWidth, 0.0f, true);
            y = child->OffsetY().Resolve(viewportHeight, 0.0f, true);
        }
        child->Arrange(Rect{x, y, childWidth, childHeight});
    }
    for (const std::unique_ptr<Element>& child : children) {
        const Rect natural = child->Bounds();
        const Rect pinned = child->PinnedRect(bounds, natural);
        if (pinned.x != natural.x || pinned.y != natural.y || pinned.width != natural.width || pinned.height != natural.height) {
            child->Arrange(pinned);
        }
    }
    PlaceScrollbars();
}

void Frame::PlaceOneScrollbar(Frame& track, bool horizontal, bool both) {
    const float inset = 4.0f;
    const float thickness = horizontal ? std::max(10.0f, track.Measured().height) : std::max(10.0f, track.Measured().width);
    const float endGap = both ? thickness + inset : inset;
    Rect trackRect{};
    if (horizontal) {
        trackRect = Rect{bounds.x + inset, bounds.y + bounds.height - thickness - inset, std::max(0.0f, bounds.width - inset - endGap), thickness};
    } else {
        trackRect = Rect{bounds.x + bounds.width - thickness - inset, bounds.y + inset, thickness, std::max(0.0f, bounds.height - inset - endGap)};
    }
    track.Arrange(trackRect);
    track.SetOpacity(scrollBarOpacity);
    track.SetVisible(scrollBarOpacity > 0.02f && (horizontal ? MaxScrollX() > 0.5f : MaxScrollY() > 0.5f || overflow == OverflowMode::SCROLL));
    if (scrollbarVisibility == "hidden") {
        track.SetVisible(false);
        track.SetOpacity(0.0f);
    } else if (scrollbarVisibility == "visible") {
        track.SetVisible(horizontal ? MaxScrollX() > 0.5f : MaxScrollY() > 0.5f || overflow == OverflowMode::SCROLL);
        track.SetOpacity(track.Visible() ? 1.0f : 0.0f);
    }
    Frame* decrement = DirectRole(track, "decrement");
    Frame* increment = DirectRole(track, "increment");
    Frame* thumb = DirectRole(track, "thumb");
    const float arrow = horizontal ? std::max(12.0f, thickness) : std::max(12.0f, thickness);
    float lead = 0.0f;
    float trail = 0.0f;
    if (decrement != nullptr) {
        lead = arrow;
        if (horizontal) {
            decrement->Arrange(Rect{trackRect.x, trackRect.y, lead, trackRect.height});
        } else {
            decrement->Arrange(Rect{trackRect.x, trackRect.y, trackRect.width, lead});
        }
        decrement->SetOpacity(track.Opacity());
        decrement->SetVisible(track.Visible());
    }
    if (increment != nullptr) {
        trail = arrow;
        if (horizontal) {
            increment->Arrange(Rect{trackRect.x + trackRect.width - trail, trackRect.y, trail, trackRect.height});
        } else {
            increment->Arrange(Rect{trackRect.x, trackRect.y + trackRect.height - trail, trackRect.width, trail});
        }
        increment->SetOpacity(track.Opacity());
        increment->SetVisible(track.Visible());
    }
    if (thumb == nullptr) {
        return;
    }
    if (horizontal) {
        const float rail = std::max(0.0f, trackRect.width - lead - trail);
        const float content = std::max(bounds.width, contentWidth);
        float thumbSize = rail * (bounds.width / content);
        thumbSize = std::clamp(thumbSize, std::min(28.0f, rail), rail);
        const float travel = std::max(0.0f, rail - thumbSize);
        const float maxScroll = MaxScrollX();
        const float x = trackRect.x + lead + (maxScroll <= 0.0f ? 0.0f : travel * (scrollX / maxScroll));
        thumb->Arrange(Rect{x, trackRect.y, thumbSize, trackRect.height});
    } else {
        const float rail = std::max(0.0f, trackRect.height - lead - trail);
        const float content = std::max(bounds.height, contentHeight);
        float thumbSize = rail * (bounds.height / content);
        thumbSize = std::clamp(thumbSize, std::min(28.0f, rail), rail);
        const float travel = std::max(0.0f, rail - thumbSize);
        const float maxScroll = MaxScrollY();
        const float y = trackRect.y + lead + (maxScroll <= 0.0f ? 0.0f : travel * (scrollY / maxScroll));
        thumb->Arrange(Rect{trackRect.x, y, trackRect.width, thumbSize});
    }
    thumb->SetOpacity(track.Opacity());
    thumb->SetVisible(track.Visible());
}

void Frame::PlaceScrollbars() {
    if (!Scrolls()) {
        return;
    }
    std::vector<Frame*> bars = DirectRoles(*this, "scrollbar");
    if (bars.empty()) {
        return;
    }
    Frame* vertical = nullptr;
    Frame* horizontal = nullptr;
    for (Frame* bar : bars) {
        if (bar->BarHorizontal()) {
            horizontal = bar;
        } else {
            vertical = bar;
        }
    }
    const bool both = vertical != nullptr && horizontal != nullptr;
    if (vertical != nullptr) {
        PlaceOneScrollbar(*vertical, false, both);
    }
    if (horizontal != nullptr) {
        PlaceOneScrollbar(*horizontal, true, both);
    }
}

void Frame::ApplyBound(const std::string& property, const std::string& value) {
    Element::ApplyBound(property, value);
    if (property == "background" || property == "backgroundColor") {
        if (const std::optional<Color> color = ParseColor(value)) {
            SetBackgroundColor(*color);
        }
    } else if (property == "cursor") {
        SetCursor(value);
    }
}

void Frame::PaintScrollArrow(Renderer& renderer) {
    if (role != "decrement" && role != "increment") {
        return;
    }
    Frame* track = Parent() != nullptr ? AsFrame(*Parent()) : nullptr;
    const bool horizontal = track != nullptr && track->BarHorizontal();
    const bool increase = role == "increment";
    TextStyle style;
    style.fontSize = std::max(9.0f, std::min(bounds.width, bounds.height) * 0.7f);
    style.fontWeight = 600;
    style.color = COLOR_TEXT_SECONDARY;
    style.horizontal = TextAlign::CENTER;
    style.vertical = TextAlign::CENTER;
    style.wrap = false;
    const char* glyph = horizontal ? (increase ? ">" : "<") : (increase ? "v" : "^");
    renderer.DrawString(glyph, bounds, style);
}

void Frame::OnPaint(Renderer& renderer) {
    if (display == FrameDisplay::NONE) {
        return;
    }
    const bool turned = std::fabs(rotationZ) > 0.01f;
    if (turned) {
        renderer.PushRotation(rotationZ, bounds.x + bounds.width * 0.5f, bounds.y + bounds.height * 0.5f);
    }
    const float radius = std::max(std::max(cornerRadius[0], cornerRadius[1]), std::max(cornerRadius[2], cornerRadius[3]));
    if (hasBackground && bounds.width > 0.0f && bounds.height > 0.0f) {
        renderer.FillRectangle(bounds, backgroundColor, radius);
    }
    if (borderStyle != "none") {
        const bool uniform = sideBorder[0] == sideBorder[1] && sideBorder[1] == sideBorder[2] && sideBorder[2] == sideBorder[3];
        if (uniform && sideBorder[0] <= 0.0f && borderWidth > 0.0f) {
            renderer.StrokeRectangle(bounds, borderColor, borderWidth, radius);
        } else if (uniform && sideBorder[0] > 0.0f) {
            renderer.StrokeRectangle(bounds, borderColor, sideBorder[0], radius);
        } else {
            if (sideBorder[0] > 0.0f) {
                renderer.FillRectangle(Rect{bounds.x, bounds.y, bounds.width, sideBorder[0]}, borderColor, 0.0f);
            }
            if (sideBorder[1] > 0.0f) {
                renderer.FillRectangle(Rect{bounds.x + bounds.width - sideBorder[1], bounds.y, sideBorder[1], bounds.height}, borderColor, 0.0f);
            }
            if (sideBorder[2] > 0.0f) {
                renderer.FillRectangle(Rect{bounds.x, bounds.y + bounds.height - sideBorder[2], bounds.width, sideBorder[2]}, borderColor, 0.0f);
            }
            if (sideBorder[3] > 0.0f) {
                renderer.FillRectangle(Rect{bounds.x, bounds.y, sideBorder[3], bounds.height}, borderColor, 0.0f);
            }
        }
    }
    PaintScrollArrow(renderer);
    const bool clipChildren = Clips() || !clipPath.empty();
    if (clipChildren) {
        renderer.PushClip(bounds);
    }
    for (Element* child : PaintOrder()) {
        child->Paint(renderer);
    }
    if (clipChildren) {
        renderer.PopClip();
    }
    if (Scrolls() && DirectRoles(*this, "scrollbar").empty() && scrollbarVisibility != "hidden" && scrollBarOpacity > 0.02f) {
        renderer.PushOpacity(scrollBarOpacity);
        if (MaxScrollY() > 0.5f) {
            const float thickness = 10.0f;
            const bool both = MaxScrollX() > 0.5f;
            const Rect track{bounds.x + bounds.width - thickness - 4.0f, bounds.y + 4.0f, thickness,
                             std::max(0.0f, bounds.height - 8.0f - (both ? thickness : 0.0f))};
            const float arrow = thickness;
            const float rail = std::max(0.0f, track.height - arrow * 2.0f);
            const float thumbHeight = std::clamp(rail * (bounds.height / std::max(bounds.height, contentHeight)), std::min(28.0f, rail), rail);
            const float travel = std::max(0.0f, rail - thumbHeight);
            const float y = track.y + arrow + (MaxScrollY() <= 0.0f ? 0.0f : travel * (scrollY / MaxScrollY()));
            renderer.FillRectangle(track, COLOR_BORDER_DEFAULT, 4.0f);
            renderer.FillRectangle(Rect{track.x, track.y, track.width, arrow}, COLOR_BG_TERTIARY, 4.0f);
            renderer.FillRectangle(Rect{track.x, track.y + track.height - arrow, track.width, arrow}, COLOR_BG_TERTIARY, 4.0f);
            renderer.FillRectangle(Rect{track.x, y, track.width, thumbHeight}, COLOR_TEXT_SECONDARY, 4.0f);
        }
        if (MaxScrollX() > 0.5f) {
            const float thickness = 10.0f;
            const bool both = MaxScrollY() > 0.5f;
            const Rect track{bounds.x + 4.0f, bounds.y + bounds.height - thickness - 4.0f, std::max(0.0f, bounds.width - 8.0f - (both ? thickness : 0.0f)),
                             thickness};
            const float arrow = thickness;
            const float rail = std::max(0.0f, track.width - arrow * 2.0f);
            const float thumbWidth = std::clamp(rail * (bounds.width / std::max(bounds.width, contentWidth)), std::min(28.0f, rail), rail);
            const float travel = std::max(0.0f, rail - thumbWidth);
            const float x = track.x + arrow + (MaxScrollX() <= 0.0f ? 0.0f : travel * (scrollX / MaxScrollX()));
            renderer.FillRectangle(track, COLOR_BORDER_DEFAULT, 4.0f);
            renderer.FillRectangle(Rect{track.x, track.y, arrow, track.height}, COLOR_BG_TERTIARY, 4.0f);
            renderer.FillRectangle(Rect{track.x + track.width - arrow, track.y, arrow, track.height}, COLOR_BG_TERTIARY, 4.0f);
            renderer.FillRectangle(Rect{x, track.y, thumbWidth, track.height}, COLOR_TEXT_SECONDARY, 4.0f);
        }
        renderer.PopOpacity();
    }
    if (turned) {
        renderer.PopTransform();
    }
}

Element* Frame::HitTest(Point point) {
    if (!visible || display == FrameDisplay::NONE || !ContainsPoint(bounds, point)) {
        return nullptr;
    }
    if (ScrollChrome() && opacity < 0.05f) {
        return nullptr;
    }
    const std::vector<Element*> order = PaintOrder();
    for (auto it = order.rbegin(); it != order.rend(); ++it) {
        if (Element* hit = (*it)->HitTest(point)) {
            return hit;
        }
    }
    return this;
}

bool Frame::OnWheel(int delta, bool shift) {
    if (!Scrolls()) {
        return false;
    }
    const float step = delta > 0 ? -scrollStep : scrollStep;
    const float beforeX = scrollX;
    const float beforeY = scrollY;
    if (shift || (MaxScrollY() <= 0.5f && MaxScrollX() > 0.5f)) {
        ScrollBy(step, 0.0f);
    } else {
        ScrollBy(0.0f, step);
        if (scrollY == beforeY && MaxScrollX() > 0.5f) {
            ScrollBy(step, 0.0f);
        }
    }
    return scrollX != beforeX || scrollY != beforeY || smoothScrolling;
}

}  // namespace Runtime
