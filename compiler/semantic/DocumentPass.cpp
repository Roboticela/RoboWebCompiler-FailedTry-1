#include "compiler/semantic/DocumentPass.h"

#include "compiler/semantic/ComponentCatalog.h"
#include "compiler/types/ValueParser.h"
#include "xml/diagnostics/Diagnostic.h"

#include <algorithm>
#include <cstdlib>
#include <unordered_set>

namespace Runtime {
namespace {

bool ReadAnimation(const XmlNode& node, DocumentModel& model, std::vector<Diagnostic>& diagnostics);

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
    if (name == "normal") {
        return STYLE_NORMAL;
    }
    if (name == "hover") {
        return STYLE_HOVER;
    }
    if (name == "pressed") {
        return STYLE_PRESSED;
    }
    if (name == "focused" || name == "focusVisible") {
        return STYLE_FOCUSED;
    }
    if (name == "disabled" || name == "readonly") {
        return STYLE_DISABLED;
    }
    if (name == "selected" || name == "checked") {
        return STYLE_SELECTED;
    }
    if (name == "expanded" || name == "collapsed" || name == "loading" || name == "error" || name == "success" || name == "dragging" ||
        name == "dragOver") {
        return -2;
    }
    return -1;
}

std::string_view ExpandToken(std::string_view raw, const std::unordered_map<std::string, std::string>& tokens, std::string& storage) {
    const std::string_view trimmed = TrimCopyView(raw);
    if (trimmed.size() > 1 && trimmed.front() == '$') {
        const auto found = tokens.find(std::string(trimmed.substr(1)));
        if (found != tokens.end()) {
            storage = found->second;
            return storage;
        }
    }
    return raw;
}

bool ReadPatch(const XmlNode& node, StylePatch& patch, const std::unordered_map<std::string, std::string>& tokens,
               std::vector<Diagnostic>& diagnostics) {
    bool ok = true;
    for (const XmlAttribute& attribute : node.attributes) {
        if (attribute.name == "name" || attribute.name == "extends" || attribute.name == "variant" || attribute.name == "maxWidth" ||
            attribute.name == "minWidth") {
            continue;
        }
        std::string tokenStorage;
        const std::string_view raw = ExpandToken(attribute.value, tokens, tokenStorage);
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
    style.extends = AttributeText(node, "extends");
    style.variant = AttributeText(node, "variant");
    if (!ReadPatch(node, style.phases[STYLE_NORMAL], model.tokens, diagnostics)) {
        return false;
    }
    for (const XmlNode& child : node.children) {
        if (child.name == "animate" || child.name == "animation") {
            continue;
        }
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
        const float maxWidth = AttributeText(child, "maxWidth").empty() ? 0.0f : std::strtof(AttributeText(child, "maxWidth").c_str(), nullptr);
        const float minWidth = AttributeText(child, "minWidth").empty() ? 0.0f : std::strtof(AttributeText(child, "minWidth").c_str(), nullptr);
        const int phase = StylePhase(stateName);
        if (phase == -1 && maxWidth <= 0.0f && minWidth <= 0.0f && child.name == "state") {
            AddDiagnostic(diagnostics, DIAGNOSTIC_TYPE_MISMATCH, "Unknown style state '" + stateName + "'.", child.location,
                          static_cast<int>(stateName.size()));
            return false;
        }
        StylePatch patch;
        if (!ReadPatch(child, patch, model.tokens, diagnostics)) {
            return false;
        }
        patch.maxWidth = maxWidth;
        patch.minWidth = minWidth;
        if (phase >= 0) {
            OverlayPatch(style.phases[phase], patch);
        } else {
            style.extraPhases[stateName] = patch;
        }
    }
    for (const XmlNode& child : node.children) {
        if (child.name == "animate" || child.name == "animation") {
            if (!ReadAnimation(child, model, diagnostics)) {
                return false;
            }
            if (!model.animations.empty()) {
                style.motions.push_back(MotionFromAnimation(model.animations.back()));
                model.animations.pop_back();
            }
        }
    }
    const std::string key = style.variant.empty() ? name : name + "/" + style.variant;
    model.styles[key] = style;
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

bool KnownAnimationProperty(std::string_view property) {
    static const char* properties[] = {"x",     "y",     "width",    "height", "opacity", "scale",    "scaleX",   "scaleY",
                                       "rotation", "rotationX", "rotationY", "backgroundColor", "color", "borderColor", "radius", "shadow",
                                       "blur",  "transform", "path", "gradient", "clip", "filter"};
    if (property.empty()) {
        return true;
    }
    for (const char* candidate : properties) {
        if (property == candidate) {
            return true;
        }
    }
    return false;
}

bool KnownEasing(std::string_view easing) {
    return easing.empty() || easing == "linear" || easing == "easeIn" || easing == "easeOut" || easing == "easeInOut" || easing == "cubicBezier" ||
           easing == "spring";
}

bool KnownTrigger(std::string_view trigger) {
    return trigger.empty() || trigger == "onMount" || trigger == "onUnmount" || trigger == "onHover" || trigger == "onPress" || trigger == "onFocus" ||
           trigger == "onStateChange" || trigger == "onPropertyChange" || trigger == "onScroll" || trigger == "onDrag" || trigger == "onLoad" ||
           trigger == "onComplete" || trigger == "hover";
}

bool ReadAnimation(const XmlNode& node, DocumentModel& model, std::vector<Diagnostic>& diagnostics) {
    AnimationSpec animation;
    animation.name = AttributeText(node, "name");
    animation.target = AttributeText(node, "target");
    animation.property = AttributeText(node, "property");
    animation.from = AttributeText(node, "from");
    animation.to = AttributeText(node, "to");
    animation.duration = AttributeText(node, "duration");
    animation.easing = AttributeText(node, "easing");
    animation.trigger = AttributeText(node, "trigger");
    animation.mass = AttributeText(node, "mass");
    animation.stiffness = AttributeText(node, "stiffness");
    animation.damping = AttributeText(node, "damping");
    animation.velocity = AttributeText(node, "velocity");
    animation.sequence = AttributeText(node, "sequence");
    animation.parallel = AttributeText(node, "parallel");
    animation.stagger = AttributeText(node, "stagger");
    if (node.name == "animate" && (animation.target.empty() || animation.property.empty())) {
        AddDiagnostic(diagnostics, DIAGNOSTIC_TYPE_MISMATCH, "An animation needs a target and a property.", node.location,
                      static_cast<int>(node.name.size()));
        return false;
    }
    if (!KnownEasing(animation.easing) || !KnownTrigger(animation.trigger) || !KnownAnimationProperty(animation.property)) {
        AddDiagnostic(diagnostics, DIAGNOSTIC_TYPE_MISMATCH, "Animation easing or trigger is not supported.", node.location,
                      static_cast<int>(node.name.size()));
        return false;
    }
    for (const XmlNode& child : node.children) {
        if (child.name != "keyframe") {
            AddDiagnostic(diagnostics, DIAGNOSTIC_UNKNOWN_COMPONENT, "Animation blocks contain keyframe elements.", child.location,
                          static_cast<int>(child.name.size()));
            return false;
        }
        std::string payload;
        for (const XmlAttribute& attribute : child.attributes) {
            if (attribute.name == "time") {
                continue;
            }
            if (!payload.empty()) {
                payload += ";";
            }
            payload += attribute.name;
            payload += "=";
            payload += attribute.value;
        }
        animation.keyframes.emplace_back(AttributeText(child, "time"), std::move(payload));
    }
    model.animations.push_back(std::move(animation));
    return true;
}

void ReadTokens(const XmlNode& node, std::unordered_map<std::string, std::string>& tokens, std::vector<Diagnostic>& diagnostics) {
    for (const XmlNode& child : node.children) {
        const std::string name = AttributeText(child, "name");
        const std::string value = AttributeText(child, "value");
        if (name.empty() || value.empty()) {
            AddDiagnostic(diagnostics, DIAGNOSTIC_TYPE_MISMATCH, "A token needs a name and a value.", child.location,
                          static_cast<int>(child.name.size()));
            continue;
        }
        tokens[name] = value;
        tokens[child.name + "." + name] = value;
    }
}

void ApplyTokens(XmlNode& node, const std::unordered_map<std::string, std::string>& tokens) {
    for (XmlAttribute& attribute : node.attributes) {
        const std::string_view value = TrimCopyView(attribute.value);
        if (value.size() > 1 && value.front() == '$') {
            const auto found = tokens.find(std::string(value.substr(1)));
            if (found != tokens.end()) {
                attribute.value = found->second;
            }
        }
    }
    for (XmlNode& child : node.children) {
        ApplyTokens(child, tokens);
    }
}

bool Harvest(XmlNode& node, DocumentModel& model, std::vector<Diagnostic>& diagnostics) {
    std::vector<XmlNode> kept;
    bool ok = true;
    for (XmlNode& child : node.children) {
        if (child.name == "style") {
            ok = ReadStyle(child, model, diagnostics) && ok;
            continue;
        }
        if (child.name == "tokens") {
            ReadTokens(child, model.tokens, diagnostics);
            continue;
        }
        if (child.name == "animate" || child.name == "animation") {
            ok = ReadAnimation(child, model, diagnostics) && ok;
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
            std::vector<XmlNode> visual;
            for (const XmlNode& member : child.children) {
                if (member.name == "property" || member.name == "event") {
                    if (AttributeText(member, "name").empty()) {
                        AddDiagnostic(diagnostics, DIAGNOSTIC_TYPE_MISMATCH, "A component member needs a name.", member.location,
                                      static_cast<int>(member.name.size()));
                        ok = false;
                    }
                    continue;
                }
                visual.push_back(member);
            }
            XmlNode body = visual.size() == 1 ? visual.front() : XmlNode{};
            if (visual.size() != 1) {
                body.name = "frame";
                body.location = child.location;
                body.attributes.push_back(XmlAttribute{});
                body.attributes.back().name = "layout";
                body.attributes.back().value = "vertical";
                body.children = std::move(visual);
            }
            model.components[name] = std::move(body);
        }
        ok = Harvest(child, model, diagnostics) && ok;
        kept.push_back(std::move(child));
    }
    node.children = std::move(kept);
    return ok;
}

}  // namespace

float DurationSeconds(const std::string& text) {
    if (text.size() > 2 && text.ends_with("ms")) {
        return std::strtof(text.c_str(), nullptr) / 1000.0f;
    }
    return std::strtof(text.c_str(), nullptr);
}

float KeyTime(const std::string& text, float duration) {
    if (!text.empty() && text.back() == '%') {
        return std::strtof(text.c_str(), nullptr) / 100.0f * (duration <= 0.0f ? 1.0f : duration);
    }
    if (text.empty()) {
        return 0.0f;
    }
    return DurationSeconds(text);
}

MotionDecl MotionFromAnimation(const AnimationSpec& animation) {
    MotionDecl motion;
    motion.name = animation.name;
    motion.target = animation.target;
    motion.property = animation.property;
    motion.duration = animation.duration.empty() ? 0.15f : DurationSeconds(animation.duration);
    motion.easing = animation.easing.empty() ? "linear" : animation.easing;
    motion.trigger = animation.trigger;
    motion.sequence = animation.sequence;
    if (const auto number = ParseFloat(animation.from)) {
        motion.from = *number;
        motion.hasFrom = true;
    }
    if (const auto number = ParseFloat(animation.to)) {
        motion.to = *number;
        motion.hasTo = true;
    }
    if (const auto number = ParseFloat(animation.mass)) {
        motion.mass = *number;
    }
    if (const auto number = ParseFloat(animation.stiffness)) {
        motion.stiffness = *number;
    }
    if (const auto number = ParseFloat(animation.damping)) {
        motion.damping = *number;
    }
    if (const auto number = ParseFloat(animation.velocity)) {
        motion.velocity = *number;
    }
    if (const auto number = ParseFloat(animation.stagger)) {
        motion.stagger = animation.stagger.size() > 2 && animation.stagger.ends_with("ms") ? *number / 1000.0f : *number;
    }
    motion.parallel = animation.parallel;
    for (const auto& key : animation.keyframes) {
        const float time = KeyTime(key.first, motion.duration);
        std::string payload = key.second;
        while (!payload.empty()) {
            const std::size_t split = payload.find(';');
            const std::string piece = split == std::string::npos ? payload : payload.substr(0, split);
            const std::size_t eq = piece.find('=');
            MotionKey sample;
            sample.time = time;
            if (eq == std::string::npos) {
                sample.property = motion.property;
                sample.text = piece;
            } else {
                sample.property = piece.substr(0, eq);
                sample.text = piece.substr(eq + 1);
                if (const auto number = ParseFloat(sample.text)) {
                    sample.number = *number;
                    sample.hasNumber = true;
                }
            }
            motion.keys.push_back(std::move(sample));
            if (split == std::string::npos) {
                break;
            }
            payload.erase(0, split + 1);
        }
    }
    return motion;
}

void ResolveStyleExtends(DocumentModel& model) {
    std::unordered_set<std::string> visiting;
    const auto resolve = [&](auto&& self, const std::string& name) -> void {
        const auto found = model.styles.find(name);
        if (found == model.styles.end() || found->second.extends.empty() || visiting.count(name) != 0) {
            return;
        }
        visiting.insert(name);
        const std::string baseName = found->second.extends;
        self(self, baseName);
        const auto base = model.styles.find(baseName);
        if (base == model.styles.end()) {
            found->second.extends.clear();
            return;
        }
        VisualStyle merged = base->second;
        merged.extends.clear();
        merged.variant = found->second.variant;
        for (int phase = 0; phase < STYLE_COUNT; ++phase) {
            OverlayPatch(merged.phases[phase], found->second.phases[phase]);
        }
        for (const auto& extra : found->second.extraPhases) {
            OverlayPatch(merged.extraPhases[extra.first], extra.second);
        }
        merged.motions.insert(merged.motions.end(), found->second.motions.begin(), found->second.motions.end());
        merged.used = true;
        model.styles[name] = std::move(merged);
    };
    std::vector<std::string> names;
    for (const auto& style : model.styles) {
        names.push_back(style.first);
    }
    for (const std::string& name : names) {
        resolve(resolve, name);
    }
}

bool PrepareDocument(XmlNode& root, DocumentModel& model, std::vector<Diagnostic>& diagnostics) {
    bool ok = Harvest(root, model, diagnostics);
    ResolveStyleExtends(model);
    ApplyTokens(root, model.tokens);
    for (XmlNode& child : root.children) {
        if (!ExpandNode(child, model, diagnostics, 1)) {
            ok = false;
        }
    }
    return ok;
}

}  // namespace Runtime
