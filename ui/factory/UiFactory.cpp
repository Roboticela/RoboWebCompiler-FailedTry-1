#include "ui/factory/UiFactory.h"

#include "compiler/semantic/UiCompiler.h"
#include "components/arrow/ArrowElement.h"
#include "components/boolean/BooleanElement.h"
#include "components/frame/Frame.h"
#include "components/group/GroupElement.h"
#include "components/layout/BoardElement.h"
#include "components/layout/WindowElement.h"
#include "components/primitive/PrimitiveElement.h"
#include "components/ellipse/EllipseElement.h"
#include "components/line/LineElement.h"
#include "components/polygon/PolygonElement.h"
#include "components/rectangle/RectangleElement.h"
#include "components/star/StarElement.h"
#include "components/svg/SvgElement.h"
#include "components/text/TextElement.h"
#include "components/vector/VectorElement.h"
#include "rendering/image/ImageDecoder.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

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

// How a block of text decides the box it covers. A word that names no mode is answered as nothing
// said rather than as a guess: the box then takes the size the text covers, which is what a text
// node did before there was a mode to name.
TextSizing ParseTextSizing(const std::string& text) {
    const std::string folded = FoldToken(text);
    if (folded == "fixed") {
        return TextSizing::FIXED;
    }
    if (folded == "autowidth" || folded == "hugwidth") {
        return TextSizing::AUTO_WIDTH;
    }
    if (folded == "autoheight" || folded == "hugheight") {
        return TextSizing::AUTO_HEIGHT;
    }
    return TextSizing::UNSET;
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

void ApplyGroup(GroupElement& group, const IrNode& node) {
    ApplyCommon(group, node);
    if (const UiValue* value = FindProperty(node, "name")) group.SetName(value->text);
    if (const UiValue* value = FindProperty(node, "minWidth")) group.SetMinWidth(value->length);
    if (const UiValue* value = FindProperty(node, "maxWidth")) group.SetMaxWidth(value->length);
    if (const UiValue* value = FindProperty(node, "minHeight")) group.SetMinHeight(value->length);
    if (const UiValue* value = FindProperty(node, "maxHeight")) group.SetMaxHeight(value->length);
    Edges margin{};
    bool hasMargin = false;
    if (const UiValue* value = FindProperty(node, "margin")) {
        margin = value->edges;
        hasMargin = true;
    }
    const auto marginSide = [&](const char* name, Length& edge) {
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
        group.SetMargin(margin);
    }
    if (const UiValue* value = FindProperty(node, "grow")) group.SetGrow(static_cast<float>(value->number));
    if (const UiValue* value = FindProperty(node, "zIndex")) group.SetZIndex(static_cast<int>(value->number));
    if (const UiValue* value = FindProperty(node, "cursor")) group.SetCursor(value->text);
    // The transform shorthand lands first, then the explicit properties have the last word.
    if (const UiValue* value = FindProperty(node, "transform")) {
        group.SetTransform(value->text);
    }
    if (const UiValue* value = FindProperty(node, "rotation")) group.SetRotation(static_cast<float>(value->number));
    if (const UiValue* value = FindProperty(node, "rotationZ")) group.SetRotation(static_cast<float>(value->number));
    const UiValue* translateX = FindProperty(node, "translateX");
    const UiValue* translateY = FindProperty(node, "translateY");
    if (translateX != nullptr || translateY != nullptr) {
        group.SetTranslate(translateX != nullptr ? static_cast<float>(translateX->number) : 0.0f,
                           translateY != nullptr ? static_cast<float>(translateY->number) : 0.0f);
    }
    const UiValue* uniformScale = FindProperty(node, "scale");
    const UiValue* scaleX = FindProperty(node, "scaleX");
    const UiValue* scaleY = FindProperty(node, "scaleY");
    if (uniformScale != nullptr || scaleX != nullptr || scaleY != nullptr) {
        const float base = uniformScale != nullptr ? static_cast<float>(uniformScale->number) : 1.0f;
        group.SetScale(scaleX != nullptr ? static_cast<float>(scaleX->number) : base,
                       scaleY != nullptr ? static_cast<float>(scaleY->number) : base);
    }
    if (const UiValue* value = FindProperty(node, "clip")) group.SetClip(value->boolean);
    if (const UiValue* value = FindProperty(node, "display")) group.SetDisplay(value->text);
    if (const UiValue* value = FindProperty(node, "visibility")) group.SetDisplay(value->text);
    // Locking is a document concern: the designer reads the attribute from the document and
    // treats a locked ancestor as locked, so a locked group already protects every child.
}

void ApplyRectangle(RectangleElement& rectangle, const IrNode& node) {
    ApplyCommon(rectangle, node);
    if (const UiValue* value = FindProperty(node, "minWidth")) rectangle.SetMinWidth(value->length);
    if (const UiValue* value = FindProperty(node, "maxWidth")) rectangle.SetMaxWidth(value->length);
    if (const UiValue* value = FindProperty(node, "minHeight")) rectangle.SetMinHeight(value->length);
    if (const UiValue* value = FindProperty(node, "maxHeight")) rectangle.SetMaxHeight(value->length);
    Edges margin{};
    bool hasMargin = false;
    if (const UiValue* value = FindProperty(node, "margin")) {
        margin = value->edges;
        hasMargin = true;
    }
    const auto marginSide = [&](const char* name, Length& edge) {
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
        rectangle.SetMargin(margin);
    }
    if (const UiValue* value = FindProperty(node, "grow")) rectangle.SetGrow(static_cast<float>(value->number));
    if (const UiValue* value = FindProperty(node, "zIndex")) rectangle.SetZIndex(static_cast<int>(value->number));
    if (const UiValue* value = FindProperty(node, "cursor")) rectangle.SetCursor(value->text);
    bool hasFill = false;
    Color fill{};
    if (const UiValue* value = FindProperty(node, "fill")) {
        fill = value->color;
        hasFill = true;
    }
    if (const UiValue* value = FindProperty(node, "background")) {
        fill = value->color;
        hasFill = true;
    }
    if (const UiValue* value = FindProperty(node, "backgroundColor")) {
        fill = value->color;
        hasFill = true;
    }
    if (hasFill) {
        rectangle.SetFill(fill);
    }
    Color stroke = rectangle.StrokeColor();
    float strokeWidth = rectangle.StrokeWidth();
    if (const UiValue* value = FindProperty(node, "stroke")) {
        stroke = value->color;
        if (strokeWidth <= 0.0f) {
            strokeWidth = 1.0f;
        }
    }
    if (const UiValue* value = FindProperty(node, "strokeWidth")) {
        strokeWidth = static_cast<float>(value->number);
    }
    rectangle.SetStroke(stroke, strokeWidth);
    // The outline is stroked the way a line and a path are stroked, so it reads the pattern and the
    // offset the pattern starts at. A pattern written as several numbers carries its own gaps, so
    // the gap read here is the one a single-number pattern is stroked with.
    if (const UiValue* value = FindProperty(node, "dash")) {
        const float gap = NumberOf(node, "dashGap", 0.0f);
        rectangle.SetDash(value->text, gap);
    }
    if (const UiValue* value = FindProperty(node, "dashOffset")) {
        rectangle.SetDashOffset(static_cast<float>(value->number));
    }
    if (const UiValue* value = FindProperty(node, "radius")) {
        rectangle.SetRadius(value->length.value);
    }
    // A named corner wins over the uniform radius, so it is applied afterwards.
    if (const UiValue* value = FindProperty(node, "radiusTopLeft")) {
        rectangle.SetCornerRadius(0, value->length.value);
    }
    if (const UiValue* value = FindProperty(node, "radiusTopRight")) {
        rectangle.SetCornerRadius(1, value->length.value);
    }
    if (const UiValue* value = FindProperty(node, "radiusBottomRight")) {
        rectangle.SetCornerRadius(2, value->length.value);
    }
    if (const UiValue* value = FindProperty(node, "radiusBottomLeft")) {
        rectangle.SetCornerRadius(3, value->length.value);
    }
    if (const UiValue* value = FindProperty(node, "rotation")) {
        rectangle.SetRotation(static_cast<float>(value->number));
    }
    if (const UiValue* value = FindProperty(node, "display")) {
        rectangle.SetDisplay(value->text);
    }
    if (const UiValue* value = FindProperty(node, "visibility")) {
        rectangle.SetDisplay(value->text);
    }
}

// The size, spacing, and identity values an ellipse and a line share with a rectangle.
// A shape has no layout of its own, so it can only be placed, sized, and capped.
void ApplyShapeBox(Element& shape, const IrNode& node) {
    if (const UiValue* value = FindProperty(node, "minWidth")) shape.SetMinWidth(value->length);
    if (const UiValue* value = FindProperty(node, "maxWidth")) shape.SetMaxWidth(value->length);
    if (const UiValue* value = FindProperty(node, "minHeight")) shape.SetMinHeight(value->length);
    if (const UiValue* value = FindProperty(node, "maxHeight")) shape.SetMaxHeight(value->length);
    Edges margin{};
    bool hasMargin = false;
    if (const UiValue* value = FindProperty(node, "margin")) {
        margin = value->edges;
        hasMargin = true;
    }
    const auto marginSide = [&](const char* name, Length& edge) {
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
        shape.SetMargin(margin);
    }
    if (const UiValue* value = FindProperty(node, "grow")) shape.SetGrow(static_cast<float>(value->number));
    if (const UiValue* value = FindProperty(node, "zIndex")) shape.SetZIndex(static_cast<int>(value->number));
}

void ApplyEllipse(EllipseElement& ellipse, const IrNode& node) {
    ApplyCommon(ellipse, node);
    ApplyShapeBox(ellipse, node);
    if (const UiValue* value = FindProperty(node, "cursor")) ellipse.SetCursor(value->text);
    bool hasFill = false;
    Color fill{};
    if (const UiValue* value = FindProperty(node, "fill")) {
        fill = value->color;
        hasFill = true;
    }
    if (const UiValue* value = FindProperty(node, "background")) {
        fill = value->color;
        hasFill = true;
    }
    if (const UiValue* value = FindProperty(node, "backgroundColor")) {
        fill = value->color;
        hasFill = true;
    }
    if (hasFill) {
        ellipse.SetFill(fill);
    }
    Color stroke = ellipse.StrokeColor();
    float strokeWidth = ellipse.StrokeWidth();
    if (const UiValue* value = FindProperty(node, "stroke")) {
        stroke = value->color;
        if (strokeWidth <= 0.0f) {
            strokeWidth = 1.0f;
        }
    }
    if (const UiValue* value = FindProperty(node, "strokeWidth")) {
        strokeWidth = static_cast<float>(value->number);
    }
    ellipse.SetStroke(stroke, strokeWidth);
    // The outline is stroked the way a line and a path are stroked, so it reads the pattern and the
    // offset the pattern starts at. A pattern written as several numbers carries its own gaps, so
    // the gap read here is the one a single-number pattern is stroked with.
    if (const UiValue* value = FindProperty(node, "dash")) {
        const float gap = NumberOf(node, "dashGap", 0.0f);
        ellipse.SetDash(value->text, gap);
    }
    if (const UiValue* value = FindProperty(node, "dashOffset")) {
        ellipse.SetDashOffset(static_cast<float>(value->number));
    }
    if (const UiValue* value = FindProperty(node, "rotation")) {
        ellipse.SetRotation(static_cast<float>(value->number));
    }
    if (const UiValue* value = FindProperty(node, "display")) {
        ellipse.SetDisplay(value->text);
    }
    if (const UiValue* value = FindProperty(node, "visibility")) {
        ellipse.SetDisplay(value->text);
    }
    // A circle is one number wide. The diameter fills in only the sizes the node did not
    // give, so a circle written with one number comes out round while an explicit width and
    // height still win and let the editor resize it.
    if (const UiValue* value = FindProperty(node, "diameter")) {
        const Length side = value->length;
        if (FindProperty(node, "width") == nullptr) {
            ellipse.SetWidth(side);
        }
        if (FindProperty(node, "height") == nullptr) {
            ellipse.SetHeight(side);
        }
    }
}

void ApplyPolygon(PolygonElement& polygon, const IrNode& node) {
    // A polygon reads everything a rectangle reads, because it is a rectangle with a different
    // outline. Only the number of sides is read here.
    ApplyRectangle(polygon, node);
    if (const UiValue* value = FindProperty(node, "sides")) {
        polygon.SetSides(static_cast<int>(value->number));
    }
}

void ApplyStar(StarElement& star, const IrNode& node) {
    // A star reads everything a polygon reads, because it is a polygon whose ring holds two radii.
    // Only the two values that shape that ring are read here.
    ApplyPolygon(star, node);
    if (const UiValue* value = FindProperty(node, "points")) {
        star.SetPoints(static_cast<int>(value->number));
    }
    if (const UiValue* value = FindProperty(node, "innerRadius")) {
        star.SetInnerRadius(static_cast<float>(value->number));
    }
}

void ApplyBoolean(BooleanElement& group, const IrNode& node) {
    // A boolean group reads everything a rectangle reads, because it is a shape with a fill, a
    // stroke, and a pattern to stroke it with. Only the operation is read here: the outline it draws
    // is not written in the document, it is computed from the shapes the group holds.
    ApplyRectangle(group, node);
    if (const UiValue* value = FindProperty(node, "operation")) {
        group.SetOperation(ParseBooleanOperation(value->text));
    }
}

void ApplyVector(VectorElement& vector, const IrNode& node) {
    // A vector reads everything a rectangle reads, because it is a shape with a fill and a stroke.
    // Only the values that describe the geometry it draws are read here.
    ApplyRectangle(vector, node);
    if (const UiValue* value = FindProperty(node, "d")) {
        vector.SetPathData(value->text);
    }
    if (const UiValue* value = FindProperty(node, "shape")) {
        vector.SetShape(value->text);
    }
    if (const UiValue* value = FindProperty(node, "viewBox")) {
        vector.SetViewBox(value->text);
    }
    std::string cap;
    std::string join;
    std::string rule;
    if (const UiValue* value = FindProperty(node, "lineCap")) {
        cap = value->text;
    }
    if (const UiValue* value = FindProperty(node, "lineJoin")) {
        join = value->text;
    }
    if (const UiValue* value = FindProperty(node, "fillRule")) {
        rule = value->text;
    }
    vector.SetLineStyle(cap, join, rule);
}

// The art an SVG document holds: the nodes that draw it, and the view box it was written in.
struct SvgArtwork {
    std::vector<IrNode> layers;
    std::string viewBox;
    bool hasViewBox = false;
};

// The path a file a node names is read from. A document names its art the way the project around it
// is laid out, which is not the directory the program happened to be started from: art read from
// wherever the program was launched is art that shows in one place and vanishes in another. So the
// name is looked for beside the document that wrote it, then beside the program, and last beside
// the project the document belongs to, which is where a document written against the layout of the
// project has always found its art. A name that is found nowhere is handed on as it was written, so
// a reader of it says what it can about a file nobody keeps.
std::string NamedFile(const std::string& name, const std::string& document) {
    const std::filesystem::path named = name;
    if (name.empty() || named.is_absolute()) {
        return name;
    }
    std::error_code error;
    std::vector<std::filesystem::path> places;
    if (!document.empty()) {
        places.push_back(std::filesystem::path(document).parent_path());
    }
    places.push_back(std::filesystem::current_path(error));
    places.push_back(std::filesystem::path(RUNTIME_SOURCE_DIR));
    for (const std::filesystem::path& place : places) {
        if (place.empty()) {
            continue;
        }
        const std::filesystem::path beside = place / named;
        if (std::filesystem::exists(beside, error)) {
            return beside.string();
        }
    }
    return name;
}

// The markup the art is written in. Art that is written into the document is the markup itself,
// which is how a document can carry an icon with no file beside it; anything else names a file,
// the way an image is given one.
std::string SvgMarkup(const std::string& source, const std::string& document) {
    if (source.find("<svg") != std::string::npos) {
        return source;
    }
    std::ifstream file(NamedFile(source, document), std::ios::binary);
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

// The art of an SVG document read into the nodes that draw it. The paths of the file become the
// same <path> nodes, and the same <group> nodes around them, that an imported icon is made of, so
// the one reader of SVG documents feeds both of them and neither is written twice. The colour the
// artwork calls its current colour is handed over as the colour the node draws in.
SvgArtwork ReadSvgArtwork(const std::string& markup, const Color& ink) {
    SvgArtwork art;
    const auto channel = [](float value) {
        return static_cast<unsigned char>(std::clamp(value, 0.0f, 1.0f) * 255.0f);
    };
    const std::string xml = VectorXmlFromSvg(markup, "Artwork", channel(ink.r), channel(ink.g), channel(ink.b));
    if (xml.empty()) {
        return art;
    }
    // The art comes back in the language of the document it was written for, so it is read by the
    // same compiler every other node is read by rather than by a reader of its own.
    const CompileResult compiled = CompileUi(SourceFile{"svg.xml", "<window>" + xml + "</window>"});
    if (!compiled.Ok() || !compiled.root.has_value() || compiled.root->children.size() != 1) {
        return art;
    }
    const IrNode& root = compiled.root->children.front();
    if (const UiValue* value = FindProperty(root, "viewBox")) {
        art.viewBox = value->text;
        art.hasViewBox = true;
    }
    art.layers = root.children;
    return art;
}

// True when the size of a node is left to the art it draws: the document either said nothing or
// asked the size to fit the art, and for an SVG the art is the size its drawing was written for.
bool SizedByArt(const IrNode& node, const char* name) {
    const UiValue* value = FindProperty(node, name);
    return value == nullptr || value->length.unit == LengthUnit::FIT;
}

// The art a node draws, which is either the paths the document wrote inside it or the paths of
// the file it names. The art written inside the node is the art the document can pick apart: every
// path of it is a node of the document tree, drawn where the document placed it. The file is read
// only for a node that carries no art of its own, so a node with children keeps them as its art and
// a node with none draws the file it names. Both end up as the same tree, which is why one reader
// of SVG documents feeds both of them.
std::vector<IrNode> ApplySvg(SvgElement& svg, const IrNode& node) {
    if (const UiValue* value = FindProperty(node, "source")) {
        svg.SetSource(value->text);
    }
    // Everything a shape reads, an SVG reads the same way, so a value the document wrote is already
    // on the node when the file is read for it.
    ApplyVector(svg, node);
    if (!node.children.empty()) {
        // The art is the document's own, so it belongs to the node where it was written and no file
        // is read for it. The children themselves are built by the reader of every other child. A
        // size the document left open is the size the art was authored at, the way the box a file
        // was written for is the size of a node that draws that file; the size a shape falls back
        // to of its own is only for a shape that draws an outline of its own.
        if (SizedByArt(node, "width")) {
            svg.SetWidth(Length::Fit());
        }
        if (SizedByArt(node, "height")) {
            svg.SetHeight(Length::Fit());
        }
        return {};
    }
    const SvgArtwork art = ReadSvgArtwork(SvgMarkup(svg.Source(), node.location.path), svg.Fill());
    if (art.hasViewBox && !svg.HasViewBox()) {
        // The art was written in the view box of the file, so the node draws it in that one unless
        // the document said where it belongs.
        svg.SetViewBox(art.viewBox);
    }
    const Rect box = svg.ViewBox();
    if (box.width > 0.0f && box.height > 0.0f) {
        // The box the art was written for is the size of the node unless the document gave a size
        // of its own, so a size the document wrote is the size the art is scaled into.
        if (SizedByArt(node, "width")) {
            svg.SetWidth(Length::Pixels(box.width));
        }
        if (SizedByArt(node, "height")) {
            svg.SetHeight(Length::Pixels(box.height));
        }
    }
    return art.layers;
}

void ApplyLine(LineElement& line, const IrNode& node) {
    ApplyCommon(line, node);
    ApplyShapeBox(line, node);
    if (const UiValue* value = FindProperty(node, "cursor")) line.SetCursor(value->text);
    line.SetEndpoints(NumberOf(node, "x1", 0.0f), NumberOf(node, "y1", 0.0f), NumberOf(node, "x2", 80.0f), NumberOf(node, "y2", 0.0f));
    Color stroke = line.StrokeColor();
    float strokeWidth = line.StrokeWidth();
    if (const UiValue* value = FindProperty(node, "stroke")) {
        stroke = value->color;
    }
    if (const UiValue* value = FindProperty(node, "strokeWidth")) {
        strokeWidth = static_cast<float>(value->number);
    }
    line.SetStroke(stroke, strokeWidth);
    if (const UiValue* value = FindProperty(node, "lineCap")) {
        line.SetLineCap(value->text);
    }
    // A dash pattern written as numbers carries its own gaps; a single number is a dash
    // length whose gap comes from dashGap.
    if (const UiValue* value = FindProperty(node, "dash")) {
        const float gap = NumberOf(node, "dashGap", 0.0f);
        line.SetDash(value->text, gap);
    }
    if (const UiValue* value = FindProperty(node, "rotation")) {
        line.SetRotation(static_cast<float>(value->number));
    }
    if (const UiValue* value = FindProperty(node, "display")) {
        line.SetDisplay(value->text);
    }
    if (const UiValue* value = FindProperty(node, "visibility")) {
        line.SetDisplay(value->text);
    }
}

void ApplyArrow(ArrowElement& arrow, const IrNode& node) {
    // Everything a line reads, an arrow reads the same way, because an arrow is the line plus
    // a head. Only the three values that shape the head are read here.
    ApplyLine(arrow, node);
    if (const UiValue* value = FindProperty(node, "head")) {
        arrow.SetHead(value->text);
    }
    if (const UiValue* value = FindProperty(node, "headLength")) {
        arrow.SetHeadLength(static_cast<float>(value->number));
    }
    if (const UiValue* value = FindProperty(node, "headWidth")) {
        arrow.SetHeadWidth(static_cast<float>(value->number));
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
        // The file is read later, where the image is first drawn, so the name is settled here: a
        // name read beside the document is what makes an image show whatever the program was
        // started from, the way the art of an SVG does.
        shape.SetSource(NamedFile(value->text, node.location.path));
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
    // A path is stroked the way a line is stroked, so it reads the dash pattern and the offset the
    // pattern starts at. A pattern written as several numbers carries its own gaps, so none is
    // read here.
    if (const UiValue* value = FindProperty(node, "dash")) {
        shape.SetDash(value->text, 0.0f);
    }
    if (const UiValue* value = FindProperty(node, "dashOffset")) {
        shape.SetDashOffset(static_cast<float>(value->number));
    }
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
    if (const UiValue* value = FindProperty(node, "sizing")) {
        text.SetSizing(ParseTextSizing(value->text));
    }
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
    // The case a run is written in, named the way the text subsystem names it: one word for the case
    // rather than the appearance the case produces, so the word is read here and the transform it
    // means is what the run is drawn with. A word that means no case leaves the transform standing.
    if (const UiValue* value = FindProperty(node, "textCase")) {
        const std::string folded = FoldToken(value->text);
        if (folded == "upper" || folded == "uppercase") {
            text.SetTransform("uppercase", true);
        } else if (folded == "lower" || folded == "lowercase") {
            text.SetTransform("lowercase", true);
        } else if (folded == "original" || folded == "none") {
            text.SetTransform("none", true);
        }
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
    // The line a run is drawn through, named the way the text subsystem names it: one word for the
    // line rather than one flag per line. A document may say either, so a word that names a line
    // replaces what the flags said, and a word that names none takes every flag away.
    if (const UiValue* value = FindProperty(node, "textDecoration")) {
        const std::string folded = FoldToken(value->text);
        underline = folded == "underline";
        strike = folded == "strikethrough" || folded == "strike" || folded == "linethrough";
        overline = folded == "overline";
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
    case NodeKind::RECTANGLE:
        return std::make_unique<RectangleElement>();
    case NodeKind::ELLIPSE:
        return std::make_unique<EllipseElement>();
    case NodeKind::POLYGON:
        return std::make_unique<PolygonElement>();
    case NodeKind::STAR:
        return std::make_unique<StarElement>();
    case NodeKind::VECTOR:
        return std::make_unique<VectorElement>();
    case NodeKind::SVG:
        return std::make_unique<SvgElement>();
    case NodeKind::LINE:
        return std::make_unique<LineElement>();
    case NodeKind::ARROW:
        return std::make_unique<ArrowElement>();
    case NodeKind::BOOLEAN:
        return std::make_unique<BooleanElement>();
    case NodeKind::SHADOW:
        return nullptr;
    case NodeKind::TEXT:
        return std::make_unique<TextElement>();
    case NodeKind::GROUP:
        return std::make_unique<GroupElement>();
    case NodeKind::IMAGE:
    case NodeKind::PATH:
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
    // The children of an SVG that names a file are the file's, not the document's: they are read
    // here and take the place of the children the node declares, which such a node has none of. An
    // SVG the document carries draws the children it declares, so the art it holds stays in the
    // tree where the document can pick it apart. Either way the children are built below.
    std::vector<IrNode> artwork;
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
    } else if (GroupElement* group = dynamic_cast<GroupElement*>(&element)) {
        ApplyGroup(*group, node);
    } else if (StarElement* star = dynamic_cast<StarElement*>(&element)) {
        // A star is a polygon, so it has to be asked first: the polygon branch below would
        // otherwise take it and leave the points of the ring unsaid.
        ApplyStar(*star, node);
    } else if (SvgElement* svg = dynamic_cast<SvgElement*>(&element)) {
        // An SVG is a vector whose art comes from a file, so it is asked before the vector
        // underneath: the vector branch would otherwise take it and leave the file unread.
        artwork = ApplySvg(*svg, node);
    } else if (VectorElement* vector = dynamic_cast<VectorElement*>(&element)) {
        // A vector is a rectangle, so it has to be asked first: the rectangle branch below would
        // otherwise take it and leave the path it draws unsaid.
        ApplyVector(*vector, node);
    } else if (BooleanElement* boolean = dynamic_cast<BooleanElement*>(&element)) {
        // A boolean group is a rectangle, so it has to be asked first: the rectangle branch below
        // would otherwise take it and leave the operation unsaid.
        ApplyBoolean(*boolean, node);
    } else if (PolygonElement* polygon = dynamic_cast<PolygonElement*>(&element)) {
        // A polygon is a rectangle, so it has to be asked first: the rectangle branch below
        // would otherwise take it and leave the sides unsaid.
        ApplyPolygon(*polygon, node);
    } else if (RectangleElement* rectangle = dynamic_cast<RectangleElement*>(&element)) {
        ApplyRectangle(*rectangle, node);
    } else if (EllipseElement* ellipse = dynamic_cast<EllipseElement*>(&element)) {
        ApplyEllipse(*ellipse, node);
    } else if (ArrowElement* arrow = dynamic_cast<ArrowElement*>(&element)) {
        // An arrow is a line, so it has to be asked first: the line branch below would
        // otherwise take it and leave the head as a line with nothing on the end.
        ApplyArrow(*arrow, node);
    } else if (LineElement* line = dynamic_cast<LineElement*>(&element)) {
        ApplyLine(*line, node);
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
    const std::vector<IrNode>& children = !artwork.empty() ? artwork : (definition == nullptr ? node.children : definition->children);
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
