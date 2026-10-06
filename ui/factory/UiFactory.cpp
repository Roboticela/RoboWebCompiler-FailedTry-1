#include "ui/factory/UiFactory.h"

#include "components/frame/Frame.h"
#include "components/layout/BoardElement.h"
#include "components/layout/WindowElement.h"
#include "components/primitive/PrimitiveElement.h"
#include "components/text/TextElement.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace Runtime {
namespace {

std::string FoldToken(const std::string& text) {
    std::string folded;
    for (unsigned char character : text) {
        if (character == '_' || character == '-' || character == ' ') {
            continue;
        }
        folded.push_back(static_cast<char>(std::tolower(character)));
    }
    return folded;
}

float NumberOf(const IrNode& node, const char* name, float fallback) {
    const UiValue* value = FindProperty(node, name);
    return value == nullptr ? fallback : static_cast<float>(value->number);
}

SizeMode ParseSizeMode(const std::string& text) {
    const std::string folded = FoldToken(text);
    if (folded == "fixed") {
        return SizeMode::FIXED;
    }
    if (folded == "fill") {
        return SizeMode::FILL;
    }
    if (folded == "fit" || folded == "hug") {
        return SizeMode::FIT;
    }
    if (folded == "percent") {
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
    if (text == "inline") {
        return FrameLayout::HORIZONTAL;
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
    bool hug = true;
    if (const UiValue* value = FindProperty(node, "auto")) {
        hug = value->kind == TypeKind::BOOL ? value->boolean : value->text == "true";
    } else {
        const UiValue* width = FindProperty(node, "width");
        const UiValue* height = FindProperty(node, "height");
        const bool fixedWidth = width != nullptr && width->length.unit != LengthUnit::FIT;
        const bool fixedHeight = height != nullptr && height->length.unit != LengthUnit::FIT;
        hug = !fixedWidth && !fixedHeight;
    }
    frame.SetAutoSize(hug);
    if (hug && node.name != "button" && node.name != "iconButton" && node.name != "switch" && node.name != "segmentedControl" &&
        node.name != "segment" && node.name != "confirmAction") {
        frame.SetHugFallback(frame.Width(), frame.Height());
        frame.SetWidth(Length::Fit());
        frame.SetHeight(Length::Fit());
    }
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
    if (const UiValue* value = FindProperty(node, "direction")) {
        const std::string text = value->text == "row" || value->text == "horizontal" ? "horizontal" : "vertical";
        frame.SetLayout(ParseLayout(text));
    }
    if (const UiValue* value = FindProperty(node, "alignment")) {
        frame.SetAlign(value->text == "center" ? Alignment::CENTER : value->text == "end" || value->text == "right" ? Alignment::END
                                                                 : value->text == "stretch" ? Alignment::STRETCH
                                                                                            : Alignment::START);
    }
    if (const UiValue* value = FindProperty(node, "alignItems")) {
        const std::string text = FoldToken(value->text);
        frame.SetAlign(text == "center" ? Alignment::CENTER : text == "end" ? Alignment::END : text == "stretch" ? Alignment::STRETCH : Alignment::START);
    }
    if (const UiValue* value = FindProperty(node, "distribution")) {
        if (value->text == "spaceBetween") {
            frame.SetJustifyBetween(true);
        } else if (value->text == "center") {
            frame.SetJustify(Alignment::CENTER);
        } else if (value->text == "end") {
            frame.SetJustify(Alignment::END);
        }
    }
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
    if (const UiValue* value = FindProperty(node, "justifyContent")) {
        const std::string text = FoldToken(value->text);
        if (text == "spacebetween") {
            frame.SetJustifyBetween(true);
        } else if (text == "center") {
            frame.SetJustify(Alignment::CENTER);
            frame.SetJustifyBetween(false);
        } else if (text == "end") {
            frame.SetJustify(Alignment::END);
            frame.SetJustifyBetween(false);
        } else {
            frame.SetJustify(Alignment::START);
            frame.SetJustifyBetween(false);
        }
    }
    if (const UiValue* value = FindProperty(node, "wrap")) frame.SetWrap(value->text == "wrap" || value->text == "true");
    if (const UiValue* value = FindProperty(node, "columns")) frame.SetColumns(static_cast<int>(value->number));
    if (const UiValue* value = FindProperty(node, "rows")) frame.SetRows(static_cast<int>(value->number));
    if (const UiValue* value = FindProperty(node, "column")) frame.SetColumn(static_cast<int>(value->number));
    if (const UiValue* value = FindProperty(node, "row")) frame.SetRow(static_cast<int>(value->number));
    if (const UiValue* value = FindProperty(node, "columnSpan")) frame.SetColumnSpan(static_cast<int>(value->number));
    if (const UiValue* value = FindProperty(node, "rowSpan")) frame.SetRowSpan(static_cast<int>(value->number));
    if (const UiValue* value = FindProperty(node, "background")) frame.SetBackgroundColor(value->color);
    if (const UiValue* value = FindProperty(node, "backgroundColor")) frame.SetBackgroundColor(value->color);
    if (const UiValue* value = FindProperty(node, "fill")) frame.SetBackgroundColor(value->color);
    if (const UiValue* value = FindProperty(node, "gradient")) frame.SetGradient(value->text);
    if (const UiValue* value = FindProperty(node, "image")) frame.SetBackgroundImage(value->text);
    if (const UiValue* value = FindProperty(node, "opacity")) frame.SetOpacity(static_cast<float>(value->number));
    if (const UiValue* stroke = FindProperty(node, "stroke")) {
        const float width = FindProperty(node, "strokeWidth") != nullptr ? FindProperty(node, "strokeWidth")->length.value : 1.0f;
        frame.SetBorder(width, stroke->color);
    }
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
    if (const UiValue* value = FindProperty(node, "scrollbarVisibility")) {
        frame.SetScrollbarVisibility(value->text);
    } else if (const UiValue* value = FindProperty(node, "scrollbar")) {
        frame.SetScrollbarVisibility(value->text);
    }
    if (const UiValue* value = FindProperty(node, "smoothScrolling")) {
        frame.SetSmoothScrolling(value->kind == TypeKind::BOOL ? value->boolean : value->text == "true");
    }
    if (const UiValue* value = FindProperty(node, "scrollStep")) {
        frame.SetScrollStep(static_cast<float>(value->number));
    }
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
    const auto pin = [&](const char* name, BoxEdge edge) {
        const UiValue* value = FindProperty(node, name);
        if (value == nullptr || value->text.empty()) {
            return;
        }
        const std::string text = value->text;
        float offset = 0.0f;
        const std::size_t plus = text.find_first_of("+-");
        const bool parent = text.find("parent.") != std::string::npos || plus == std::string::npos;
        if (plus != std::string::npos && plus > 0) {
            offset = std::strtof(text.c_str() + plus, nullptr);
        } else if (text.find("parent.") == std::string::npos) {
            offset = std::strtof(text.c_str(), nullptr);
        }
        if (parent) {
            element.SetEdgePin(edge, offset);
        }
    };
    pin("left", BoxEdge::LEFT);
    pin("right", BoxEdge::RIGHT);
    pin("top", BoxEdge::TOP);
    pin("bottom", BoxEdge::BOTTOM);
    pin("centerX", BoxEdge::CENTER_X);
    pin("centerY", BoxEdge::CENTER_Y);
    const auto pixelOf = [&](const char* name, float fallback) {
        const UiValue* value = FindProperty(node, name);
        if (value == nullptr || value->length.unit != LengthUnit::PIXELS) {
            return fallback;
        }
        return value->length.value;
    };
    if (const UiValue* value = FindProperty(node, "horizontal")) {
        const std::string text = FoldToken(value->text);
        const float inset = pixelOf("x", 16.0f);
        if (text == "left") {
            element.SetEdgePin(BoxEdge::LEFT, pixelOf("x", 0.0f));
        } else if (text == "right") {
            element.SetEdgePin(BoxEdge::RIGHT, inset);
        } else if (text == "leftright") {
            element.SetEdgePin(BoxEdge::LEFT, inset);
            element.SetEdgePin(BoxEdge::RIGHT, inset);
        } else if (text == "center") {
            element.SetEdgePin(BoxEdge::CENTER_X, pixelOf("x", 0.0f));
        }
    }
    if (const UiValue* value = FindProperty(node, "vertical")) {
        const std::string text = FoldToken(value->text);
        const float inset = pixelOf("y", 16.0f);
        if (text == "top") {
            element.SetEdgePin(BoxEdge::TOP, pixelOf("y", 0.0f));
        } else if (text == "bottom") {
            element.SetEdgePin(BoxEdge::BOTTOM, inset);
        } else if (text == "topbottom") {
            element.SetEdgePin(BoxEdge::TOP, inset);
            element.SetEdgePin(BoxEdge::BOTTOM, inset);
        } else if (text == "center") {
            element.SetEdgePin(BoxEdge::CENTER_Y, pixelOf("y", 0.0f));
        }
    }
    if (const UiValue* value = FindProperty(node, "minScreen")) {
        element.SetHiddenBelow(static_cast<float>(value->number));
    }
    if (const UiValue* value = FindProperty(node, "maxScreen")) {
        element.SetMaxScreen(static_cast<float>(value->number));
    }
    if (const UiValue* value = FindProperty(node, "orientation")) {
        element.SetOrientation(value->text);
    }
    if (const UiValue* value = FindProperty(node, "minContainer")) {
        element.SetMinContainer(static_cast<float>(value->number));
    }
    for (const auto& property : node.properties) {
        if (!property.second.binding.empty()) {
            element.AddBinding(property.first, property.second.binding, property.second.bindMode, property.second.invert);
        }
    }
    if (node.visual.used || !node.visual.motions.empty()) {
        element.SetVisualStyle(node.visual);
        element.SetMotions(node.visual.motions);
    }
    if (const UiValue* value = FindProperty(node, "name")) {
        element.SetAccessibleName(value->text);
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
    if (const UiValue* value = FindProperty(node, "fit")) {
        shape.SetImageFit(value->text);
    }
    if (const UiValue* value = FindProperty(node, "clip")) {
        shape.SetImageClip(value->boolean);
    }
    if (const UiValue* value = FindProperty(node, "d")) {
        shape.SetPathData(value->text);
    }
    if (const UiValue* value = FindProperty(node, "viewBox")) {
        const char* text = value->text.c_str();
        char* end = nullptr;
        const float boxX = std::strtof(text, &end);
        if (end != text) {
            const char* next = end;
            const float boxY = std::strtof(next, &end);
            if (end != next) {
                next = end;
                const float boxWidth = std::strtof(next, &end);
                if (end != next) {
                    next = end;
                    const float boxHeight = std::strtof(next, &end);
                    if (end != next && boxWidth > 0.0f && boxHeight > 0.0f) {
                        shape.SetViewBox(boxX, boxY, boxWidth, boxHeight);
                    }
                }
            }
        }
    }
    std::string cap;
    std::string join;
    bool evenOdd = false;
    if (const UiValue* value = FindProperty(node, "lineCap")) {
        cap = value->text;
    }
    if (const UiValue* value = FindProperty(node, "lineJoin")) {
        join = value->text;
    }
    if (const UiValue* value = FindProperty(node, "fillRule")) {
        evenOdd = value->text == "evenodd";
    }
    if (!cap.empty() || !join.empty() || evenOdd) {
        shape.SetLineStyle(cap, join, evenOdd);
    }
    if (const UiValue* value = FindProperty(node, "shape")) {
        shape.SetShapeName(value->text);
    }
    if (const UiValue* value = FindProperty(node, "fill")) {
        shape.SetFill(value->color);
    }
    Color stroke{0.114f, 0.114f, 0.122f, 1.0f};
    float strokeWidth = 0.0f;
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
        } else         if (value->text == "right" || value->text == "end") {
            align = TextAlign::END;
        } else if (value->text == "justify") {
            align = TextAlign::JUSTIFY;
        }
    }
    shape.SetFont(NumberOf(node, "fontSize", 16.0f), static_cast<int>(NumberOf(node, "fontWeight", 400.0f)), textColor, align);
    std::string family;
    std::string locale;
    if (const UiValue* value = FindProperty(node, "fontFamily")) {
        family = value->text;
    }
    if (const UiValue* value = FindProperty(node, "language")) {
        locale = value->text;
    }
    const bool italic = FindProperty(node, "fontStyle") != nullptr && FindProperty(node, "fontStyle")->text == "italic";
    const bool rtl = FindProperty(node, "direction") != nullptr && FindProperty(node, "direction")->text == "rtl";
    shape.SetTextStyle(std::move(family), italic, rtl, std::move(locale));
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

bool FlagValue(const UiValue* value) {
    if (value == nullptr) {
        return false;
    }
    if (value->kind == TypeKind::BOOL) {
        return value->boolean;
    }
    return value->text == "true";
}

Color ToneColor(const std::string& tone, Color fallback) {
    if (tone == "primary") {
        return Color{0x1D / 255.0f, 0x1D / 255.0f, 0x1F / 255.0f, 1.0f};
    }
    if (tone == "secondary") {
        return Color{0x6E / 255.0f, 0x6E / 255.0f, 0x73 / 255.0f, 1.0f};
    }
    if (tone == "tertiary") {
        return Color{0x86 / 255.0f, 0x86 / 255.0f, 0x8B / 255.0f, 1.0f};
    }
    if (tone == "disabled") {
        return Color{0xC7 / 255.0f, 0xC7 / 255.0f, 0xCC / 255.0f, 1.0f};
    }
    if (tone == "link") {
        return Color{0x0E / 255.0f, 0x7A / 255.0f, 0xFF / 255.0f, 1.0f};
    }
    if (tone == "danger") {
        return Color{0xFF / 255.0f, 0x3B / 255.0f, 0x30 / 255.0f, 1.0f};
    }
    if (tone == "success") {
        return Color{0x30 / 255.0f, 0xD1 / 255.0f, 0x58 / 255.0f, 1.0f};
    }
    if (tone == "warning") {
        return Color{0xFF / 255.0f, 0x9F / 255.0f, 0x0A / 255.0f, 1.0f};
    }
    if (tone == "on-brand") {
        return Color{1.0f, 1.0f, 1.0f, 1.0f};
    }
    return fallback;
}

std::string RoleFor(const IrNode& node) {
    std::string role = "content";
    if (node.name == "heading") {
        role = "h1";
    } else if (node.name == "display") {
        role = "display";
    } else if (node.name == "subhead") {
        role = "subhead";
    } else if (node.name == "caption") {
        role = "caption";
    } else if (node.name == "eyebrow") {
        role = "eyebrow";
    } else if (node.name == "label") {
        role = "label";
    } else if (node.name == "body" || node.name == "content" || node.name == "text") {
        role = "content";
    }
    if (const UiValue* value = FindProperty(node, "variant")) {
        role = value->text;
    }
    if (const UiValue* value = FindProperty(node, "level")) {
        if (value->text == "display") {
            role = "display";
        } else if (value->text == "2") {
            role = "h2";
        } else if (value->text == "3") {
            role = "h3";
        } else if (value->text == "1") {
            role = "h1";
        }
    }
    if (const UiValue* value = FindProperty(node, "size")) {
        if (value->text == "sm" && (role == "content" || node.name == "body" || node.name == "content")) {
            role = "content-sm";
        }
    }
    return role;
}

void ApplyTypography(TextElement& text, const IrNode& node) {
    text.SetRole(RoleFor(node));
    std::string content;
    if (const UiValue* value = FindProperty(node, "text")) {
        content = value->text;
    }
    if (const UiValue* value = FindProperty(node, "value")) {
        content = value->text;
    }
    text.SetText(std::move(content));
    const bool sizeSpecified = FindProperty(node, "fontSize") != nullptr;
    const bool weightSpecified = FindProperty(node, "fontWeight") != nullptr;
    const bool colorSpecified = FindProperty(node, "textColor") != nullptr || FindProperty(node, "tone") != nullptr;
    TextAlign align = TextAlign::START;
    if (const UiValue* value = FindProperty(node, "textAlign")) {
        if (value->text == "center") {
            align = TextAlign::CENTER;
        } else if (value->text == "right" || value->text == "end") {
            align = TextAlign::END;
        } else if (value->text == "justify") {
            align = TextAlign::JUSTIFY;
        }
    }
    Color color = text.FontSize() >= 0.0f ? Color{0x1D / 255.0f, 0x1D / 255.0f, 0x1F / 255.0f, 1.0f} : Color{};
    if (const UiValue* value = FindProperty(node, "tone")) {
        color = ToneColor(value->text, color);
    }
    if (const UiValue* value = FindProperty(node, "textColor")) {
        color = value->color;
    }
    const float size = sizeSpecified ? NumberOf(node, "fontSize", 19.0f) : 19.0f;
    const int weight = weightSpecified ? static_cast<int>(NumberOf(node, "fontWeight", 400.0f)) : 400;
    text.SetFont(size, weight, color, align);
    text.SetFontSpecified(sizeSpecified, weightSpecified, colorSpecified);
    if (!sizeSpecified || !weightSpecified || !colorSpecified) {
        text.SetRole(RoleFor(node));
    }
    if (const UiValue* value = FindProperty(node, "letterSpacing")) {
        text.SetTracking(static_cast<float>(value->number), true);
    }
    if (const UiValue* value = FindProperty(node, "lineHeight")) {
        text.SetLeading(static_cast<float>(value->number), true);
    }
    if (const UiValue* value = FindProperty(node, "wordSpacing")) {
        text.SetWordSpacing(static_cast<float>(value->number));
    }
    std::string family = "Inter";
    std::string file;
    std::string locale;
    if (const UiValue* value = FindProperty(node, "fontFamily")) {
        family = value->text;
    }
    if (const UiValue* value = FindProperty(node, "fontFile")) {
        file = value->text;
    }
    if (const UiValue* value = FindProperty(node, "language")) {
        locale = value->text;
    }
    const bool italic = FindProperty(node, "fontStyle") != nullptr &&
                        (FindProperty(node, "fontStyle")->text == "italic" || FindProperty(node, "fontStyle")->text == "oblique");
    const bool rtl = FindProperty(node, "direction") != nullptr && FindProperty(node, "direction")->text == "rtl";
    text.SetTextStyle(std::move(family), std::move(file), italic, rtl, std::move(locale));
    if (FindProperty(node, "layout") == nullptr && FindProperty(node, "direction") != nullptr) {
        const std::string direction = FindProperty(node, "direction")->text;
        if (direction == "ltr" || direction == "rtl") {
            text.SetLayout(FrameLayout::NONE);
        }
    }
    bool wrap = true;
    if (const UiValue* value = FindProperty(node, "wrap")) {
        wrap = value->text != "nowrap" && value->text != "false";
        text.SetWrap(false);
    }
    text.SetWrapText(wrap);
    if (const UiValue* value = FindProperty(node, "maxLines")) {
        text.SetMaxLines(static_cast<int>(value->number));
    }
    if (const UiValue* value = FindProperty(node, "ellipsis")) {
        text.SetEllipsis(FlagValue(value));
    }
    if (const UiValue* value = FindProperty(node, "verticalAlign")) {
        TextAlign vertical = TextAlign::START;
        if (value->text == "center") {
            vertical = TextAlign::CENTER;
        } else if (value->text == "bottom" || value->text == "end") {
            vertical = TextAlign::END;
        }
        text.SetVerticalAlign(vertical);
    }
    if (const UiValue* value = FindProperty(node, "textTransform")) {
        text.SetTransform(value->text, true);
    }
    bool underline = false;
    bool strike = false;
    bool overline = false;
    if (const UiValue* value = FindProperty(node, "underline")) {
        underline = FlagValue(value);
    }
    if (const UiValue* value = FindProperty(node, "lineThrough")) {
        strike = FlagValue(value);
    }
    if (const UiValue* value = FindProperty(node, "overline")) {
        overline = FlagValue(value);
    }
    Color decoration = color;
    if (const UiValue* value = FindProperty(node, "decorationColor")) {
        decoration = value->color;
    }
    float decorationWidth = 1.0f;
    if (const UiValue* value = FindProperty(node, "decorationWidth")) {
        decorationWidth = static_cast<float>(value->number);
    }
    std::string decorationStyle;
    if (const UiValue* value = FindProperty(node, "decorationStyle")) {
        decorationStyle = value->text;
    }
    text.SetDecoration(underline, strike, overline, decoration, decorationWidth, decorationStyle);
    if (const UiValue* value = FindProperty(node, "fontFeatures")) {
        text.SetFontFeatures(value->text);
    }
    if (const UiValue* value = FindProperty(node, "writingMode")) {
        text.SetWritingMode(value->text);
    }
}

std::unique_ptr<Element> CreateElement(const IrNode& node) {
    switch (node.kind) {
    case NodeKind::WINDOW:
        return std::make_unique<WindowElement>();
    case NodeKind::BOARD:
        return std::make_unique<BoardElement>();
    case NodeKind::FRAME:
        return std::make_unique<Frame>();
    case NodeKind::SHADOW:
        return nullptr;
    case NodeKind::TEXT:
        return std::make_unique<TextElement>();
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
    if (TextElement* text = dynamic_cast<TextElement*>(&element)) {
        ApplyFrame(*text, node);
        ApplyCommon(*text, node);
        ApplyTypography(*text, node);
    } else if (Frame* frame = dynamic_cast<Frame*>(&element)) {
        ApplyFrame(*frame, node);
        ApplyCommon(*frame, node);
        if (WindowElement* window = dynamic_cast<WindowElement*>(frame)) {
            if (const UiValue* value = FindProperty(node, "title")) {
                window->SetTitle(value->text);
            }
        }
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
