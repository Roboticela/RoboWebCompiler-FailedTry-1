#include "compiler/semantic/UiCompiler.h"

#include "compiler/semantic/ComponentCatalog.h"
#include "compiler/semantic/DocumentPass.h"
#include "compiler/semantic/Suggestions.h"
#include "compiler/types/ValueParser.h"
#include "xml/parser/XmlParser.h"

#include <unordered_set>

namespace Runtime {
namespace {

void AddDiagnostic(std::vector<Diagnostic>& diagnostics, std::string code, std::string message, SourceLocation location,
                   int span) {
    Diagnostic diagnostic;
    diagnostic.severity = DiagnosticSeverity::FAILURE;
    diagnostic.code = std::move(code);
    diagnostic.message = std::move(message);
    diagnostic.location = std::move(location);
    diagnostic.spanLength = span;
    diagnostics.push_back(std::move(diagnostic));
}

std::string JoinNames(const std::vector<std::string>& names) {
    std::string joined;
    for (std::size_t index = 0; index < names.size(); ++index) {
        if (index > 0) {
            joined += ", ";
        }
        joined += names[index];
    }
    return joined;
}

bool ParseProperty(const PropertySpec& spec, const XmlAttribute& attribute, UiValue& value,
                   std::vector<Diagnostic>& diagnostics) {
    const std::string_view raw = attribute.value;
    if (IsBindingExpression(raw)) {
        if (spec.type != TypeKind::STRING && spec.type != TypeKind::BOOL) {
            AddDiagnostic(diagnostics, DIAGNOSTIC_BINDING, "Bindings are supported for String and Bool properties.",
                          attribute.valueLocation, attribute.valueSpan);
            return false;
        }
        value.kind = spec.type;
        value.binding = BindingKey(raw);
        value.bindMode = BindingModeOf(raw);
        value.text.clear();
        value.boolean = false;
        return true;
    }
    switch (spec.type) {
    case TypeKind::STRING: {
        value.kind = TypeKind::STRING;
        value.text = std::string(TrimCopyView(raw));
        const char* const* choices = nullptr;
        int choiceCount = 0;
        if (attribute.name == "layout") {
            static const char* options[] = {"none", "horizontal", "vertical", "grid", "wrap", "stack"};
            choices = options;
            choiceCount = 6;
        } else if (attribute.name == "position") {
            static const char* options[] = {"flow", "absolute", "fixed", "sticky"};
            choices = options;
            choiceCount = 4;
        } else if (attribute.name == "widthMode" || attribute.name == "heightMode") {
            static const char* options[] = {"fixed", "fill", "fit", "percent"};
            choices = options;
            choiceCount = 4;
        } else if (attribute.name == "anchor") {
            static const char* options[] = {"top", "right", "bottom", "left", "center"};
            choices = options;
            choiceCount = 5;
        } else if (attribute.name == "justify") {
            static const char* options[] = {"start", "center", "end", "left", "right", "top", "bottom", "stretch", "spaceBetween"};
            choices = options;
            choiceCount = 9;
        } else if (attribute.name == "wrap") {
            static const char* options[] = {"nowrap", "wrap"};
            choices = options;
            choiceCount = 2;
        } else if (attribute.name == "overflow") {
            static const char* options[] = {"visible", "hidden", "scroll", "auto"};
            choices = options;
            choiceCount = 4;
        } else if (attribute.name == "display") {
            static const char* options[] = {"normal", "none"};
            choices = options;
            choiceCount = 2;
        } else if (attribute.name == "borderStyle") {
            static const char* options[] = {"solid", "dashed", "dotted", "none"};
            choices = options;
            choiceCount = 4;
        } else if (attribute.name == "blendMode") {
            static const char* options[] = {"normal", "multiply", "screen", "overlay", "darken", "lighten"};
            choices = options;
            choiceCount = 6;
        } else if (attribute.name == "cursor") {
            static const char* options[] = {"default", "pointer", "text", "grab", "grabbing", "resize", "crosshair", "notAllowed"};
            choices = options;
            choiceCount = 8;
        } else if (attribute.name == "visibility") {
            static const char* options[] = {"visible", "hidden"};
            choices = options;
            choiceCount = 2;
        } else if (attribute.name == "textAlign") {
            static const char* options[] = {"left", "center", "right", "justify"};
            choices = options;
            choiceCount = 4;
        } else if (attribute.name == "verticalAlign") {
            static const char* options[] = {"top", "center", "bottom"};
            choices = options;
            choiceCount = 3;
        } else if (attribute.name == "fontStyle") {
            static const char* options[] = {"normal", "italic", "oblique"};
            choices = options;
            choiceCount = 3;
        } else if (attribute.name == "textTransform") {
            static const char* options[] = {"none", "uppercase", "lowercase", "capitalize"};
            choices = options;
            choiceCount = 4;
        } else if (attribute.name == "decorationStyle") {
            static const char* options[] = {"solid", "dashed", "dotted", "double", "none"};
            choices = options;
            choiceCount = 5;
        } else if (attribute.name == "direction") {
            static const char* options[] = {"ltr", "rtl", "scrollX", "scrollY", "scrollBoth", "horizontal", "vertical"};
            choices = options;
            choiceCount = 7;
        } else if (attribute.name == "writingMode") {
            static const char* options[] = {"horizontal", "vertical"};
            choices = options;
            choiceCount = 2;
        } else if (attribute.name == "fit") {
            static const char* options[] = {"fill", "contain", "cover", "none", "scaleDown"};
            choices = options;
            choiceCount = 5;
        } else if (attribute.name == "lineCap") {
            static const char* options[] = {"butt", "round", "square"};
            choices = options;
            choiceCount = 3;
        } else if (attribute.name == "lineJoin") {
            static const char* options[] = {"miter", "round", "bevel"};
            choices = options;
            choiceCount = 3;
        } else if (attribute.name == "fillRule") {
            static const char* options[] = {"nonzero", "evenodd"};
            choices = options;
            choiceCount = 2;
        } else if (attribute.name == "scrollbar" || attribute.name == "scrollbarVisibility") {
            static const char* options[] = {"auto", "visible", "hidden"};
            choices = options;
            choiceCount = 3;
        } else if (attribute.name == "overscroll") {
            static const char* options[] = {"auto", "contain", "none"};
            choices = options;
            choiceCount = 3;
        } else if (attribute.name == "scrollPhysics") {
            static const char* options[] = {"clamping", "bouncing"};
            choices = options;
            choiceCount = 2;
        }
        if (choices != nullptr) {
            bool matched = false;
            for (int index = 0; index < choiceCount; ++index) {
                if (value.text == choices[index]) {
                    matched = true;
                    break;
                }
            }
            if (!matched) {
                AddDiagnostic(diagnostics, DIAGNOSTIC_TYPE_MISMATCH,
                              "Property '" + attribute.name + "' has an unknown value '" + value.text + "'.",
                              attribute.valueLocation, attribute.valueSpan);
                return false;
            }
        }
        return true;
    }
    case TypeKind::INT: {
        const std::optional<int> parsed = attribute.name == "zIndex" ? ParseZIndex(raw) : ParseInteger(raw);
        if (!parsed.has_value()) {
            AddDiagnostic(diagnostics, DIAGNOSTIC_TYPE_MISMATCH,
                          "Property '" + attribute.name + "' expects Int. Received String.", attribute.valueLocation,
                          attribute.valueSpan);
            return false;
        }
        if (attribute.name == "columns" && *parsed < 1) {
            AddDiagnostic(diagnostics, DIAGNOSTIC_VALUE_RANGE, "Property 'columns' must be at least 1.", attribute.valueLocation,
                          attribute.valueSpan);
            return false;
        }
        value.kind = TypeKind::INT;
        value.number = *parsed;
        return true;
    }
    case TypeKind::BOOL: {
        auto parsed = ParseBool(raw);
        if (!parsed.has_value()) {
            AddDiagnostic(diagnostics, DIAGNOSTIC_TYPE_MISMATCH,
                          "Property '" + attribute.name + "' expects Bool. Received String.", attribute.valueLocation,
                          attribute.valueSpan);
            return false;
        }
        value.kind = TypeKind::BOOL;
        value.boolean = *parsed;
        return true;
    }
    case TypeKind::LENGTH: {
        auto parsed = ParseLength(raw);
        if (!parsed.has_value()) {
            AddDiagnostic(diagnostics, DIAGNOSTIC_TYPE_MISMATCH,
                          "Property '" + attribute.name + "' expects Length. Received String.", attribute.valueLocation,
                          attribute.valueSpan);
            return false;
        }
        const bool offset = attribute.name == "x" || attribute.name == "y";
        if (!offset && parsed->unit == LengthUnit::PIXELS && parsed->value < 0.0f) {
            AddDiagnostic(diagnostics, DIAGNOSTIC_VALUE_RANGE,
                          "Property '" + attribute.name + "' must be greater than or equal to 0.",
                          attribute.valueLocation, attribute.valueSpan);
            return false;
        }
        value.kind = TypeKind::LENGTH;
        value.length = *parsed;
        return true;
    }
    case TypeKind::FONT_SIZE: {
        auto parsed = ParseFontSize(raw);
        if (!parsed.has_value()) {
            AddDiagnostic(diagnostics, DIAGNOSTIC_TYPE_MISMATCH,
                          "Property '" + attribute.name + "' expects FontSize. Received String.",
                          attribute.valueLocation, attribute.valueSpan);
            return false;
        }
        if (*parsed < 0.0f) {
            AddDiagnostic(diagnostics, DIAGNOSTIC_VALUE_RANGE,
                          "Property '" + attribute.name + "' must be greater than or equal to 0.",
                          attribute.valueLocation, attribute.valueSpan);
            return false;
        }
        value.kind = TypeKind::FONT_SIZE;
        value.number = *parsed;
        return true;
    }
    case TypeKind::COLOR: {
        auto parsed = ParseColor(raw);
        if (!parsed.has_value()) {
            AddDiagnostic(diagnostics, DIAGNOSTIC_TYPE_MISMATCH,
                          "Property '" + attribute.name + "' expects Color. Received String.", attribute.valueLocation,
                          attribute.valueSpan);
            return false;
        }
        value.kind = TypeKind::COLOR;
        value.color = *parsed;
        return true;
    }
    case TypeKind::ALIGNMENT: {
        auto parsed = ParseAlignment(raw);
        if (!parsed.has_value()) {
            AddDiagnostic(diagnostics, DIAGNOSTIC_TYPE_MISMATCH,
                          "Property '" + attribute.name + "' expects Alignment. Received String.",
                          attribute.valueLocation, attribute.valueSpan);
            return false;
        }
        value.kind = TypeKind::ALIGNMENT;
        value.alignment = *parsed;
        return true;
    }
    case TypeKind::EVENT_HANDLER:
        if (!IsEventName(raw) && !IsAction(raw)) {
            AddDiagnostic(diagnostics, DIAGNOSTIC_EVENT_SIGNATURE,
                          "Event handler '" + std::string(TrimCopyView(raw)) + "' has an incompatible signature.",
                          attribute.valueLocation, attribute.valueSpan);
            return false;
        }
        value.kind = TypeKind::EVENT_HANDLER;
        value.text = std::string(TrimCopyView(raw));
        return true;
    case TypeKind::FLOAT: {
        const std::optional<float> parsed = ParseFloat(raw);
        if (!parsed.has_value()) {
            AddDiagnostic(diagnostics, DIAGNOSTIC_TYPE_MISMATCH,
                          "Property '" + attribute.name + "' expects Float. Received String.", attribute.valueLocation,
                          attribute.valueSpan);
            return false;
        }
        if ((attribute.name == "opacity" || attribute.name == "ratio") && (*parsed < 0.0f || *parsed > 1.0f)) {
            AddDiagnostic(diagnostics, DIAGNOSTIC_VALUE_RANGE, "Property '" + attribute.name + "' must be from 0 to 1.",
                          attribute.valueLocation, attribute.valueSpan);
            return false;
        }
        value.kind = TypeKind::FLOAT;
        value.number = *parsed;
        return true;
    }
    case TypeKind::EDGES: {
        const std::optional<Edges> parsed = ParseEdges(raw);
        if (!parsed.has_value()) {
            AddDiagnostic(diagnostics, DIAGNOSTIC_TYPE_MISMATCH,
                          "Property '" + attribute.name + "' expects Edges. Received String.", attribute.valueLocation,
                          attribute.valueSpan);
            return false;
        }
        value.kind = TypeKind::EDGES;
        value.edges = *parsed;
        return true;
    }
    }
    AddDiagnostic(diagnostics, DIAGNOSTIC_TYPE_MISMATCH,
                  "Property '" + attribute.name + "' expects " + TypeKindName(spec.type) + ".", attribute.valueLocation,
                  attribute.valueSpan);
    return false;
}

std::optional<IrNode> Lower(const XmlNode& node, std::vector<Diagnostic>& diagnostics, std::unordered_set<std::string>& ids,
                            const DocumentModel& model, int depth) {
    if (depth > MAX_TREE_DEPTH) {
        AddDiagnostic(diagnostics, DIAGNOSTIC_TREE_DEPTH, "Element nesting exceeds the supported depth.", node.location,
                      static_cast<int>(node.name.size()));
        return std::nullopt;
    }
    const ComponentSpec* spec = FindComponent(node.name);
    if (spec == nullptr) {
        std::vector<std::string> names;
        for (const ComponentSpec& candidate : ComponentCatalog()) {
            names.emplace_back(candidate.name);
        }
        std::string message = "Unknown component '" + node.name + "'.";
        if (auto suggestion = SuggestName(node.name, names)) {
            message += " Did you mean '" + *suggestion + "'?";
        } else {
            message += " Known components: " + JoinNames(names) + ".";
        }
        AddDiagnostic(diagnostics, DIAGNOSTIC_UNKNOWN_COMPONENT, std::move(message), node.location,
                      static_cast<int>(node.name.size()));
        return std::nullopt;
    }

    IrNode lowered;
    lowered.kind = spec->kind;
    lowered.name = spec->name;
    lowered.location = node.location;

    std::unordered_set<std::string> seen;
    for (const XmlAttribute& attribute : node.attributes) {
        std::string propertyName = attribute.name;
        const PropertySpec* property = nullptr;
        for (const PropertySpec& candidate : spec->properties) {
            if (candidate.name == propertyName) {
                property = &candidate;
                break;
            }
        }
        if (property == nullptr && spec->kind == NodeKind::INSTANCE) {
            UiValue extra;
            extra.kind = TypeKind::STRING;
            extra.text = std::string(TrimCopyView(attribute.value));
            lowered.properties.emplace_back(attribute.name, std::move(extra));
            continue;
        }
        if (property == nullptr) {
            std::vector<std::string> names;
            for (const PropertySpec& candidate : spec->properties) {
                names.emplace_back(candidate.name);
            }
            std::string message = "Unknown property '" + attribute.name + "' on component '" + node.name + "'.";
            if (auto suggestion = SuggestName(attribute.name, names)) {
                message += " Did you mean '" + *suggestion + "'?";
            } else {
                message += " Known properties: " + JoinNames(names) + ".";
            }
            AddDiagnostic(diagnostics, DIAGNOSTIC_UNKNOWN_PROPERTY, std::move(message), attribute.nameLocation,
                          static_cast<int>(attribute.name.size()));
            continue;
        }
        std::string canonical = propertyName;
        if (canonical == "value") {
            canonical = "text";
        }
        if (!seen.insert(canonical).second) {
            AddDiagnostic(diagnostics, DIAGNOSTIC_DUPLICATE_PROPERTY,
                          "Property '" + canonical + "' is set more than once.", attribute.nameLocation,
                          static_cast<int>(attribute.name.size()));
            continue;
        }
        UiValue value;
        if (!ParseProperty(*property, attribute, value, diagnostics)) {
            continue;
        }
        if (spec->kind == NodeKind::WINDOW && (canonical == "width" || canonical == "height") &&
            value.length.unit != LengthUnit::PIXELS) {
            AddDiagnostic(diagnostics, DIAGNOSTIC_TYPE_MISMATCH,
                          "Property '" + canonical + "' on window expects a pixel Length.", attribute.valueLocation,
                          attribute.valueSpan);
            continue;
        }
        if (canonical == "id") {
            if (!value.text.empty() && !ids.insert(value.text).second) {
                AddDiagnostic(diagnostics, DIAGNOSTIC_DUPLICATE_ID, "Duplicate id '" + value.text + "'.",
                              attribute.valueLocation, attribute.valueSpan);
            }
            lowered.id = value.text;
        }
        lowered.properties.emplace_back(canonical, std::move(value));
    }
    if (const UiValue* styleName = FindProperty(lowered, "style")) {
        std::string key = styleName->text;
        if (const UiValue* variant = FindProperty(lowered, "variant")) {
            const auto varied = model.styles.find(key + "/" + variant->text);
            if (varied != model.styles.end()) {
                key += "/" + variant->text;
            }
        }
        const auto found = model.styles.find(key);
        if (found == model.styles.end()) {
            AddDiagnostic(diagnostics, DIAGNOSTIC_UNKNOWN_PROPERTY, "Unknown style '" + key + "'.", node.location,
                          static_cast<int>(node.name.size()));
        } else {
            lowered.visual = found->second;
        }
    }
    if (depth == 1) {
        lowered.states = model.states;
    }

    if (!spec->acceptsChildren && !node.children.empty()) {
        AddDiagnostic(diagnostics, DIAGNOSTIC_UNEXPECTED_CHILD,
                      "Component '" + node.name + "' cannot contain child elements.", node.children.front().location,
                      static_cast<int>(node.children.front().name.size()));
    } else {
        for (const XmlNode& child : node.children) {
            auto loweredChild = Lower(child, diagnostics, ids, model, depth + 1);
            if (loweredChild.has_value()) {
                lowered.children.push_back(std::move(*loweredChild));
            }
        }
    }
    return lowered;
}

}  // namespace

CompileResult CompileUi(const SourceFile& source) {
    CompileResult result;
    result.source = source;
    ParseResult parsed = ParseXml(source);
    result.diagnostics = std::move(parsed.diagnostics);
    if (!parsed.root.has_value() || HasFailure(result.diagnostics)) {
        return result;
    }
    if (parsed.root->name != "window") {
        AddDiagnostic(result.diagnostics, DIAGNOSTIC_ROOT_WINDOW, "The root element must be 'window'.",
                      parsed.root->location, static_cast<int>(parsed.root->name.size()));
        return result;
    }
    DocumentModel model;
    if (!PrepareDocument(*parsed.root, model, result.diagnostics)) {
        return result;
    }
    std::unordered_set<std::string> ids;
    const std::size_t before = result.diagnostics.size();
    auto lowered = Lower(*parsed.root, result.diagnostics, ids, model, 1);
    if (result.diagnostics.size() == before && lowered.has_value()) {
        for (const AnimationSpec& animation : model.animations) {
            lowered->visual.motions.push_back(MotionFromAnimation(animation));
        }
        result.root = std::move(*lowered);
    }
    return result;
}

CompileResult CompileUiFile(const std::filesystem::path& path) {
    std::string error;
    auto source = ReadSourceFile(path, error);
    if (!source.has_value()) {
        CompileResult result;
        result.source.path = path.string();
        AddDiagnostic(result.diagnostics, DIAGNOSTIC_CANNOT_READ_FILE, error, SourceLocation{path.string(), 0, 0}, 1);
        return result;
    }
    return CompileUi(*source);
}

}  // namespace Runtime
