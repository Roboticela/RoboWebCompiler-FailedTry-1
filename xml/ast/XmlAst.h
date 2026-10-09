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
    // Set on the root of a subtree that was placed by naming a component. Such a subtree stands in
    // place of the definition it was placed from, so the ids it carries name the definition's layers
    // rather than making new ones, and they are allowed to repeat the ids of the definition.
    bool fromDefinition = false;
    // Set on a definition that was borrowed from another document by an import. Such a definition is
    // read and registered like any other, but it is not drawn: it belongs to the document it was
    // written in, and importing it is what makes its name usable here rather than what places it.
    bool fromImport = false;
};

}  // namespace Runtime
