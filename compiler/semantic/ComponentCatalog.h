#pragma once

#include "engine/types/UiTypes.h"

#include <string_view>
#include <vector>

namespace Runtime {

struct PropertySpec {
    const char* name = "";
    TypeKind type = TypeKind::STRING;
};

struct ComponentSpec {
    const char* name = "";
    NodeKind kind = NodeKind::WINDOW;
    bool acceptsChildren = false;
    std::vector<PropertySpec> properties;
};

const std::vector<ComponentSpec>& ComponentCatalog();
const ComponentSpec* FindComponent(std::string_view name);
const PropertySpec* FindInstanceProperty(std::string_view component, std::string_view property);

// True when the component declares the property, so a writer can stay inside the
// language the compiler accepts instead of producing an element that fails to parse.
// An unknown component has no shape to check against, so everything is accepted.
bool ComponentAcceptsProperty(std::string_view component, std::string_view property);

}  // namespace Runtime
