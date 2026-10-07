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

}  // namespace Runtime
