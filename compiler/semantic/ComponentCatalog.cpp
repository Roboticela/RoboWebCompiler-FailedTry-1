#include "compiler/semantic/ComponentCatalog.h"

namespace Runtime {
namespace {

std::vector<PropertySpec> FrameProperties() {
    return {
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
    };
}

std::vector<PropertySpec> IdentityProperties() {
    return {
        {"id", TypeKind::STRING},
        {"name", TypeKind::STRING},
        {"x", TypeKind::LENGTH},
        {"y", TypeKind::LENGTH},
        {"width", TypeKind::LENGTH},
        {"height", TypeKind::LENGTH},
        {"position", TypeKind::STRING},
        {"opacity", TypeKind::FLOAT},
        {"visible", TypeKind::BOOL},
    };
}

std::vector<PropertySpec> With(std::vector<PropertySpec> base, std::vector<PropertySpec> extra) {
    base.insert(base.end(), extra.begin(), extra.end());
    return base;
}

std::vector<PropertySpec> TextProperties() {
    return With(IdentityProperties(), {{"text", TypeKind::STRING},
                                       {"fontSize", TypeKind::FONT_SIZE},
                                       {"fontWeight", TypeKind::INT},
                                       {"textColor", TypeKind::COLOR},
                                       {"textAlign", TypeKind::STRING}});
}

std::vector<PropertySpec> ImageProperties() {
    return With(IdentityProperties(), {{"source", TypeKind::STRING}, {"fit", TypeKind::STRING}, {"radius", TypeKind::LENGTH}});
}

std::vector<PropertySpec> VectorProperties() {
    return With(IdentityProperties(), {{"shape", TypeKind::STRING},
                                       {"fill", TypeKind::COLOR},
                                       {"stroke", TypeKind::COLOR},
                                       {"strokeWidth", TypeKind::FLOAT},
                                       {"radius", TypeKind::LENGTH}});
}

std::vector<PropertySpec> PathProperties() {
    return With(IdentityProperties(), {{"d", TypeKind::STRING}, {"fill", TypeKind::COLOR}, {"stroke", TypeKind::COLOR}, {"strokeWidth", TypeKind::FLOAT}});
}

std::vector<PropertySpec> LineProperties() {
    return With(IdentityProperties(), {{"x1", TypeKind::FLOAT},
                                       {"y1", TypeKind::FLOAT},
                                       {"x2", TypeKind::FLOAT},
                                       {"y2", TypeKind::FLOAT},
                                       {"stroke", TypeKind::COLOR},
                                       {"strokeWidth", TypeKind::FLOAT}});
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
        {"vector", NodeKind::VECTOR, false, VectorProperties()},
        {"path", NodeKind::PATH, false, PathProperties()},
        {"line", NodeKind::LINE, false, LineProperties()},
        {"group", NodeKind::GROUP, true, IdentityProperties()},
        {"component", NodeKind::COMPONENT, true, IdentityProperties()},
        {"instance", NodeKind::INSTANCE, true, With(IdentityProperties(), {{"component", TypeKind::STRING}})},
        {"slot", NodeKind::SLOT, true, With(IdentityProperties(), {{"text", TypeKind::STRING}})},
        {"scroll", NodeKind::SCROLL, true, FrameProperties()},
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

}  // namespace Runtime
