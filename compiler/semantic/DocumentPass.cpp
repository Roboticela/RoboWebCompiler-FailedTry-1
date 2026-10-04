#include "compiler/semantic/DocumentPass.h"

#include "compiler/semantic/ComponentCatalog.h"
#include "compiler/types/ValueParser.h"
#include "xml/diagnostics/Diagnostic.h"

#include <algorithm>
#include <unordered_set>

namespace Runtime {
namespace {

void AddDiagnostic(std::vector<Diagnostic>& diagnostics, std::string code, std::string message, SourceLocation location, int span) {
    Diagnostic diagnostic;
    diagnostic.severity = DiagnosticSeverity::FAILURE;
    diagnostic.code = std::move(code);
    diagnostic.message = std::move(message);
    diagnostic.location = std::move(location);
    diagnostic.spanLength = span;
    diagnostics.push_back(std::move(diagnostic));
}

const XmlAttribute* FindAttribute(const XmlNode& node, std::string_view name) {
    for (const XmlAttribute& attribute : node.attributes) {
        if (attribute.name == name) {
            return &attribute;
        }
    }
    return nullptr;
}

std::string AttributeText(const XmlNode& node, std::string_view name) {
    if (const XmlAttribute* attribute = FindAttribute(node, name)) {
        return std::string(TrimCopyView(attribute->value));
    }
    return {};
}

int StylePhase(std::string_view name) {
    if (name == "hover") {
        return STYLE_HOVER;
    }
    if (name == "pressed") {
        return STYLE_PRESSED;
    }
    if (name == "focused" || name == "focusedVisible") {
        return STYLE_FOCUSED;
    }
    if (name == "disabled") {
        return STYLE_DISABLED;
    }
    if (name == "selected" || name == "checked") {
        return STYLE_SELECTED;
    }
    return STYLE_NORMAL;
}

bool ReadPatch(const XmlNode& node, StylePatch& patch, std::vector<Diagnostic>& diagnostics) {
    bool ok = true;
    for (const XmlAttribute& attribute : node.attributes) {
        if (attribute.name == "name") {
            continue;
        }
        const std::string_view raw = attribute.value;
        if (attribute.name == "background") {
            if (auto color = ParseColor(raw)) {
                patch.background = true;
                patch.backgroundColor = *color;
            } else {
                ok = false;
            }
        } else if (attribute.name == "color" || attribute.name == "foreground") {
            if (auto color = ParseColor(raw)) {
                patch.foreground = true;
                patch.foregroundColor = *color;
            } else {
                ok = false;
            }
        } else if (attribute.name == "borderColor") {
            if (auto color = ParseColor(raw)) {
                patch.border = true;
                patch.borderColor = *color;
            } else {
                ok = false;
            }
        } else if (attribute.name == "borderWidth") {
            if (auto length = ParseLength(raw)) {
                patch.border = true;
                patch.borderWidth = length->value;
            } else {
                ok = false;
            }
        } else if (attribute.name == "cornerRadius" || attribute.name == "radius") {
            if (auto length = ParseLength(raw)) {
                patch.radius = true;
                patch.radiusLength = *length;
            } else {
                ok = false;
            }
        } else if (attribute.name == "padding") {
            if (auto edges = ParseEdges(raw)) {
                patch.padding = true;
                patch.paddingEdges = *edges;
            } else {
                ok = false;
            }
        } else if (attribute.name == "fontSize") {
            if (auto size = ParseFontSize(raw)) {
                patch.fontSize = true;
                patch.fontSizeValue = *size;
            } else {
                ok = false;
            }
        } else if (attribute.name == "fontWeight") {
            if (auto weight = ParseInteger(raw)) {
                patch.fontWeight = true;
                patch.fontWeightValue = *weight;
            } else {
                ok = false;
            }
        } else {
            AddDiagnostic(diagnostics, DIAGNOSTIC_UNKNOWN_PROPERTY, "Unknown style property '" + attribute.name + "'.",
                          attribute.nameLocation, static_cast<int>(attribute.name.size()));
            ok = false;
            continue;
        }
        if (!ok) {
            AddDiagnostic(diagnostics, DIAGNOSTIC_TYPE_MISMATCH, "Style property '" + attribute.name + "' has an invalid value.",
                          attribute.valueLocation, attribute.valueSpan);
            return false;
        }
    }
    return ok;
}

bool ReadStyle(const XmlNode& node, DocumentModel& model, std::vector<Diagnostic>& diagnostics) {
    const std::string name = AttributeText(node, "name");
    if (name.empty()) {
        AddDiagnostic(diagnostics, DIAGNOSTIC_TYPE_MISMATCH, "A style needs a name.", node.location, static_cast<int>(node.name.size()));
        return false;
    }
    VisualStyle style;
    style.used = true;
    if (!ReadPatch(node, style.phases[STYLE_NORMAL], diagnostics)) {
        return false;
    }
    for (const XmlNode& child : node.children) {
        if (child.name != "state") {
            AddDiagnostic(diagnostics, DIAGNOSTIC_UNKNOWN_COMPONENT, "Style blocks contain state elements.", child.location,
                          static_cast<int>(child.name.size()));
            return false;
        }
        const std::string stateName = AttributeText(child, "name");
        if (stateName.empty()) {
            AddDiagnostic(diagnostics, DIAGNOSTIC_TYPE_MISMATCH, "A style state needs a name.", child.location, 5);
            return false;
        }
        const int phase = StylePhase(stateName);
        if (phase == STYLE_NORMAL && stateName != "normal") {
            AddDiagnostic(diagnostics, DIAGNOSTIC_TYPE_MISMATCH, "Unknown style state '" + stateName + "'.", child.location,
                          static_cast<int>(stateName.size()));
            return false;
        }
        if (!ReadPatch(child, style.phases[phase], diagnostics)) {
            return false;
        }
    }
    model.styles[name] = style;
    return true;
}

std::string Substitute(std::string text, const std::unordered_map<std::string, std::string>& parameters) {
    std::string result;
    for (std::size_t index = 0; index < text.size();) {
        if (text[index] == '{') {
            const std::size_t end = text.find('}', index + 1);
            if (end != std::string::npos) {
                const std::string key = text.substr(index + 1, end - index - 1);
                const auto found = parameters.find(key);
                if (found != parameters.end()) {
                    result += found->second;
                    index = end + 1;
                    continue;
                }
            }
        }
        result.push_back(text[index]);
        ++index;
    }
    return result;
}

void SubstituteNode(XmlNode& node, const std::unordered_map<std::string, std::string>& parameters) {
    for (XmlAttribute& attribute : node.attributes) {
        attribute.value = Substitute(attribute.value, parameters);
    }
    for (XmlNode& child : node.children) {
        SubstituteNode(child, parameters);
    }
}

bool UsesParameter(const XmlNode& node, const std::string& key) {
    const std::string token = "{" + key + "}";
    for (const XmlAttribute& attribute : node.attributes) {
        if (attribute.value.find(token) != std::string::npos) {
            return true;
        }
    }
    for (const XmlNode& child : node.children) {
        if (UsesParameter(child, key)) {
            return true;
        }
    }
    return false;
}

void CopyAttribute(XmlNode& node, const XmlAttribute& attribute) {
    if (FindAttribute(node, attribute.name) != nullptr) {
        return;
    }
    node.attributes.push_back(attribute);
}

bool ExpandNode(XmlNode& node, const DocumentModel& model, std::vector<Diagnostic>& diagnostics, int depth);

bool FillSlots(XmlNode& node, std::vector<XmlNode> content, const std::unordered_map<std::string, std::vector<XmlNode>>& slots) {
    std::vector<XmlNode> next;
    for (XmlNode& child : node.children) {
        if (child.name == "slot") {
            const std::string name = AttributeText(child, "name");
            const std::string slotName = name.empty() ? "content" : name;
            const auto found = slots.find(slotName);
            if (found != slots.end()) {
                next.insert(next.end(), found->second.begin(), found->second.end());
            } else if (slotName == "content") {
                next.insert(next.end(), content.begin(), content.end());
            }
            continue;
        }
        FillSlots(child, content, slots);
        next.push_back(std::move(child));
    }
    node.children = std::move(next);
    return true;
}

bool ExpandNode(XmlNode& node, const DocumentModel& model, std::vector<Diagnostic>& diagnostics, int depth) {
    if (depth > MAX_TREE_DEPTH) {
        AddDiagnostic(diagnostics, DIAGNOSTIC_TREE_DEPTH, "Component expansion is too deep.", node.location, static_cast<int>(node.name.size()));
        return false;
    }
    const auto definition = model.components.find(node.name);
    if (definition == model.components.end()) {
        for (XmlNode& child : node.children) {
            if (!ExpandNode(child, model, diagnostics, depth + 1)) {
                return false;
            }
        }
        return true;
    }
    std::unordered_map<std::string, std::string> parameters;
    std::unordered_set<std::string> parameterNames;
    for (const XmlAttribute& attribute : node.attributes) {
        parameters[attribute.name] = attribute.value;
        if (UsesParameter(definition->second, attribute.name)) {
            parameterNames.insert(attribute.name);
        }
    }
    std::unordered_map<std::string, std::vector<XmlNode>> slotted;
    std::vector<XmlNode> content;
    for (XmlNode& child : node.children) {
        if (!ExpandNode(child, model, diagnostics, depth + 1)) {
            return false;
        }
        const std::string slot = AttributeText(child, "slot");
        XmlNode copy = child;
        if (!slot.empty()) {
            copy.attributes.erase(std::remove_if(copy.attributes.begin(), copy.attributes.end(),
                                                 [](const XmlAttribute& attribute) { return attribute.name == "slot"; }),
                                  copy.attributes.end());
            slotted[slot].push_back(std::move(copy));
        } else {
            content.push_back(std::move(copy));
        }
    }
    XmlNode expanded = definition->second;
    SubstituteNode(expanded, parameters);
    FillSlots(expanded, content, slotted);
    for (const XmlAttribute& attribute : node.attributes) {
        if (parameterNames.count(attribute.name) == 0) {
            CopyAttribute(expanded, attribute);
        }
    }
    if (!ExpandNode(expanded, model, diagnostics, depth + 1)) {
        return false;
    }
    node = std::move(expanded);
    return true;
}

bool Harvest(XmlNode& node, DocumentModel& model, std::vector<Diagnostic>& diagnostics) {
    std::vector<XmlNode> kept;
    bool ok = true;
    for (XmlNode& child : node.children) {
        if (child.name == "style") {
            ok = ReadStyle(child, model, diagnostics) && ok;
            continue;
        }
        if (child.name == "state") {
            const std::string name = AttributeText(child, "name");
            const std::string value = AttributeText(child, "value");
            if (name.empty()) {
                AddDiagnostic(diagnostics, DIAGNOSTIC_TYPE_MISMATCH, "A state entry needs a name.", child.location, 5);
                ok = false;
                continue;
            }
            model.states.emplace_back(name, value);
            continue;
        }
        if (child.name == "component") {
            const std::string name = AttributeText(child, "name");
            if (name.empty() || child.children.empty()) {
                AddDiagnostic(diagnostics, DIAGNOSTIC_TYPE_MISMATCH, "A component needs a name and a body.", child.location,
                              static_cast<int>(child.name.size()));
                ok = false;
                continue;
            }
            if (FindComponent(name) != nullptr) {
                AddDiagnostic(diagnostics, DIAGNOSTIC_UNKNOWN_COMPONENT, "Component '" + name + "' hides a built-in element.",
                              child.location, static_cast<int>(name.size()));
                ok = false;
                continue;
            }
            XmlNode body = child.children.size() == 1 ? child.children.front() : XmlNode{};
            if (child.children.size() != 1) {
                body.name = "column";
                body.location = child.location;
                body.children = child.children;
            }
            model.components[name] = std::move(body);
            continue;
        }
        ok = Harvest(child, model, diagnostics) && ok;
        kept.push_back(std::move(child));
    }
    node.children = std::move(kept);
    return ok;
}

}  // namespace

bool PrepareDocument(XmlNode& root, DocumentModel& model, std::vector<Diagnostic>& diagnostics) {
    bool ok = Harvest(root, model, diagnostics);
    for (XmlNode& child : root.children) {
        if (!ExpandNode(child, model, diagnostics, 1)) {
            ok = false;
        }
    }
    return ok;
}

}  // namespace Runtime
