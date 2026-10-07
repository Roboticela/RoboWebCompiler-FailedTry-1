#include "components/svg/SvgElement.h"

namespace Runtime {

NodeKind SvgElement::Kind() const {
    return NodeKind::SVG;
}

void SvgElement::SetSource(std::string value) {
    source = std::move(value);
}

const std::string& SvgElement::Source() const {
    return source;
}

void SvgElement::ApplyBound(const std::string& property, const std::string& value) {
    VectorElement::ApplyBound(property, value);
    // The file is read once, when the node is built, so a binding keeps the new name on the node
    // and the art it was built from stays where it is until the node is built again.
    if (property == "source") {
        source = value;
    }
}

}  // namespace Runtime
