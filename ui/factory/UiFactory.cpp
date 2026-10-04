#include "ui/factory/UiFactory.h"

#include "components/frame/Frame.h"
#include "components/layout/WindowElement.h"
#include "components/primitive/PrimitiveElement.h"

#include <algorithm>

namespace Runtime {
namespace {

float NumberOf(const IrNode& node, const char* name, float fallback) {
    const UiValue* value = FindProperty(node, name);
    return value == nullptr ? fallback : static_cast<float>(value->number);
}

SizeMode ParseSizeMode(const std::string& text) {
    if (text == "fixed") {
        return SizeMode::FIXED;
    }
    if (text == "fill") {
        return SizeMode::FILL;
    }
    if (text == "fit") {
        return SizeMode::FIT;
    }
    if (text == "percent") {
        return SizeMode::PERCENT;
    }
    return SizeMode::UNSET;
}

FramePosition ParsePosition(const std::string& text) {
    if (text == "absolute") {
        return FramePosition::ABSOLUTE;
    }
    if (text == "fixed") {
        return FramePosition::FIXED;
    }
    if (text == "sticky") {
        return FramePosition::STICKY;
    }
    return FramePosition::FLOW;
}

FrameLayout ParseLayout(const std::string& text) {
    if (text == "horizontal") {
        return FrameLayout::HORIZONTAL;
    }
    if (text == "vertical") {
        return FrameLayout::VERTICAL;
    }
    if (text == "grid") {
        return FrameLayout::GRID;
    }
    if (text == "wrap") {
        return FrameLayout::WRAP;
    }
    if (text == "stack") {
        return FrameLayout::STACK;
    }
    return FrameLayout::NONE;
}

FrameAnchor ParseAnchor(const std::string& text) {
    if (text == "top") {
        return FrameAnchor::TOP;
    }
    if (text == "right") {
        return FrameAnchor::RIGHT;
    }
    if (text == "bottom") {
        return FrameAnchor::BOTTOM;
    }
    if (text == "left") {
        return FrameAnchor::LEFT;
    }
    if (text == "center") {
        return FrameAnchor::CENTER;
    }
    return FrameAnchor::NONE;
}

OverflowMode ParseOverflow(const std::string& text) {
    if (text == "hidden") {
        return OverflowMode::HIDDEN;
    }
    if (text == "scroll") {
        return OverflowMode::SCROLL;
    }
    if (text == "auto") {
        return OverflowMode::AUTO;
    }
    return OverflowMode::VISIBLE;
}

FrameShadow ReadShadow(const IrNode& node) {
    FrameShadow shadow;
    if (const UiValue* color = FindProperty(node, "shadowColor")) {
        shadow.color = color->color;
    }
    shadow.x = NumberOf(node, "shadowX", 0.0f);
    shadow.y = NumberOf(node, "shadowY", 0.0f);
    shadow.blur = NumberOf(node, "shadowBlur", 0.0f);
    shadow.spread = NumberOf(node, "shadowSpread", 0.0f);
    return shadow;
}

void ApplyFrame(Frame& frame, const IrNode& node) {
    frame.SetId(node.id);
    if (const UiValue* value = FindProperty(node, "name")) frame.SetName(value->text);
    if (const UiValue* value = FindProperty(node, "class")) frame.SetClassName(value->text);
    if (const UiValue* value = FindProperty(node, "component")) frame.SetComponentName(value->text);
    if (const UiValue* value = FindProperty(node, "instance")) frame.SetInstanceName(value->text);
    if (const UiValue* value = FindProperty(node, "tag")) frame.SetTag(value->text);
    if (const UiValue* value = FindProperty(node, "role")) frame.SetRole(value->text);
    if (const UiValue* value = FindProperty(node, "width")) frame.SetWidth(value->length);
    if (const UiValue* value = FindProperty(node, "height")) frame.SetHeight(value->length);
    if (const UiValue* value = FindProperty(node, "minWidth")) frame.SetMinWidth(value->length);
    if (const UiValue* value = FindProperty(node, "maxWidth")) frame.SetMaxWidth(value->length);
    if (const UiValue* value = FindProperty(node, "minHeight")) frame.SetMinHeight(value->length);
    if (const UiValue* value = FindProperty(node, "maxHeight")) frame.SetMaxHeight(value->length);
    if (const UiValue* value = FindProperty(node, "widthMode")) frame.SetWidthMode(ParseSizeMode(value->text));
    if (const UiValue* value = FindProperty(node, "heightMode")) frame.SetHeightMode(ParseSizeMode(value->text));
    if (const UiValue* value = FindProperty(node, "aspectRatio")) frame.SetAspectRatio(static_cast<float>(value->number));
    if (const UiValue* value = FindProperty(node, "x")) frame.SetOffsetX(value->length);
    if (const UiValue* value = FindProperty(node, "y")) frame.SetOffsetY(value->length);
    if (const UiValue* value = FindProperty(node, "zIndex")) frame.SetZIndex(static_cast<int>(value->number));
    if (const UiValue* value = FindProperty(node, "position")) frame.SetPositionMode(ParsePosition(value->text));
    if (const UiValue* value = FindProperty(node, "anchor")) frame.SetAnchor(ParseAnchor(value->text));
    Edges padding{};
    bool hasPadding = false;
    if (const UiValue* value = FindProperty(node, "padding")) {
        padding = value->edges;
        hasPadding = true;
    }
    auto side = [&](const char* name, Length& edge) {
        if (const UiValue* value = FindProperty(node, name)) {
            edge = value->length;
            hasPadding = true;
        }
    };
    side("paddingTop", padding.top);
    side("paddingRight", padding.right);
    side("paddingBottom", padding.bottom);
    side("paddingLeft", padding.left);
    if (hasPadding) {
        frame.SetPadding(padding);
    }
    Edges margin{};
    bool hasMargin = false;
    if (const UiValue* value = FindProperty(node, "margin")) {
        margin = value->edges;
        hasMargin = true;
    }
    auto marginSide = [&](const char* name, Length& edge) {
        if (const UiValue* value = FindProperty(node, name)) {
            edge = value->length;
            hasMargin = true;
        }
    };
    marginSide("marginTop", margin.top);
    marginSide("marginRight", margin.right);
    marginSide("marginBottom", margin.bottom);
    marginSide("marginLeft", margin.left);
    if (hasMargin) {
        frame.SetMargin(margin);
    }
    if (const UiValue* value = FindProperty(node, "layout")) frame.SetLayout(ParseLayout(value->text));
    if (const UiValue* value = FindProperty(node, "grow")) frame.SetGrow(static_cast<float>(value->number));
    if (const UiValue* value = FindProperty(node, "shrink")) frame.SetShrink(static_cast<float>(value->number));
    if (const UiValue* value = FindProperty(node, "basis")) frame.SetBasis(value->length);
    if (const UiValue* value = FindProperty(node, "gap")) frame.SetGap(value->length);
    if (const UiValue* value = FindProperty(node, "rowGap")) frame.SetRowGap(value->length);
    if (const UiValue* value = FindProperty(node, "columnGap")) frame.SetColumnGap(value->length);
    if (const UiValue* value = FindProperty(node, "align")) frame.SetAlign(value->alignment);
    if (const UiValue* value = FindProperty(node, "justify")) {
        if (value->text == "spaceBetween") {
            frame.SetJustifyBetween(true);
        } else if (value->text == "center") {
            frame.SetJustify(Alignment::CENTER);
        } else if (value->text == "end" || value->text == "right" || value->text == "bottom") {
            frame.SetJustify(Alignment::END);
        } else if (value->text == "stretch") {
            frame.SetJustify(Alignment::STRETCH);
        }
    }
    if (const UiValue* value = FindProperty(node, "wrap")) frame.SetWrap(value->text == "wrap");
    if (const UiValue* value = FindProperty(node, "columns")) frame.SetColumns(static_cast<int>(value->number));
    if (const UiValue* value = FindProperty(node, "rows")) frame.SetRows(static_cast<int>(value->number));
    if (const UiValue* value = FindProperty(node, "column")) frame.SetColumn(static_cast<int>(value->number));
    if (const UiValue* value = FindProperty(node, "row")) frame.SetRow(static_cast<int>(value->number));
    if (const UiValue* value = FindProperty(node, "columnSpan")) frame.SetColumnSpan(static_cast<int>(value->number));
    if (const UiValue* value = FindProperty(node, "rowSpan")) frame.SetRowSpan(static_cast<int>(value->number));
    if (const UiValue* value = FindProperty(node, "background")) frame.SetBackgroundColor(value->color);
    if (const UiValue* value = FindProperty(node, "backgroundColor")) frame.SetBackgroundColor(value->color);
    if (const UiValue* value = FindProperty(node, "gradient")) frame.SetGradient(value->text);
    if (const UiValue* value = FindProperty(node, "image")) frame.SetBackgroundImage(value->text);
    if (const UiValue* value = FindProperty(node, "opacity")) frame.SetOpacity(static_cast<float>(value->number));
    if (const UiValue* width = FindProperty(node, "borderWidth")) {
        Color color = COLOR_BORDER_DEFAULT;
        if (const UiValue* borderColor = FindProperty(node, "borderColor")) {
            color = borderColor->color;
        }
        frame.SetBorder(width->length.value, color);
    } else if (const UiValue* borderColor = FindProperty(node, "borderColor")) {
        frame.SetBorder(1.0f, borderColor->color);
    }
    if (const UiValue* value = FindProperty(node, "borderStyle")) frame.SetBorderStyle(value->text);
    if (const UiValue* value = FindProperty(node, "borderTop")) frame.SetSideBorder(0, value->length.value);
    if (const UiValue* value = FindProperty(node, "borderRight")) frame.SetSideBorder(1, value->length.value);
    if (const UiValue* value = FindProperty(node, "borderBottom")) frame.SetSideBorder(2, value->length.value);
    if (const UiValue* value = FindProperty(node, "borderLeft")) frame.SetSideBorder(3, value->length.value);
    if (const UiValue* value = FindProperty(node, "radius")) frame.SetRadius(value->length.value);
    if (const UiValue* value = FindProperty(node, "radiusTopLeft")) frame.SetCornerRadius(0, value->length.value);
    if (const UiValue* value = FindProperty(node, "radiusTopRight")) frame.SetCornerRadius(1, value->length.value);
    if (const UiValue* value = FindProperty(node, "radiusBottomRight")) frame.SetCornerRadius(2, value->length.value);
    if (const UiValue* value = FindProperty(node, "radiusBottomLeft")) frame.SetCornerRadius(3, value->length.value);
    if (FindProperty(node, "shadow") != nullptr || FindProperty(node, "shadowColor") != nullptr || FindProperty(node, "shadowX") != nullptr) {
        frame.SetShadow(ReadShadow(node));
    }
    if (const UiValue* value = FindProperty(node, "overflow")) frame.SetOverflow(ParseOverflow(value->text));
    if (const UiValue* value = FindProperty(node, "clip")) frame.SetClip(value->boolean);
    if (const UiValue* value = FindProperty(node, "clipRadius")) frame.SetClipRadius(value->length.value);
    if (const UiValue* value = FindProperty(node, "clipPath")) frame.SetClipPath(value->text);
    frame.SetTranslate(NumberOf(node, "translateX", 0.0f), NumberOf(node, "translateY", 0.0f));
    const float scale = NumberOf(node, "scale", 1.0f);
    frame.SetScale(NumberOf(node, "scaleX", scale), NumberOf(node, "scaleY", scale));
    const float rotation = NumberOf(node, "rotation", 0.0f);
    frame.SetRotation(NumberOf(node, "rotationX", 0.0f), NumberOf(node, "rotationY", 0.0f), NumberOf(node, "rotationZ", rotation));
    frame.SetSkew(NumberOf(node, "skewX", 0.0f), NumberOf(node, "skewY", 0.0f));
    if (const UiValue* value = FindProperty(node, "transformOrigin")) frame.SetTransformOrigin(value->text);
    if (const UiValue* value = FindProperty(node, "visible")) frame.SetVisible(value->boolean);
    if (const UiValue* value = FindProperty(node, "visibility")) frame.SetVisible(value->text != "hidden");
    if (const UiValue* value = FindProperty(node, "display")) {
        if (value->text == "none") {
            frame.SetDisplay(FrameDisplay::NONE);
        }
    }
    if (const UiValue* value = FindProperty(node, "blendMode")) frame.SetBlendMode(value->text);
    frame.SetEffects(NumberOf(node, "blur", 0.0f), NumberOf(node, "backdropBlur", 0.0f), NumberOf(node, "brightness", 1.0f),
                     NumberOf(node, "contrast", 1.0f), NumberOf(node, "saturation", 1.0f), NumberOf(node, "grayscale", 0.0f),
                     NumberOf(node, "hueRotate", 0.0f));
    if (const UiValue* value = FindProperty(node, "cursor")) frame.SetCursor(value->text);
    if (const UiValue* value = FindProperty(node, "opacity")) frame.SetOpacity(static_cast<float>(value->number));
}

void ApplyCommon(Element& element, const IrNode& node) {
    if (!node.id.empty()) {
        element.SetId(node.id);
    }
    if (const UiValue* value = FindProperty(node, "x")) {
        element.SetOffsetX(value->length);
    }
    if (const UiValue* value = FindProperty(node, "y")) {
        element.SetOffsetY(value->length);
    }
    if (const UiValue* value = FindProperty(node, "width")) {
        element.SetWidth(value->length);
    }
    if (const UiValue* value = FindProperty(node, "height")) {
        element.SetHeight(value->length);
    }
    if (const UiValue* value = FindProperty(node, "opacity")) {
        element.SetOpacity(static_cast<float>(value->number));
    }
    if (const UiValue* value = FindProperty(node, "visible")) {
        element.SetVisible(value->boolean);
    }
    if (const UiValue* value = FindProperty(node, "position")) {
        element.SetPositionMode(ParsePosition(value->text));
    }
}

void ApplyShape(ShapeElement& shape, const IrNode& node) {
    ApplyCommon(shape, node);
    if (const UiValue* value = FindProperty(node, "text")) {
        shape.SetText(value->text);
    }
    if (const UiValue* value = FindProperty(node, "name")) {
        if (shape.Kind() == NodeKind::SLOT || shape.Kind() == NodeKind::COMPONENT) {
            shape.SetText(value->text);
        }
    }
    if (const UiValue* value = FindProperty(node, "source")) {
        shape.SetSource(value->text);
    }
    if (const UiValue* value = FindProperty(node, "d")) {
        shape.SetPathData(value->text);
    }
    if (const UiValue* value = FindProperty(node, "shape")) {
        shape.SetShapeName(value->text);
    }
    if (const UiValue* value = FindProperty(node, "fill")) {
        shape.SetFill(value->color);
    }
    Color stroke{0.114f, 0.114f, 0.122f, 1.0f};
    float strokeWidth = shape.Kind() == NodeKind::LINE || shape.Kind() == NodeKind::PATH ? 2.0f : 0.0f;
    if (const UiValue* value = FindProperty(node, "stroke")) {
        stroke = value->color;
        if (strokeWidth <= 0.0f) {
            strokeWidth = 1.0f;
        }
    }
    if (const UiValue* value = FindProperty(node, "strokeWidth")) {
        strokeWidth = static_cast<float>(value->number);
    }
    shape.SetStroke(stroke, strokeWidth);
    if (const UiValue* value = FindProperty(node, "radius")) {
        shape.SetCornerRadius(value->length.value);
    }
    Color textColor{0.094f, 0.094f, 0.102f, 1.0f};
    if (const UiValue* value = FindProperty(node, "textColor")) {
        textColor = value->color;
    }
    TextAlign align = TextAlign::START;
    if (const UiValue* value = FindProperty(node, "textAlign")) {
        if (value->text == "center") {
            align = TextAlign::CENTER;
        } else if (value->text == "right" || value->text == "end") {
            align = TextAlign::END;
        }
    }
    shape.SetFont(NumberOf(node, "fontSize", 16.0f), static_cast<int>(NumberOf(node, "fontWeight", 400.0f)), textColor, align);
    shape.SetLine(NumberOf(node, "x1", 0.0f), NumberOf(node, "y1", 0.0f), NumberOf(node, "x2", 80.0f), NumberOf(node, "y2", 0.0f));
}

const IrNode* FindNamedComponent(const IrNode& node, std::string_view name) {
    if (node.kind == NodeKind::COMPONENT) {
        if (const UiValue* value = FindProperty(node, "name")) {
            if (value->text == name) {
                return &node;
            }
        }
    }
    for (const IrNode& child : node.children) {
        if (const IrNode* found = FindNamedComponent(child, name)) {
            return found;
        }
    }
    return nullptr;
}

std::unique_ptr<Element> CreateElement(const IrNode& node) {
    switch (node.kind) {
    case NodeKind::WINDOW:
        return std::make_unique<WindowElement>();
    case NodeKind::FRAME:
        return std::make_unique<Frame>();
    case NodeKind::SHADOW:
        return nullptr;
    case NodeKind::TEXT:
    case NodeKind::IMAGE:
    case NodeKind::VECTOR:
    case NodeKind::PATH:
    case NodeKind::LINE:
    case NodeKind::GROUP:
    case NodeKind::COMPONENT:
    case NodeKind::INSTANCE:
    case NodeKind::SLOT:
        return std::make_unique<ShapeElement>(node.kind);
    case NodeKind::SCROLL:
        return std::make_unique<ScrollElement>();
    }
    return nullptr;
}

void Populate(Element& element, const IrNode& node, const std::unordered_map<std::string, std::function<void()>>& clicks, const IrNode& root) {
    (void)clicks;
    if (Frame* frame = dynamic_cast<Frame*>(&element)) {
        ApplyFrame(*frame, node);
        ApplyCommon(*frame, node);
        if (node.kind == NodeKind::SCROLL) {
            frame->SetOverflow(OverflowMode::SCROLL);
        }
    } else if (ShapeElement* shape = dynamic_cast<ShapeElement*>(&element)) {
        ApplyShape(*shape, node);
    } else {
        ApplyCommon(element, node);
    }
    const IrNode* definition = nullptr;
    if (node.kind == NodeKind::INSTANCE && node.children.empty()) {
        if (const UiValue* value = FindProperty(node, "component")) {
            definition = FindNamedComponent(root, value->text);
        }
    }
    const std::vector<IrNode>& children = definition == nullptr ? node.children : definition->children;
    for (const IrNode& child : children) {
        if (child.kind == NodeKind::SHADOW) {
            if (Frame* frame = dynamic_cast<Frame*>(&element)) {
                frame->AddShadow(ReadShadow(child));
            }
            continue;
        }
        std::unique_ptr<Element> created = CreateElement(child);
        if (created == nullptr) {
            continue;
        }
        Populate(*created, child, clicks, root);
        element.AddChild(std::move(created));
    }
}

}  // namespace

std::unique_ptr<Element> Instantiate(const IrNode& root, const std::unordered_map<std::string, std::function<void()>>& clicks) {
    std::unique_ptr<Element> created = CreateElement(root);
    if (created == nullptr) {
        return nullptr;
    }
    Populate(*created, root, clicks, root);
    if (!root.states.empty()) {
        created->SeedState(root.states);
    } else {
        created->ApplyState();
    }
    return created;
}

}  // namespace Runtime
