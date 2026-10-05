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
        {"justify", TypeKind::STRING},
        {"wrap", TypeKind::STRING},
        {"columns", TypeKind::INT},
        {"rows", TypeKind::INT},
        {"column", TypeKind::INT},
        {"row", TypeKind::INT},
        {"columnSpan", TypeKind::INT},
        {"rowSpan", TypeKind::INT},
        {"background", TypeKind::COLOR},
        {"backgroundColor", TypeKind::COLOR},
        {"gradient", TypeKind::STRING},
        {"image", TypeKind::STRING},
        {"opacity", TypeKind::FLOAT},
        {"borderWidth", TypeKind::LENGTH},
        {"borderColor", TypeKind::COLOR},
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

std::vector<PropertySpec> VectorProperties() {
    return With(IdentityProperties(), {{"shape", TypeKind::STRING},
                                       {"viewBox", TypeKind::STRING},
                                       {"fill", TypeKind::COLOR},
                                       {"stroke", TypeKind::COLOR},
                                       {"strokeWidth", TypeKind::FLOAT},
                                       {"strokeOpacity", TypeKind::FLOAT},
                                       {"lineCap", TypeKind::STRING},
                                       {"lineJoin", TypeKind::STRING},
                                       {"fillRule", TypeKind::STRING},
                                       {"transform", TypeKind::STRING},
                                       {"radius", TypeKind::LENGTH}});
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

std::vector<PropertySpec> LineProperties() {
    return With(IdentityProperties(), {{"x1", TypeKind::FLOAT},
                                       {"y1", TypeKind::FLOAT},
                                       {"x2", TypeKind::FLOAT},
                                       {"y2", TypeKind::FLOAT},
                                       {"stroke", TypeKind::COLOR},
                                       {"strokeWidth", TypeKind::FLOAT},
                                       {"lineCap", TypeKind::STRING},
                                       {"dash", TypeKind::STRING},
                                       {"dashGap", TypeKind::FLOAT}});
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
        {"window", NodeKind::WINDOW, true, {{"id", TypeKind::STRING}, {"title", TypeKind::STRING}, {"width", TypeKind::LENGTH}, {"height", TypeKind::LENGTH}, {"background", TypeKind::COLOR}}},
        {"frame", NodeKind::FRAME, true, FrameProperties()},
        {"shadow", NodeKind::SHADOW, false, ShadowProperties()},
        {"text", NodeKind::TEXT, false, TextProperties()},
        {"image", NodeKind::IMAGE, false, ImageProperties()},
        {"vector", NodeKind::VECTOR, true, VectorProperties()},
        {"path", NodeKind::PATH, false, PathProperties()},
        {"line", NodeKind::LINE, false, LineProperties()},
        {"group", NodeKind::GROUP, true, With(IdentityProperties(), {{"transform", TypeKind::STRING}, {"rotation", TypeKind::FLOAT}, {"scale", TypeKind::FLOAT}, {"visibility", TypeKind::STRING}})},
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

const PropertySpec* FindInstanceProperty(std::string_view component, std::string_view property) {
    if (component != "Button") {
        return nullptr;
    }
    static const PropertySpec buttonProperties[] = {
        {"variant", TypeKind::STRING}, {"size", TypeKind::STRING},     {"label", TypeKind::STRING},
        {"disabled", TypeKind::BOOL},  {"loading", TypeKind::BOOL},    {"fullWidth", TypeKind::BOOL},
        {"onClick", TypeKind::EVENT_HANDLER},
    };
    for (const PropertySpec& spec : buttonProperties) {
        if (property == spec.name) {
            return &spec;
        }
    }
    return nullptr;
}

}  // namespace Runtime
