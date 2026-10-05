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

}  // namespace Runtime
