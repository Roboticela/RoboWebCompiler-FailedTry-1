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
};

struct CompileResult {
    SourceFile source;
    std::vector<Diagnostic> diagnostics;
    std::optional<IrNode> root;

    bool Ok() const {
        return root.has_value() && !HasFailure(diagnostics);
    }
};

inline const UiValue* FindProperty(const IrNode& node, std::string_view name) {
    for (const auto& property : node.properties) {
        if (property.first == name) {
            return &property.second;
        }
    }
    return nullptr;
}

std::string FormatValue(const UiValue& value);
std::string FormatIr(const IrNode& node);

}  // namespace Runtime
