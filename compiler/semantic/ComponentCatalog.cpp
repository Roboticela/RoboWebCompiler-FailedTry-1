#include "compiler/semantic/ComponentCatalog.h"

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
// describes a box of content: no layout, no padding, no gap, no overflow, no scrolling.
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

std::vector<PropertySpec> ImageProperties() {
    return With(IdentityProperties(), {{"source", TypeKind::STRING},
                                       {"fit", TypeKind::STRING},
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

// An SVG is a shape whose art comes from a file rather than from the document, so it takes the
// values that describe a shape and the file it draws, and nothing that describes the geometry
// itself: the paths, the fill rule and the line style are the file's, not the node's.
std::vector<PropertySpec> SvgProperties() {
    return With(RectangleProperties(), {{"source", TypeKind::STRING}, {"viewBox", TypeKind::STRING}});
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
                                                                       {"backgroundColor", TypeKind::COLOR},
                                                                       {"stroke", TypeKind::COLOR},
                                                                       {"strokeWidth", TypeKind::FLOAT},
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

std::vector<PropertySpec> TypographyProperties() {
    std::vector<PropertySpec> properties = FrameProperties();
    const PropertySpec extra[] = {
        {"text", TypeKind::STRING},
        {"value", TypeKind::STRING},
        {"fontFamily", TypeKind::STRING},
        {"fontFile", TypeKind::STRING},
        {"fontSize", TypeKind::FONT_SIZE},
        {"fontWeight", TypeKind::INT},
        {"fontStyle", TypeKind::STRING},
        {"letterSpacing", TypeKind::FLOAT},
        {"wordSpacing", TypeKind::FLOAT},
        {"lineHeight", TypeKind::FLOAT},
        {"textColor", TypeKind::COLOR},
        {"textAlign", TypeKind::STRING},
        {"verticalAlign", TypeKind::STRING},
        {"maxLines", TypeKind::INT},
        {"ellipsis", TypeKind::BOOL},
        {"underline", TypeKind::BOOL},
        {"lineThrough", TypeKind::BOOL},
        {"overline", TypeKind::BOOL},
        {"decorationColor", TypeKind::COLOR},
        {"decorationWidth", TypeKind::FLOAT},
        {"decorationStyle", TypeKind::STRING},
        {"textTransform", TypeKind::STRING},
        {"fontFeatures", TypeKind::STRING},
        {"language", TypeKind::STRING},
        {"writingMode", TypeKind::STRING},
        {"tone", TypeKind::STRING},
        {"level", TypeKind::STRING},
        {"size", TypeKind::STRING},
    };
    properties.insert(properties.end(), std::begin(extra), std::end(extra));
    return properties;
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
        {"image", NodeKind::IMAGE, false, ImageProperties()},
        {"vector", NodeKind::VECTOR, true, VectorProperties()},
        {"svg", NodeKind::SVG, false, SvgProperties()},
        {"path", NodeKind::PATH, false, PathProperties()},
        {"ellipse", NodeKind::ELLIPSE, false, EllipseProperties(false)},
        {"polygon", NodeKind::POLYGON, false, PolygonProperties()},
        {"star", NodeKind::STAR, false, StarProperties()},
        {"circle", NodeKind::ELLIPSE, false, EllipseProperties(true)},
        {"line", NodeKind::LINE, false, LineProperties()},
        {"arrow", NodeKind::ARROW, false, ArrowProperties()},
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
