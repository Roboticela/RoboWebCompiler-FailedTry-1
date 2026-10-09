#include "compiler/semantic/UiCompiler.h"

#include "compiler/semantic/ComponentCatalog.h"
#include "compiler/semantic/DocumentPass.h"
#include "compiler/semantic/Foundations.h"
#include "compiler/semantic/Suggestions.h"
#include "compiler/types/ValueParser.h"
#include "xml/parser/XmlParser.h"
#include "xml/project/ProjectLoader.h"

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
        if (spec.type != TypeKind::STRING && spec.type != TypeKind::BOOL && spec.type != TypeKind::COLOR) {
            AddDiagnostic(diagnostics, DIAGNOSTIC_BINDING, "Bindings are supported for String, Bool, and Color properties.",
                          attribute.valueLocation, attribute.valueSpan);
            return false;
        }
        value.kind = spec.type;
        value.binding = BindingKey(raw);
        value.bindMode = BindingModeOf(raw);
        value.invert = spec.type == TypeKind::BOOL && value.bindMode == BindMode::ONE_TIME;
        if (value.invert) {
            value.bindMode = BindMode::REACTIVE;
        }
        value.text.clear();
        value.boolean = false;
        return true;
    }
    switch (spec.type) {
    case TypeKind::STRING: {
        value.kind = TypeKind::STRING;
        // The text a document writes is the text it gets. Nothing in a string has to be read the way
        // a number or a colour is, so nothing in it has to be taken off either: a run of text that
        // ends a word keeps the space separating it from the run after it, and without it a paragraph
        // of runs glues its words together. The values a property chooses between are keywords rather
        // than text, so those are matched with the whitespace around them taken off.
        value.text = std::string(raw);
        const std::string_view word = TrimCopyView(raw);
        const char* const* choices = nullptr;
        int choiceCount = 0;
        if (attribute.name == "layout") {
            static const char* options[] = {"none", "horizontal", "vertical", "grid", "wrap", "stack", "inline"};
            choices = options;
            choiceCount = 7;
        } else if (attribute.name == "position") {
            static const char* options[] = {"flow", "absolute", "fixed", "sticky"};
            choices = options;
            choiceCount = 4;
        } else if (attribute.name == "widthMode" || attribute.name == "heightMode") {
            static const char* options[] = {"fixed", "fill", "fit", "hug", "percent", "FIXED", "FILL", "HUG"};
            choices = options;
            choiceCount = 8;
        } else if (attribute.name == "anchor") {
            static const char* options[] = {"top", "right", "bottom", "left", "center"};
            choices = options;
            choiceCount = 5;
        } else if (attribute.name == "alignItems") {
            static const char* options[] = {"start", "center", "end", "stretch", "START", "CENTER", "END", "STRETCH"};
            choices = options;
            choiceCount = 8;
        } else if (attribute.name == "justifyContent") {
            static const char* options[] = {"start", "center", "end", "spaceBetween", "START", "CENTER", "END", "SPACE_BETWEEN"};
            choices = options;
            choiceCount = 8;
        } else if (attribute.name == "horizontal") {
            static const char* options[] = {"left", "right", "leftRight", "center", "scale", "LEFT", "RIGHT", "LEFT_RIGHT", "CENTER", "SCALE"};
            choices = options;
            choiceCount = 10;
        } else if (attribute.name == "vertical") {
            static const char* options[] = {"top", "bottom", "topBottom", "center", "scale", "TOP", "BOTTOM", "TOP_BOTTOM", "CENTER", "SCALE"};
            choices = options;
            choiceCount = 10;
        } else if (attribute.name == "justify") {
            static const char* options[] = {"start", "center", "end", "left", "right", "top", "bottom", "stretch", "spaceBetween"};
            choices = options;
            choiceCount = 9;
        } else if (attribute.name == "wrap") {
            static const char* options[] = {"nowrap", "wrap", "true", "false"};
            choices = options;
            choiceCount = 4;
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
            static const char* options[] = {"left", "center", "right", "justify", "start", "end"};
            choices = options;
            choiceCount = 6;
        } else if (attribute.name == "verticalAlign") {
            static const char* options[] = {"top", "center", "bottom", "start", "end"};
            choices = options;
            choiceCount = 5;
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
        } else if (attribute.name == "target") {
            // Where a link is opened: in the place it stands, beside it, or in a window of its own.
            static const char* options[] = {"self", "blank", "window"};
            choices = options;
            choiceCount = 3;
        }
        if (choices != nullptr) {
            bool matched = false;
            for (int index = 0; index < choiceCount; ++index) {
                if (word == choices[index]) {
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
            // A value chosen out of a set is the word of the set rather than the spacing it was
            // written with, so everything that reads the word reads the same one.
            value.text = std::string(word);
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
    case TypeKind::LINE_HEIGHT: {
        auto parsed = ParseLineHeight(raw);
        if (!parsed.has_value()) {
            AddDiagnostic(diagnostics, DIAGNOSTIC_TYPE_MISMATCH,
                          "Property '" + attribute.name + "' expects LineHeight. Received String.",
                          attribute.valueLocation, attribute.valueSpan);
            return false;
        }
        if (*parsed < 0.0f) {
            AddDiagnostic(diagnostics, DIAGNOSTIC_VALUE_RANGE,
                          "Property '" + attribute.name + "' must be greater than or equal to 0.",
                          attribute.valueLocation, attribute.valueSpan);
            return false;
        }
        value.kind = TypeKind::LINE_HEIGHT;
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

// `idIsReference` says the node being read is a child of a copy of a component. Such a child stands
// in place of a layer of the definition it was placed from, so the id it carries names that layer
// rather than making a second layer of its own, and it is allowed to repeat the id it replaces.
std::optional<IrNode> Lower(const XmlNode& node, std::vector<Diagnostic>& diagnostics, std::unordered_set<std::string>& ids,
                            const DocumentModel& model, int depth, bool idIsReference = false) {
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
    lowered.fromImport = node.fromImport;

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
            std::string componentName;
            for (const XmlAttribute& candidate : node.attributes) {
                if (candidate.name == "component") {
                    componentName = std::string(TrimCopyView(candidate.value));
                    break;
                }
            }
            property = FindInstanceProperty(componentName, attribute.name);
            if (property == nullptr) {
                UiValue extra;
                extra.kind = TypeKind::STRING;
                extra.text = std::string(TrimCopyView(attribute.value));
                lowered.properties.emplace_back(attribute.name, std::move(extra));
                continue;
            }
        }
        if (property == nullptr && propertyName == "slot") {
            // A child is sent to a place inside the definition its parent was placed from by naming
            // that place. Which places a definition offers is only known once the definition is
            // resolved, so the name is read as the part of the component language it is rather than
            // refused on every component that could hold it.
            UiValue slot;
            slot.kind = TypeKind::STRING;
            slot.text = std::string(TrimCopyView(attribute.value));
            lowered.properties.emplace_back("slot", std::move(slot));
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
        if (canonical == "value" && spec->kind == NodeKind::TEXT) {
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
        if (canonical == "id") {
            if (!value.text.empty() && !idIsReference && !ids.insert(value.text).second) {
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
    if (spec->kind == NodeKind::LINK) {
        // A link that names nowhere is a shape that looks like one and does nothing, so it is
        // refused. A link that names a screen is checked against the screens the document holds, so
        // a name that leads nowhere is reported where it was written rather than doing nothing when
        // it is clicked.
        const UiValue* href = FindProperty(lowered, "href");
        const UiValue* to = FindProperty(lowered, "to");
        const bool hasHref = href != nullptr && !TrimCopyView(href->text).empty();
        const bool hasTo = to != nullptr && !TrimCopyView(to->text).empty();
        if (!hasHref && !hasTo) {
            AddDiagnostic(diagnostics, DIAGNOSTIC_LINK_TARGET, "A link needs an href or a to.", node.location,
                          static_cast<int>(node.name.size()));
        } else if (hasTo && model.screenIds.count(std::string(TrimCopyView(to->text))) == 0) {
            AddDiagnostic(diagnostics, DIAGNOSTIC_LINK_UNKNOWN,
                          "A link points at '" + std::string(TrimCopyView(to->text)) +
                              "', which is not a screen of this document.",
                          node.location, static_cast<int>(node.name.size()));
        }
    }

    if ((node.name == "window" || node.name == "board") && depth > 1) {
        const char* message = node.name == "board" ? "A board is independent and cannot be nested."
                                                    : "A window cannot contain another window.";
        AddDiagnostic(diagnostics, DIAGNOSTIC_UNEXPECTED_CHILD, message, node.location, static_cast<int>(node.name.size()));
        return std::nullopt;
    }

    if (!spec->acceptsChildren && !node.children.empty()) {
        AddDiagnostic(diagnostics, DIAGNOSTIC_UNEXPECTED_CHILD,
                      "Component '" + node.name + "' cannot contain child elements.", node.children.front().location,
                      static_cast<int>(node.children.front().name.size()));
    } else {
        // The children of an element placed from a definition are the definition's layers, so the ids
        // they carry name those layers rather than making new ones, the same way the children of a
        // copy do. An embedded document's content is that document's, so the names in it are its own
        // as well. A subtree once entered as a reference stays one all the way down, so the children
        // of such a child are references too.
        const bool childIsReference =
            idIsReference || node.fromDefinition || spec->kind == NodeKind::INSTANCE || spec->kind == NodeKind::EMBED;
        for (const XmlNode& child : node.children) {
            auto loweredChild = Lower(child, diagnostics, ids, model, depth + 1, childIsReference);
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
    // A document may be written across several files: every `import` is replaced by what it borrowed
    // and every `embed` is inlined, so what is compiled is one tree made of several files. Each file
    // is named beside the document that wrote it, so a document split across a folder of files reads
    // as one document.
    std::filesystem::path baseDirectory;
    if (!source.path.empty()) {
        baseDirectory = std::filesystem::path(source.path).parent_path();
    }
    ProjectLoad project = LoadProject(source, baseDirectory);
    result.sources = std::move(project.sources);
    result.diagnostics = std::move(project.diagnostics);
    if (!project.root.has_value() || HasFailure(result.diagnostics)) {
        return result;
    }
    XmlNode root = std::move(*project.root);
    // A library is written to be borrowed from: it holds the definitions of a document and no screen
    // of its own, so it compiles to a tree that holds the definitions and nothing else. It is read
    // the same way a document is, which is what lets a library be checked on its own.
    const bool library = root.name == "library";
    const bool forest = root.name == "screens";
    if (!forest && !library && root.name != "window" && root.name != "board") {
        AddDiagnostic(result.diagnostics, DIAGNOSTIC_ROOT_WINDOW,
                      "The root element must be 'window', 'board', 'screens', or 'library'.", root.location,
                      static_cast<int>(root.name.size()));
        return result;
    }
    if (library) {
        for (const XmlNode& child : root.children) {
            if (IsDefinitionElement(child.name)) {
                continue;
            }
            AddDiagnostic(result.diagnostics, DIAGNOSTIC_IMPORT_IGNORED,
                          "A library holds definitions, and '" + child.name + "' is not one.", child.location,
                          static_cast<int>(child.name.size()));
            return result;
        }
    }
    if (forest) {
        for (const XmlNode& child : root.children) {
            if (child.name == "window" || child.name == "board" || IsDefinitionElement(child.name)) {
                continue;
            }
            AddDiagnostic(result.diagnostics, DIAGNOSTIC_ROOT_WINDOW, "Each root element must be 'window' or 'board'.",
                          child.location, static_cast<int>(child.name.size()));
            return result;
        }
    }
    DocumentModel model;
    SetSuppressedFoundations(&model.removedTokens);
    const bool prepared = PrepareDocument(root, model, result.diagnostics);
    if (!prepared) {
        SetSuppressedFoundations(nullptr);
        return result;
    }
    std::unordered_set<std::string> ids;
    const std::size_t before = result.diagnostics.size();
    if (library) {
        IrNode host;
        host.kind = NodeKind::FRAME;
        host.name = "library";
        host.location = root.location;
        bool ok = true;
        for (const XmlNode& child : root.children) {
            auto lowered = Lower(child, result.diagnostics, ids, model, 1);
            if (!lowered.has_value()) {
                ok = false;
                continue;
            }
            host.children.push_back(std::move(*lowered));
        }
        if (ok && result.diagnostics.size() == before) {
            result.root = std::move(host);
        }
    } else if (!forest) {
        auto lowered = Lower(root, result.diagnostics, ids, model, 1);
        if (result.diagnostics.size() == before && lowered.has_value()) {
            for (const AnimationSpec& animation : model.animations) {
                lowered->visual.motions.push_back(MotionFromAnimation(animation));
            }
            result.root = std::move(*lowered);
        }
    } else {
        IrNode host;
        host.kind = NodeKind::FRAME;
        host.name = "screens";
        host.location = root.location;
        bool ok = result.diagnostics.size() == before;
        for (const XmlNode& child : root.children) {
            // A definition the document holds belongs to the document rather than to one of its
            // screens, so it is lowered into the forest next to the screens. It is marked as borrowed
            // when it came from another file, so it is in the tree a copy is resolved from without
            // being built into any screen.
            const bool screen = child.name == "window" || child.name == "board";
            if (!screen && !IsDefinitionElement(child.name)) {
                continue;
            }
            auto lowered = Lower(child, result.diagnostics, ids, model, 1);
            if (!lowered.has_value()) {
                ok = false;
                continue;
            }
            if (screen) {
                for (const AnimationSpec& animation : model.animations) {
                    lowered->visual.motions.push_back(MotionFromAnimation(animation));
                }
            }
            host.children.push_back(std::move(*lowered));
        }
        if (ok && !host.children.empty()) {
            result.root = std::move(host);
        }
    }
    SetSuppressedFoundations(nullptr);
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
