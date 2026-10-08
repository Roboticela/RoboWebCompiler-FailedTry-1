#include "compiler/semantic/ComponentCatalog.h"

#include <algorithm>
#include <initializer_list>
#include <string_view>

namespace Runtime {
namespace {

std::vector<PropertySpec> EventProperties();

std::vector<PropertySpec> FrameProperties() {
    std::vector<PropertySpec> properties = {
        {"id", TypeKind::STRING},
        {"name", TypeKind::STRING},
        {"class", TypeKind::STRING},
        {"component", TypeKind::STRING},
        {"instance", TypeKind::STRING},
        {"tag", TypeKind::STRING},
        {"role", TypeKind::STRING},
        {"width", TypeKind::LENGTH},
        {"height", TypeKind::LENGTH},
        {"auto", TypeKind::BOOL},
        {"minWidth", TypeKind::LENGTH},
        {"maxWidth", TypeKind::LENGTH},
        {"minHeight", TypeKind::LENGTH},
        {"maxHeight", TypeKind::LENGTH},
        {"widthMode", TypeKind::STRING},
        {"heightMode", TypeKind::STRING},
        {"aspectRatio", TypeKind::FLOAT},
        {"x", TypeKind::LENGTH},
        {"y", TypeKind::LENGTH},
        {"zIndex", TypeKind::INT},
        {"position", TypeKind::STRING},
        {"anchor", TypeKind::STRING},
        {"padding", TypeKind::EDGES},
        {"paddingTop", TypeKind::LENGTH},
        {"paddingRight", TypeKind::LENGTH},
        {"paddingBottom", TypeKind::LENGTH},
        {"paddingLeft", TypeKind::LENGTH},
        {"margin", TypeKind::EDGES},
        {"marginTop", TypeKind::LENGTH},
        {"marginRight", TypeKind::LENGTH},
        {"marginBottom", TypeKind::LENGTH},
        {"marginLeft", TypeKind::LENGTH},
        {"layout", TypeKind::STRING},
        {"grow", TypeKind::FLOAT},
        {"shrink", TypeKind::FLOAT},
        {"basis", TypeKind::LENGTH},
        {"gap", TypeKind::LENGTH},
        {"rowGap", TypeKind::LENGTH},
        {"columnGap", TypeKind::LENGTH},
        {"align", TypeKind::ALIGNMENT},
        {"alignItems", TypeKind::STRING},
        {"justify", TypeKind::STRING},
        {"justifyContent", TypeKind::STRING},
        {"wrap", TypeKind::STRING},
        {"columns", TypeKind::INT},
        {"rows", TypeKind::INT},
        {"column", TypeKind::INT},
        {"row", TypeKind::INT},
        {"columnSpan", TypeKind::INT},
        {"rowSpan", TypeKind::INT},
        {"background", TypeKind::COLOR},
        {"backgroundColor", TypeKind::COLOR},
        {"fill", TypeKind::COLOR},
        {"gradient", TypeKind::STRING},
        {"image", TypeKind::STRING},
        {"opacity", TypeKind::FLOAT},
        {"borderWidth", TypeKind::LENGTH},
        {"borderColor", TypeKind::COLOR},
        {"stroke", TypeKind::COLOR},
        {"strokeWidth", TypeKind::LENGTH},
        {"borderStyle", TypeKind::STRING},
        {"borderTop", TypeKind::LENGTH},
        {"borderRight", TypeKind::LENGTH},
        {"borderBottom", TypeKind::LENGTH},
        {"borderLeft", TypeKind::LENGTH},
        {"radius", TypeKind::LENGTH},
        {"radiusTopLeft", TypeKind::LENGTH},
        {"radiusTopRight", TypeKind::LENGTH},
        {"radiusBottomRight", TypeKind::LENGTH},
        {"radiusBottomLeft", TypeKind::LENGTH},
        {"shadow", TypeKind::STRING},
        {"shadowColor", TypeKind::COLOR},
        {"shadowX", TypeKind::FLOAT},
        {"shadowY", TypeKind::FLOAT},
        {"shadowBlur", TypeKind::FLOAT},
        {"shadowSpread", TypeKind::FLOAT},
        {"overflow", TypeKind::STRING},
        {"scrollbar", TypeKind::STRING},
        {"scrollbarVisibility", TypeKind::STRING},
        {"smoothScrolling", TypeKind::BOOL},
        {"scrollStep", TypeKind::FLOAT},
        {"overscroll", TypeKind::STRING},
        {"scrollPhysics", TypeKind::STRING},
        {"clip", TypeKind::BOOL},
        {"clipRadius", TypeKind::LENGTH},
        {"clipPath", TypeKind::STRING},
        {"translateX", TypeKind::FLOAT},
        {"translateY", TypeKind::FLOAT},
        {"scale", TypeKind::FLOAT},
        {"scaleX", TypeKind::FLOAT},
        {"scaleY", TypeKind::FLOAT},
        {"rotation", TypeKind::FLOAT},
        {"rotationX", TypeKind::FLOAT},
        {"rotationY", TypeKind::FLOAT},
        {"rotationZ", TypeKind::FLOAT},
        {"skewX", TypeKind::FLOAT},
        {"skewY", TypeKind::FLOAT},
        {"transformOrigin", TypeKind::STRING},
        {"visible", TypeKind::BOOL},
        {"locked", TypeKind::BOOL},
        {"visibility", TypeKind::STRING},
        {"display", TypeKind::STRING},
        {"blendMode", TypeKind::STRING},
        {"blur", TypeKind::FLOAT},
        {"backdropBlur", TypeKind::FLOAT},
        {"brightness", TypeKind::FLOAT},
        {"contrast", TypeKind::FLOAT},
        {"saturation", TypeKind::FLOAT},
        {"grayscale", TypeKind::FLOAT},
        {"hueRotate", TypeKind::FLOAT},
        {"cursor", TypeKind::STRING},
        {"style", TypeKind::STRING},
        {"horizontal", TypeKind::STRING},
        {"vertical", TypeKind::STRING},
        {"left", TypeKind::STRING},
        {"right", TypeKind::STRING},
        {"top", TypeKind::STRING},
        {"bottom", TypeKind::STRING},
        {"centerX", TypeKind::STRING},
        {"centerY", TypeKind::STRING},
        {"direction", TypeKind::STRING},
        {"alignment", TypeKind::STRING},
        {"distribution", TypeKind::STRING},
        {"minScreen", TypeKind::FLOAT},
        {"maxScreen", TypeKind::FLOAT},
        {"orientation", TypeKind::STRING},
        {"minContainer", TypeKind::FLOAT},
        {"variant", TypeKind::STRING},
    };
    const std::vector<PropertySpec> events = EventProperties();
    properties.insert(properties.end(), events.begin(), events.end());
    return properties;
}

std::vector<PropertySpec> WindowProperties() {
    std::vector<PropertySpec> properties = FrameProperties();
    properties.insert(properties.begin() + 2, {"title", TypeKind::STRING});
    properties.insert(properties.begin() + 3, {"mobile", TypeKind::BOOL});
    properties.insert(properties.begin() + 4, {"tablet", TypeKind::BOOL});
    properties.insert(properties.begin() + 5, {"desktop", TypeKind::BOOL});
    return properties;
}

std::vector<PropertySpec> EventProperties() {
    const char* events[] = {"onPointerDown", "onPointerUp",     "onPointerMove", "onPointerEnter", "onPointerLeave", "onPointerOver",
                            "onPointerOut",  "onPointerCancel", "onClick",       "onDoubleClick",  "onContextMenu",  "onTouchStart",
                            "onTouchMove",   "onTouchEnd",      "onTouchCancel", "onKeyDown",      "onKeyUp",        "onKeyPress",
                            "onFocus",       "onBlur",          "onFocusIn",     "onFocusOut",     "onInput",        "onChange",
                            "onBeforeInput", "onCompositionStart", "onCompositionUpdate", "onCompositionEnd", "onSubmit", "onReset",
                            "onValidate",    "onInvalid",       "onDragStart",   "onDrag",         "onDragEnter",    "onDragOver",
                            "onDragLeave",   "onDrop",          "onDragEnd",     "onScroll",       "onScrollStart",  "onScrollEnd",
                            "onCreate",      "onMount",         "onUnmount",     "onDestroy",      "onLoad",         "onUnload",
                            "onResize",      "onLayout",        "onPositionChange", "onSizeChange", "onAnimationStart", "onAnimationUpdate",
                            "onAnimationComplete", "onAnimationCancel", "onStateChange", "onPropertyChange", "onVariantChange", "onSelect",
                            "onDeselect",    "onSelectionChange", "onLoadStart", "onLoadError",    "onPlay",         "onPause",
                            "onEnded",       "onTimeUpdate"};
    std::vector<PropertySpec> properties;
    for (const char* event : events) {
        properties.push_back({event, TypeKind::EVENT_HANDLER});
    }
    return properties;
}

std::vector<PropertySpec> IdentityProperties() {
    std::vector<PropertySpec> properties = {
        {"id", TypeKind::STRING},
        {"name", TypeKind::STRING},
        {"style", TypeKind::STRING},
        {"x", TypeKind::LENGTH},
        {"y", TypeKind::LENGTH},
        {"width", TypeKind::LENGTH},
        {"height", TypeKind::LENGTH},
        {"position", TypeKind::STRING},
        {"opacity", TypeKind::FLOAT},
        {"visible", TypeKind::BOOL},
    };
    const std::vector<PropertySpec> events = EventProperties();
    properties.insert(properties.end(), events.begin(), events.end());
    return properties;
}

std::vector<PropertySpec> With(std::vector<PropertySpec> base, std::vector<PropertySpec> extra) {
    base.insert(base.end(), extra.begin(), extra.end());
    return base;
}

// The same values a component already declares, less the ones it has no use for. A component that
// takes the values of the component it is built on still has to refuse the ones it cannot draw
// with, so the list it starts from is carried over by name rather than written out again.
std::vector<PropertySpec> Without(std::vector<PropertySpec> base, std::initializer_list<std::string_view> removed) {
    base.erase(std::remove_if(base.begin(), base.end(),
                              [&](const PropertySpec& property) {
                                  return std::find(removed.begin(), removed.end(), std::string_view(property.name)) != removed.end();
                              }),
               base.end());
    return base;
}

// A group combines its children into one node. It takes everything that a single node
// can carry and hand to its children, and nothing that belongs to layout: no padding,
// no gap, no direction, no background. Properties outside this list are rejected by the
// compiler instead of being accepted and ignored.
std::vector<PropertySpec> GroupProperties() {
    return With(IdentityProperties(), {{"class", TypeKind::STRING},
                                       {"tag", TypeKind::STRING},
                                       {"role", TypeKind::STRING},
                                       {"minWidth", TypeKind::LENGTH},
                                       {"maxWidth", TypeKind::LENGTH},
                                       {"minHeight", TypeKind::LENGTH},
                                       {"maxHeight", TypeKind::LENGTH},
                                       {"margin", TypeKind::EDGES},
                                       {"marginTop", TypeKind::LENGTH},
                                       {"marginRight", TypeKind::LENGTH},
                                       {"marginBottom", TypeKind::LENGTH},
                                       {"marginLeft", TypeKind::LENGTH},
                                       {"grow", TypeKind::FLOAT},
                                       {"zIndex", TypeKind::INT},
                                       {"transform", TypeKind::STRING},
                                       {"rotation", TypeKind::FLOAT},
                                       {"rotationZ", TypeKind::FLOAT},
                                       {"translateX", TypeKind::FLOAT},
                                       {"translateY", TypeKind::FLOAT},
                                       {"scale", TypeKind::FLOAT},
                                       {"scaleX", TypeKind::FLOAT},
                                       {"scaleY", TypeKind::FLOAT},
                                       {"clip", TypeKind::BOOL},
                                       {"cursor", TypeKind::STRING},
                                       {"display", TypeKind::STRING},
                                       {"visibility", TypeKind::STRING},
                                       {"locked", TypeKind::BOOL},
                                       {"left", TypeKind::STRING},
                                       {"right", TypeKind::STRING},
                                       {"top", TypeKind::STRING},
                                       {"bottom", TypeKind::STRING},
                                       {"centerX", TypeKind::STRING},
                                       {"centerY", TypeKind::STRING},
                                       {"horizontal", TypeKind::STRING},
                                       {"vertical", TypeKind::STRING},
                                       {"minScreen", TypeKind::FLOAT},
                                       {"maxScreen", TypeKind::FLOAT},
                                       {"orientation", TypeKind::STRING},
                                       {"minContainer", TypeKind::FLOAT}});
}

// A rectangle is a shape, so it takes the values that describe a shape and nothing that
// describes a box of content: no layout, no padding, no gap, no overflow, no scrolling. The
// pattern its outline is stroked with is one of those values, so it is taken here and every shape
// built on a rectangle inherits it rather than repeating it.
std::vector<PropertySpec> RectangleProperties() {
    return With(IdentityProperties(), {{"minWidth", TypeKind::LENGTH},
                                       {"maxWidth", TypeKind::LENGTH},
                                       {"minHeight", TypeKind::LENGTH},
                                       {"maxHeight", TypeKind::LENGTH},
                                       {"margin", TypeKind::EDGES},
                                       {"marginTop", TypeKind::LENGTH},
                                       {"marginRight", TypeKind::LENGTH},
                                       {"marginBottom", TypeKind::LENGTH},
                                       {"marginLeft", TypeKind::LENGTH},
                                       {"grow", TypeKind::FLOAT},
                                       {"zIndex", TypeKind::INT},
                                       {"fill", TypeKind::COLOR},
                                       {"background", TypeKind::COLOR},
                                       {"backgroundColor", TypeKind::COLOR},
                                       {"stroke", TypeKind::COLOR},
                                       {"strokeWidth", TypeKind::FLOAT},
                                       {"dash", TypeKind::STRING},
                                       {"dashGap", TypeKind::FLOAT},
                                       {"dashOffset", TypeKind::FLOAT},
                                       {"radius", TypeKind::LENGTH},
                                       {"radiusTopLeft", TypeKind::LENGTH},
                                       {"radiusTopRight", TypeKind::LENGTH},
                                       {"radiusBottomRight", TypeKind::LENGTH},
                                       {"radiusBottomLeft", TypeKind::LENGTH},
                                       {"rotation", TypeKind::FLOAT},
                                       {"cursor", TypeKind::STRING},
                                       {"display", TypeKind::STRING},
                                       {"visibility", TypeKind::STRING},
                                       {"locked", TypeKind::BOOL},
                                       {"left", TypeKind::STRING},
                                       {"right", TypeKind::STRING},
                                       {"top", TypeKind::STRING},
                                       {"bottom", TypeKind::STRING},
                                       {"centerX", TypeKind::STRING},
                                       {"centerY", TypeKind::STRING},
                                       {"horizontal", TypeKind::STRING},
                                       {"vertical", TypeKind::STRING},
                                       {"minScreen", TypeKind::FLOAT},
                                       {"maxScreen", TypeKind::FLOAT},
                                       {"orientation", TypeKind::STRING},
                                       {"minContainer", TypeKind::FLOAT}});
}

std::vector<PropertySpec> TextProperties() {
    return With(IdentityProperties(), {{"text", TypeKind::STRING},
                                       {"value", TypeKind::STRING},
                                       {"fontFamily", TypeKind::STRING},
                                       {"fontSize", TypeKind::FONT_SIZE},
                                       {"fontWeight", TypeKind::INT},
                                       {"fontStyle", TypeKind::STRING},
                                       {"letterSpacing", TypeKind::FLOAT},
                                       {"wordSpacing", TypeKind::FLOAT},
                                       {"lineHeight", TypeKind::FLOAT},
                                       {"textColor", TypeKind::COLOR},
                                       {"textAlign", TypeKind::STRING},
                                       {"verticalAlign", TypeKind::STRING},
                                       {"wrap", TypeKind::STRING},
                                       {"maxLines", TypeKind::INT},
                                       {"ellipsis", TypeKind::BOOL},
                                       {"overflow", TypeKind::STRING},
                                       {"underline", TypeKind::BOOL},
                                       {"lineThrough", TypeKind::BOOL},
                                       {"overline", TypeKind::BOOL},
                                       {"decorationColor", TypeKind::COLOR},
                                       {"decorationWidth", TypeKind::FLOAT},
                                       {"decorationStyle", TypeKind::STRING},
                                       {"textTransform", TypeKind::STRING},
                                       {"fontFeatures", TypeKind::STRING},
                                       {"language", TypeKind::STRING},
                                       {"direction", TypeKind::STRING},
                                       {"writingMode", TypeKind::STRING}});
}

// An image is a box drawn around a picture. It takes the values of a node and the values of the
// picture: the file, the fitting that decides how much of the box it covers, the region of the file
// that is drawn, where it sits in a box it does not cover, and the look it is washed with. The box
// it is given is its own, so a picture has its own radius, its own clip, and its own turn. What it
// refuses is everything about holding content: no layout of children, no padding, no gap, and no
// overflow to scroll, because a picture is one thing in the flow of its parent.
std::vector<PropertySpec> ImageProperties() {
    return With(IdentityProperties(), {{"source", TypeKind::STRING},
                                       {"fit", TypeKind::STRING},
                                       {"crop", TypeKind::STRING},
                                       {"objectPosition", TypeKind::STRING},
                                       {"radius", TypeKind::LENGTH},
                                       {"clip", TypeKind::BOOL},
                                       {"tint", TypeKind::COLOR},
                                       {"brightness", TypeKind::FLOAT},
                                       {"contrast", TypeKind::FLOAT},
                                       {"saturation", TypeKind::FLOAT},
                                       {"flipX", TypeKind::BOOL},
                                       {"flipY", TypeKind::BOOL},
                                       {"rotation", TypeKind::FLOAT}});
}

// A vector is a shape, so it takes the values that describe a shape and nothing that describes a
// box of content: no layout, no padding, no gap, no overflow, no scrolling. It draws path
// geometry, so it also takes the path it draws, the view box that path is written in, the rule
// its fill follows, and the way its ends and joins are drawn.
std::vector<PropertySpec> VectorProperties() {
    return With(RectangleProperties(), {{"shape", TypeKind::STRING},
                                        {"d", TypeKind::STRING},
                                        {"viewBox", TypeKind::STRING},
                                        {"fillRule", TypeKind::STRING},
                                        {"lineCap", TypeKind::STRING},
                                        {"lineJoin", TypeKind::STRING}});
}

// An SVG is a vector whose art is drawn either from a file it names or from the paths written
// inside it. Both are art written in a view box of its own, so it takes everything a vector takes:
// the box, the fill, the stroke, the pattern to stroke it with, the geometry it draws of its own,
// the rule its fill follows, and the way its ends and joins are drawn. What it adds is the file,
// which is one of the two ways its art arrives rather than the only one. Nothing is refused here,
// because an SVG the document carries is a vector in every way a document cares about.
std::vector<PropertySpec> SvgProperties() {
    return With(VectorProperties(), {{"source", TypeKind::STRING}});
}

std::vector<PropertySpec> PathProperties() {
    return With(IdentityProperties(), {{"d", TypeKind::STRING},
                                       {"viewBox", TypeKind::STRING},
                                       {"fill", TypeKind::COLOR},
                                       {"stroke", TypeKind::COLOR},
                                       {"strokeWidth", TypeKind::FLOAT},
                                       {"lineCap", TypeKind::STRING},
                                       {"lineJoin", TypeKind::STRING},
                                       {"fillRule", TypeKind::STRING},
                                       {"dash", TypeKind::STRING},
                                       {"dashOffset", TypeKind::FLOAT}});
}

// An ellipse is a shape, so it takes the values that describe a shape and nothing that
// describes a box of content: no layout, no padding, no gap, no overflow, no scrolling.
// A circle is the same component with a square box and one diameter instead of two sizes.
std::vector<PropertySpec> EllipseProperties(bool circle) {
    std::vector<PropertySpec> properties = With(IdentityProperties(), {{"minWidth", TypeKind::LENGTH},
                                                                       {"maxWidth", TypeKind::LENGTH},
                                                                       {"minHeight", TypeKind::LENGTH},
                                                                       {"maxHeight", TypeKind::LENGTH},
                                                                       {"margin", TypeKind::EDGES},
                                                                       {"marginTop", TypeKind::LENGTH},
                                                                       {"marginRight", TypeKind::LENGTH},
                                                                       {"marginBottom", TypeKind::LENGTH},
                                                                       {"marginLeft", TypeKind::LENGTH},
                                                                       {"grow", TypeKind::FLOAT},
                                                                       {"zIndex", TypeKind::INT},
                                                                       {"fill", TypeKind::COLOR},
                                                                       {"background", TypeKind::COLOR},
                                                                       {"backgroundColor", TypeKind::COLOR},                                       {"stroke", TypeKind::COLOR},
                                       {"strokeWidth", TypeKind::FLOAT},
                                       {"dash", TypeKind::STRING},
                                       {"dashGap", TypeKind::FLOAT},
                                       {"dashOffset", TypeKind::FLOAT},
                                       {"rotation", TypeKind::FLOAT},
                                       {"cursor", TypeKind::STRING},
                                       {"display", TypeKind::STRING},
                                       {"visibility", TypeKind::STRING},
                                       {"locked", TypeKind::BOOL},
                                       {"left", TypeKind::STRING},
                                       {"right", TypeKind::STRING},
                                       {"top", TypeKind::STRING},
                                       {"bottom", TypeKind::STRING},
                                       {"centerX", TypeKind::STRING},
                                       {"centerY", TypeKind::STRING},
                                       {"horizontal", TypeKind::STRING},
                                       {"vertical", TypeKind::STRING},
                                       {"minScreen", TypeKind::FLOAT},
                                       {"maxScreen", TypeKind::FLOAT},
                                       {"orientation", TypeKind::STRING},
                                       {"minContainer", TypeKind::FLOAT}});
    if (circle) {
        properties.push_back({"diameter", TypeKind::LENGTH});
    }
    return properties;
}

// A line is a shape too, but the only paint it owns is its stroke: a fill would have no
// area to cover. Its geometry is the two endpoints, so it has no radius to round.
std::vector<PropertySpec> LineProperties() {
    return With(IdentityProperties(), {{"minWidth", TypeKind::LENGTH},
                                       {"maxWidth", TypeKind::LENGTH},
                                       {"minHeight", TypeKind::LENGTH},
                                       {"maxHeight", TypeKind::LENGTH},
                                       {"margin", TypeKind::EDGES},
                                       {"marginTop", TypeKind::LENGTH},
                                       {"marginRight", TypeKind::LENGTH},
                                       {"marginBottom", TypeKind::LENGTH},
                                       {"marginLeft", TypeKind::LENGTH},
                                       {"grow", TypeKind::FLOAT},
                                       {"zIndex", TypeKind::INT},
                                       {"x1", TypeKind::FLOAT},
                                       {"y1", TypeKind::FLOAT},
                                       {"x2", TypeKind::FLOAT},
                                       {"y2", TypeKind::FLOAT},
                                       {"stroke", TypeKind::COLOR},
                                       {"strokeWidth", TypeKind::FLOAT},
                                       {"lineCap", TypeKind::STRING},
                                       {"dash", TypeKind::STRING},
                                       {"dashGap", TypeKind::FLOAT},
                                       {"rotation", TypeKind::FLOAT},
                                       {"cursor", TypeKind::STRING},
                                       {"display", TypeKind::STRING},
                                       {"visibility", TypeKind::STRING},
                                       {"locked", TypeKind::BOOL},
                                       {"left", TypeKind::STRING},
                                       {"right", TypeKind::STRING},
                                       {"top", TypeKind::STRING},
                                       {"bottom", TypeKind::STRING},
                                       {"centerX", TypeKind::STRING},
                                       {"centerY", TypeKind::STRING},
                                       {"horizontal", TypeKind::STRING},
                                       {"vertical", TypeKind::STRING},
                                       {"minScreen", TypeKind::FLOAT},
                                       {"maxScreen", TypeKind::FLOAT},
                                       {"orientation", TypeKind::STRING},
                                       {"minContainer", TypeKind::FLOAT}});
}

// A polygon is a rectangle with a different outline, so it accepts everything a rectangle
// accepts and the number of sides on top of it. The four named corner radii a rectangle offers
// mean nothing to a ring of sides, so they are dropped here rather than being accepted and then
// quietly ignored: the compiler rejects them.
std::vector<PropertySpec> PolygonProperties() {
    std::vector<PropertySpec> properties;
    for (const PropertySpec& property : RectangleProperties()) {
        const std::string_view name(property.name);
        if (name == "radiusTopLeft" || name == "radiusTopRight" || name == "radiusBottomRight" || name == "radiusBottomLeft") {
            continue;
        }
        properties.push_back(property);
    }
    properties.push_back({"sides", TypeKind::INT});
    return properties;
}

// A star is a polygon whose ring holds two radii instead of one, so it accepts everything a
// polygon accepts. The number of sides a polygon counts says how many corners it has; a star
// counts its points and always has two corners per point, so `sides` is dropped here rather than
// being accepted and then quietly ignored.
std::vector<PropertySpec> StarProperties() {
    std::vector<PropertySpec> properties;
    for (const PropertySpec& property : PolygonProperties()) {
        if (std::string_view(property.name) == "sides") {
            continue;
        }
        properties.push_back(property);
    }
    properties.push_back({"points", TypeKind::INT});
    properties.push_back({"innerRadius", TypeKind::FLOAT});
    return properties;
}

// A boolean group is a shape whose outline is not written down anywhere: it is computed from the
// shapes the group holds. So it accepts the values that place, fill, and stroke a shape, and refuses
// the ones that describe an outline it was not given: there is no side count to count, no corner of
// its own and no radius to round one with, and the four named corner radii mean nothing to an
// outline that arrived already combined. What it adds is the operation that decides which parts of
// its shapes survive.
std::vector<PropertySpec> BooleanProperties() {
    return With(Without(PolygonProperties(), {"sides", "radius", "radiusTopLeft", "radiusTopRight", "radiusBottomRight",
                                               "radiusBottomLeft"}),
                {{"operation", TypeKind::STRING}});
}

// An arrow is a line with a head, so it accepts everything a line accepts and the three
// values that shape the head on top of it. Anything the line rejects, the arrow rejects too.
std::vector<PropertySpec> ArrowProperties() {
    return With(LineProperties(), {{"headLength", TypeKind::FLOAT}, {"headWidth", TypeKind::FLOAT}, {"head", TypeKind::STRING}});
}

std::vector<PropertySpec> ScrollProperties() {
    return With(FrameProperties(), {{"direction", TypeKind::STRING},
                                    {"offset", TypeKind::FLOAT},
                                    {"contentWidth", TypeKind::LENGTH},
                                    {"contentHeight", TypeKind::LENGTH},
                                    {"scrollbar", TypeKind::STRING},
                                    {"scrollbarVisibility", TypeKind::STRING},
                                    {"overscroll", TypeKind::STRING},
                                    {"scrollPhysics", TypeKind::STRING},
                                    {"smoothScrolling", TypeKind::BOOL}});
}

// The values that set a run of text, wherever a run of text is drawn: the font it is written in,
// the size, the weight, the colour, the tracking, and the line drawn through it. They are listed
// once because a block of text and a run inside a block offer the same vocabulary for how text
// looks, so the two cannot drift apart: a value learned on a block is a value a run writes, and a
// value a run writes is a value the block it sits in can set for every run at once. What is not
// here is everything that describes the box and its lines: how wide a block is, where it sits, how
// tall its lines are, and how many of them it holds are questions about a block, and a run inside
// one answers none of them.
std::vector<PropertySpec> RunProperties() {
    return {
        {"text", TypeKind::STRING},
        {"value", TypeKind::STRING},
        {"fontFamily", TypeKind::STRING},
        {"fontSize", TypeKind::FONT_SIZE},
        {"fontWeight", TypeKind::INT},
        {"fontStyle", TypeKind::STRING},
        {"letterSpacing", TypeKind::FLOAT},
        {"wordSpacing", TypeKind::FLOAT},
        {"textColor", TypeKind::COLOR},
        // The tone a run is drawn in, named the way the design system names a colour, so a run can
        // be told apart from the text around it without a colour being written out.
        {"tone", TypeKind::STRING},
        // The two spellings the text subsystem names for what a run of text looks like: the case it
        // is written in and the line drawn through it. They are keywords, so they are read here and
        // folded where they are folded for every other keyword.
        {"textCase", TypeKind::STRING},
        {"textDecoration", TypeKind::STRING},
        {"underline", TypeKind::BOOL},
        {"lineThrough", TypeKind::BOOL},
        {"decorationColor", TypeKind::COLOR},
        {"decorationWidth", TypeKind::FLOAT},
        {"decorationStyle", TypeKind::STRING},
        {"textTransform", TypeKind::STRING},
        {"fontFeatures", TypeKind::STRING},
        {"language", TypeKind::STRING},
    };
}

// A block of typography: everything a box of content takes, everything that sets a run of text, and
// the values that belong to the block alone. So a block of text and a block of rich text take the
// same language, because the only difference between them is how their text is written down: one
// string, or a run per stretch of it.
std::vector<PropertySpec> TypographyProperties() {
    std::vector<PropertySpec> properties = FrameProperties();
    // A box and the text inside it both name the way they are read, and the name is already the
    // block's because a block lays its content out the way it is read. So a value the block already
    // offers is not handed to it a second time.
    for (const PropertySpec& property : RunProperties()) {
        const bool named = std::any_of(properties.begin(), properties.end(), [&](const PropertySpec& candidate) {
            return std::string_view(candidate.name) == property.name;
        });
        if (!named) {
            properties.push_back(property);
        }
    }
    const PropertySpec blockOnly[] = {
        // A font named by the file it is kept in is a question a block answers once for all of its
        // text: the platform draws a stretch of a line in a font of the collection the whole block
        // was laid out in, so a run cannot name a file of its own. The block it sits in can.
        {"fontFile", TypeKind::STRING},
        {"lineHeight", TypeKind::LINE_HEIGHT},
        {"textAlign", TypeKind::STRING},
        {"verticalAlign", TypeKind::STRING},
        // How the block decides the box it covers: the size it was given, a width that hugs one
        // line, or a height that grows with the lines the text wraps into.
        {"sizing", TypeKind::STRING},
        {"maxLines", TypeKind::INT},
        {"ellipsis", TypeKind::BOOL},
        // The line drawn above a block. A run inside a block draws the lines it was given through
        // and under it, and the line above it belongs to the block: it is drawn across the top of
        // the box, which is a place only the block knows.
        {"overline", TypeKind::BOOL},
        {"writingMode", TypeKind::STRING},
        // Which role of the design system the block is drawn as. A role carries how the lines of a
        // block are set, so it is a question about the block rather than about a run inside it.
        {"level", TypeKind::STRING},
        {"size", TypeKind::STRING},
    };
    properties.insert(properties.end(), std::begin(blockOnly), std::end(blockOnly));
    return properties;
}

// A run of text inside a block of text: everything that sets a run, and the two names that let a
// reader point at it. What it refuses is everything that describes a box or the lines of a block:
// a run has no width, no place, no padding, no layout, and no line height of its own, because it is
// part of a line the block around it broke.
std::vector<PropertySpec> SpanProperties() {
    return With({{"id", TypeKind::STRING}, {"name", TypeKind::STRING}}, RunProperties());
}

std::vector<PropertySpec> ShadowProperties() {
    return {
        {"shadow", TypeKind::STRING},
        {"shadowColor", TypeKind::COLOR},
        {"shadowX", TypeKind::FLOAT},
        {"shadowY", TypeKind::FLOAT},
        {"shadowBlur", TypeKind::FLOAT},
        {"shadowSpread", TypeKind::FLOAT},
    };
}

}  // namespace

const std::vector<ComponentSpec>& ComponentCatalog() {
    static const std::vector<ComponentSpec> catalog = {
        {"window", NodeKind::WINDOW, true, WindowProperties()},
        {"board", NodeKind::BOARD, true, FrameProperties()},
        {"frame", NodeKind::FRAME, true, FrameProperties()},
        {"rectangle", NodeKind::RECTANGLE, false, RectangleProperties()},
        {"shadow", NodeKind::SHADOW, false, ShadowProperties()},
        {"text", NodeKind::TEXT, true, TypographyProperties()},
        {"richtext", NodeKind::RICHTEXT, true, TypographyProperties()},
        {"span", NodeKind::SPAN, false, SpanProperties()},
        {"image", NodeKind::IMAGE, false, ImageProperties()},
        {"vector", NodeKind::VECTOR, true, VectorProperties()},
        {"svg", NodeKind::SVG, true, SvgProperties()},
        {"path", NodeKind::PATH, false, PathProperties()},
        {"ellipse", NodeKind::ELLIPSE, false, EllipseProperties(false)},
        {"polygon", NodeKind::POLYGON, false, PolygonProperties()},
        {"star", NodeKind::STAR, false, StarProperties()},
        {"circle", NodeKind::ELLIPSE, false, EllipseProperties(true)},
        {"line", NodeKind::LINE, false, LineProperties()},
        {"arrow", NodeKind::ARROW, false, ArrowProperties()},
        {"boolean", NodeKind::BOOLEAN, true, BooleanProperties()},
        {"group", NodeKind::GROUP, true, GroupProperties()},
        {"component", NodeKind::COMPONENT, true, With(IdentityProperties(), {{"variant", TypeKind::STRING}})},
        {"instance", NodeKind::INSTANCE, true, With(IdentityProperties(), {{"component", TypeKind::STRING}, {"variant", TypeKind::STRING}})},
        {"slot", NodeKind::SLOT, true, With(IdentityProperties(), {{"text", TypeKind::STRING}})},
        {"scroll", NodeKind::SCROLL, true, ScrollProperties()},
    };
    return catalog;
}

const ComponentSpec* FindComponent(std::string_view name) {
    for (const ComponentSpec& spec : ComponentCatalog()) {
        if (name == spec.name) {
            return &spec;
        }
    }
    return nullptr;
}

const PropertySpec* FindInstanceProperty(std::string_view, std::string_view) {
    return nullptr;
}

bool ComponentAcceptsProperty(std::string_view component, std::string_view property) {
    const ComponentSpec* spec = FindComponent(component);
    if (spec == nullptr) {
        // A definition such as style, tokens, or component is not a component, so nothing
        // here can say that an attribute is wrong.
        return true;
    }
    for (const PropertySpec& candidate : spec->properties) {
        if (property == candidate.name) {
            return true;
        }
    }
    return false;
}

}  // namespace Runtime
