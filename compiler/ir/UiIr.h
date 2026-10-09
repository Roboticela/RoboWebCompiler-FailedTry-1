#pragma once

#include "engine/core/SourceLocation.h"
#include "engine/theme/VisualStyle.h"
#include "engine/types/UiTypes.h"
#include "xml/diagnostics/Diagnostic.h"

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Runtime {

struct UiValue {
    TypeKind kind = TypeKind::STRING;
    std::string text;
    bool boolean = false;
    double number = 0.0;
    Color color{};
    Length length{};
    Edges edges{};
    Alignment alignment = Alignment::START;
    std::string binding;
    BindMode bindMode = BindMode::ONE_WAY;
    bool invert = false;
};

struct IrNode {
    NodeKind kind = NodeKind::WINDOW;
    std::string name;
    std::string id;
    SourceLocation location;
    std::vector<std::pair<std::string, UiValue>> properties;
    std::vector<IrNode> children;
    VisualStyle visual;
    std::vector<std::pair<std::string, std::string>> states;
    // A definition borrowed from another document. It is part of the tree so that a copy placed from
    // it can be resolved, and it is not built into the live tree: a borrowed definition is a name a
    // document can use, not a box it draws.
    bool fromImport = false;
};

struct CompileResult {
    SourceFile source;
    // Every file the document was read from, the entry file first, so a diagnostic is shown beside
    // the file it was found in rather than beside the file the program was started with.
    std::vector<SourceFile> sources;
    std::vector<Diagnostic> diagnostics;
    std::optional<IrNode> root;

    bool Ok() const {
        return root.has_value() && !HasFailure(diagnostics);
    }
};

enum class DeviceClass {
    MOBILE,
    TABLET,
    DESKTOP,
};

inline constexpr float DEVICE_MOBILE_MAX = 734.0f;
inline constexpr float DEVICE_TABLET_MAX = 1068.0f;

inline DeviceClass DeviceClassForWidth(float width) {
    if (width <= DEVICE_MOBILE_MAX) {
        return DeviceClass::MOBILE;
    }
    if (width <= DEVICE_TABLET_MAX) {
        return DeviceClass::TABLET;
    }
    return DeviceClass::DESKTOP;
}

inline const UiValue* FindProperty(const IrNode& node, std::string_view name) {
    for (const auto& property : node.properties) {
        if (property.first == name) {
            return &property.second;
        }
    }
    return nullptr;
}

inline bool WindowLaunchesOn(const IrNode& node, DeviceClass device) {
    const char* name = "desktop";
    if (device == DeviceClass::MOBILE) {
        name = "mobile";
    } else if (device == DeviceClass::TABLET) {
        name = "tablet";
    }
    const UiValue* value = FindProperty(node, name);
    if (value == nullptr) {
        return true;
    }
    if (value->kind == TypeKind::BOOL) {
        return value->boolean;
    }
    return value->text != "false";
}

std::string FormatValue(const UiValue& value);
std::string FormatIr(const IrNode& node);

}  // namespace Runtime
