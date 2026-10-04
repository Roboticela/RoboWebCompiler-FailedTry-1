#pragma once

#include "engine/core/SourceLocation.h"

#include <string>
#include <vector>

namespace Runtime {

struct XmlAttribute {
    std::string name;
    std::string value;
    SourceLocation nameLocation;
    SourceLocation valueLocation;
    int valueSpan = 1;
};

struct XmlNode {
    std::string name;
    SourceLocation location;
    std::vector<XmlAttribute> attributes;
    std::vector<XmlNode> children;
};

}  // namespace Runtime
