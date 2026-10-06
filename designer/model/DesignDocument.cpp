#include "designer/model/DesignDocument.h"

#include "compiler/semantic/ComponentCatalog.h"
#include "xml/parser/XmlParser.h"
#include "xml/serializer/XmlSerializer.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

namespace Runtime {
namespace {

XmlAttribute* FindAttribute(XmlNode& node, std::string_view name) {
    for (XmlAttribute& attribute : node.attributes) {
        if (attribute.name == name) {
            return &attribute;
        }
    }
    return nullptr;
}

const XmlAttribute* FindAttribute(const XmlNode& node, std::string_view name) {
    for (const XmlAttribute& attribute : node.attributes) {
        if (attribute.name == name) {
            return &attribute;
        }
    }
    return nullptr;
}

void AddAttribute(XmlNode& node, std::string name, std::string value) {
    if (XmlAttribute* existing = FindAttribute(node, name)) {
        existing->value = std::move(value);
        return;
    }
    XmlAttribute attribute;
    attribute.name = std::move(name);
    attribute.value = std::move(value);
    node.attributes.push_back(std::move(attribute));
}

// A component only accepts the properties its catalog entry lists. Writing a default the
// component does not declare would leave a document that fails to compile, so a default
// that is not part of the language is left out instead.
void AddDefaultAttribute(XmlNode& node, const char* name, std::string value) {
    if (!ComponentAcceptsProperty(node.name, name)) {
        return;
    }
    AddAttribute(node, name, std::move(value));
}

bool IsDocumentMeta(const std::string& name) {
    return name == "tokens" || name == "style" || name == "state" || name == "animate" || name == "animation" || name == "property" ||
           name == "event";
}

bool ContainsWindow(const XmlNode& node) {
    for (const XmlNode& child : node.children) {
        if (child.name == "window" || ContainsWindow(child)) {
            return true;
        }
    }
    return false;
}

void LiftWindows(XmlNode node, std::vector<XmlNode>& forest) {
    if (node.name != "window") {
        forest.push_back(std::move(node));
        return;
    }
    std::vector<XmlNode> kept;
    std::vector<XmlNode> lifted;
    for (XmlNode& child : node.children) {
        if (child.name == "window") {
            LiftWindows(std::move(child), lifted);
        } else {
            kept.push_back(std::move(child));
        }
    }
    node.children = std::move(kept);
    bool visual = false;
    for (const XmlNode& child : node.children) {
        if (!IsDocumentMeta(child.name)) {
            visual = true;
        }
    }
    if (!lifted.empty() && !visual) {
        if (!node.children.empty()) {
            lifted.front().children.insert(lifted.front().children.begin(), node.children.begin(), node.children.end());
        }
        forest.insert(forest.end(), std::make_move_iterator(lifted.begin()), std::make_move_iterator(lifted.end()));
        return;
    }
    forest.push_back(std::move(node));
    forest.insert(forest.end(), std::make_move_iterator(lifted.begin()), std::make_move_iterator(lifted.end()));
}

void NormalizeRoots(std::optional<XmlNode>& root) {
    if (!root.has_value() || root->name == "component") {
        return;
    }
    std::vector<XmlNode> nodes;
    if (root->name == "screens") {
        for (XmlNode& child : root->children) {
            LiftWindows(std::move(child), nodes);
        }
    } else {
        LiftWindows(std::move(*root), nodes);
    }
    XmlNode screens;
    screens.name = "screens";
    screens.children = std::move(nodes);
    root = std::move(screens);
}

XmlNode* TokenHost(XmlNode& root) {
    if (root.name == "screens") {
        for (XmlNode& child : root.children) {
            if (child.name == "window") {
                return &child;
            }
        }
    }
    return &root;
}

XmlNode MakeComponent(std::string_view name) {
    XmlNode node;
    node.name = std::string(name);
    if (name == "window") {
        AddAttribute(node, "auto", "false");
        AddAttribute(node, "name", "Window");
        AddAttribute(node, "title", "Window");
        AddAttribute(node, "width", "800");
        AddAttribute(node, "height", "600");
        AddAttribute(node, "background", "#FFFFFF");
        AddAttribute(node, "mobile", "true");
        AddAttribute(node, "tablet", "true");
        AddAttribute(node, "desktop", "true");
        AddAttribute(node, "position", "absolute");
        AddAttribute(node, "x", "0");
        AddAttribute(node, "y", "0");
    } else if (name == "board") {
        AddAttribute(node, "auto", "false");
        AddAttribute(node, "name", "Board");
        AddAttribute(node, "width", "800");
        AddAttribute(node, "height", "600");
        AddAttribute(node, "background", "#FFFFFF");
        AddAttribute(node, "position", "absolute");
        AddAttribute(node, "x", "0");
        AddAttribute(node, "y", "0");
    } else if (name == "frame") {
        AddDefaultAttribute(node, "auto", "true");
        AddDefaultAttribute(node, "width", "160");
        AddDefaultAttribute(node, "height", "80");
        AddDefaultAttribute(node, "background", "#0E7AFF");
        AddDefaultAttribute(node, "radius", "8");
    } else if (name == "rectangle") {
        AddDefaultAttribute(node, "width", "100");
        AddDefaultAttribute(node, "height", "100");
        AddDefaultAttribute(node, "fill", "#0E7AFF");
        AddDefaultAttribute(node, "radius", "0");
    } else if (name == "ellipse") {
        AddDefaultAttribute(node, "width", "100");
        AddDefaultAttribute(node, "height", "100");
        AddDefaultAttribute(node, "fill", "#0E7AFF");
    } else if (name == "circle") {
        AddDefaultAttribute(node, "diameter", "100");
        AddDefaultAttribute(node, "fill", "#0E7AFF");
    } else if (name == "shadow") {
        AddDefaultAttribute(node, "shadowX", "0");
        AddDefaultAttribute(node, "shadowY", "4");
        AddDefaultAttribute(node, "shadowColor", "#1D1D1F");
    } else if (name == "text") {
        AddDefaultAttribute(node, "auto", "true");
        AddDefaultAttribute(node, "text", "Text");
        AddDefaultAttribute(node, "fontSize", "16");
        AddDefaultAttribute(node, "textColor", "#18181A");
    } else if (name == "image") {
        AddDefaultAttribute(node, "source", "image.png");
        AddDefaultAttribute(node, "width", "120");
        AddDefaultAttribute(node, "height", "80");
    } else if (name == "vector") {
        AddDefaultAttribute(node, "shape", "ellipse");
        AddDefaultAttribute(node, "width", "64");
        AddDefaultAttribute(node, "height", "64");
        AddDefaultAttribute(node, "fill", "#7EFFF5");
    } else if (name == "path") {
        AddDefaultAttribute(node, "d", "M0 40 L80 0 L80 40 Z");
        AddDefaultAttribute(node, "width", "80");
        AddDefaultAttribute(node, "height", "40");
        AddDefaultAttribute(node, "stroke", "#18181A");
        AddDefaultAttribute(node, "strokeWidth", "2");
    } else if (name == "line" || name == "arrow") {
        // An arrow is a line with a head, so it starts life as the same line and then says
        // how big its head is.
        AddDefaultAttribute(node, "x1", "0");
        AddDefaultAttribute(node, "y1", "0");
        AddDefaultAttribute(node, "x2", "80");
        AddDefaultAttribute(node, "y2", "0");
        AddDefaultAttribute(node, "stroke", "#18181A");
        AddDefaultAttribute(node, "strokeWidth", "2");
        AddDefaultAttribute(node, "lineCap", "butt");
        AddDefaultAttribute(node, "width", "80");
        AddDefaultAttribute(node, "height", "2");
        if (name == "arrow") {
            AddDefaultAttribute(node, "headLength", "12");
            AddDefaultAttribute(node, "headWidth", "12");
            AddDefaultAttribute(node, "head", "end");
        }
    } else if (name == "group" || name == "component" || name == "slot" || name == "scroll") {
        AddDefaultAttribute(node, "width", "160");
        AddDefaultAttribute(node, "height", "80");
        if (name == "component" || name == "slot") {
            AddDefaultAttribute(node, "name", std::string(name));
        }
    } else if (name == "instance") {
        AddDefaultAttribute(node, "component", "component");
        AddDefaultAttribute(node, "width", "160");
        AddDefaultAttribute(node, "height", "80");
    }
    return node;
}

}  // namespace

bool DesignDocument::LoadFile(const std::filesystem::path& file) {
    std::string error;
    const std::optional<SourceFile> source = ReadSourceFile(file, error);
    if (!source.has_value()) {
        parseDiagnostics.clear();
        Diagnostic diagnostic;
        diagnostic.severity = DiagnosticSeverity::FAILURE;
        diagnostic.code = DIAGNOSTIC_CANNOT_READ_FILE;
        diagnostic.message = error;
        diagnostic.location = SourceLocation{file.string(), 0, 0};
        parseDiagnostics.push_back(std::move(diagnostic));
        return false;
    }
    return LoadText(source->text, source->path);
}

bool DesignDocument::LoadText(std::string xmlText, std::string path) {
    filePath = std::move(path);
    return ReplaceText(std::move(xmlText));
}

bool DesignDocument::Save() const {
    if (filePath.empty()) {
        return false;
    }
    std::ofstream stream(std::filesystem::path(filePath), std::ios::binary);
    if (!stream) {
        return false;
    }
    stream << text;
    return static_cast<bool>(stream);
}

const std::string& DesignDocument::Text() const {
    return text;
}

std::string DesignDocument::PreviewText() const {
    if (!root.has_value() || root->name != "component") {
        return text;
    }
    XmlNode window;
    window.name = "window";
    AddAttribute(window, "width", "800");
    AddAttribute(window, "height", "600");
    std::string componentName = "Component";
    if (const XmlAttribute* name = FindAttribute(*root, "name")) {
        if (!name->value.empty()) {
            componentName = name->value;
        }
    }
    std::string label = componentName;
    for (const XmlNode& child : root->children) {
        if (child.name != "property") {
            continue;
        }
        const XmlAttribute* propertyName = FindAttribute(child, "name");
        const XmlAttribute* propertyDefault = FindAttribute(child, "default");
        if (propertyName != nullptr && propertyName->value == "label" && propertyDefault != nullptr && !propertyDefault->value.empty()) {
            label = propertyDefault->value;
        }
    }
    const auto addState = [&](const char* name, const std::string& value) {
        XmlNode state;
        state.name = "state";
        AddAttribute(state, "name", name);
        AddAttribute(state, "value", value);
        window.children.push_back(std::move(state));
    };
    addState("label", label);
    addState("loading", "false");
    addState("isHovered", "false");
    addState("visualBackground", "$color.primary");
    addState("visualTextColor", "$color.onPrimary");
    for (const XmlNode& child : root->children) {
        if (child.name == "property" || child.name == "event") {
            XmlNode hidden;
            hidden.name = "frame";
            AddAttribute(hidden, "width", "0");
            AddAttribute(hidden, "height", "0");
            AddAttribute(hidden, "display", "none");
            window.children.push_back(std::move(hidden));
            continue;
        }
        window.children.push_back(child);
    }
    return SerializeXml(window);
}

const std::string& DesignDocument::FilePath() const {
    return filePath;
}

bool DesignDocument::TextSynced() const {
    return textSynced;
}

const XmlNode* DesignDocument::Root() const {
    return root.has_value() ? &*root : nullptr;
}

const std::vector<Diagnostic>& DesignDocument::ParseDiagnostics() const {
    return parseDiagnostics;
}

const XmlNode* DesignDocument::NodeAt(const std::vector<int>& path) const {
    if (!root.has_value()) {
        return nullptr;
    }
    const XmlNode* node = &*root;
    for (const int index : path) {
        if (index < 0 || index >= static_cast<int>(node->children.size())) {
            return nullptr;
        }
        node = &node->children[static_cast<std::size_t>(index)];
    }
    return node;
}

XmlNode* DesignDocument::MutableNode(const std::vector<int>& path) {
    if (!root.has_value()) {
        return nullptr;
    }
    XmlNode* node = &*root;
    for (const int index : path) {
        if (index < 0 || index >= static_cast<int>(node->children.size())) {
            return nullptr;
        }
        node = &node->children[static_cast<std::size_t>(index)];
    }
    return node;
}

std::string DesignDocument::Attribute(const std::vector<int>& path, std::string_view name) const {
    const XmlNode* node = NodeAt(path);
    if (node == nullptr) {
        return {};
    }
    const XmlAttribute* attribute = FindAttribute(*node, name);
    return attribute == nullptr ? std::string{} : attribute->value;
}

void DesignDocument::SyncTextFromRoot() {
    if (!root.has_value()) {
        return;
    }
    text = SerializeXml(*root);
    textSynced = true;
    parseDiagnostics.clear();
}

bool DesignDocument::AcceptsChildren(const XmlNode& node) const {
    if (node.name == "screens") {
        return true;
    }
    const ComponentSpec* spec = FindComponent(node.name);
    return spec != nullptr && spec->acceptsChildren;
}

bool DesignDocument::SetAttribute(const std::vector<int>& path, std::string_view name, std::string value) {
    if (!textSynced) {
        return false;
    }
    XmlNode* node = MutableNode(path);
    if (node == nullptr || name.empty()) {
        return false;
    }
    if (!ComponentAcceptsProperty(node->name, name)) {
        // Writing a property the component does not declare would break the next compile,
        // so the editor only ever edits inside the language the compiler accepts.
        return false;
    }
    AddAttribute(*node, std::string(name), std::move(value));
    SyncTextFromRoot();
    return true;
}

bool DesignDocument::ClearAttribute(const std::vector<int>& path, std::string_view name) {
    if (!textSynced) {
        return false;
    }
    XmlNode* node = MutableNode(path);
    if (node == nullptr) {
        return false;
    }
    for (std::size_t index = 0; index < node->attributes.size(); ++index) {
        if (node->attributes[index].name == name) {
            node->attributes.erase(node->attributes.begin() + static_cast<std::ptrdiff_t>(index));
            SyncTextFromRoot();
            return true;
        }
    }
    return false;
}

bool DesignDocument::InsertChild(const std::vector<int>& parentPath, std::string_view componentName,
                                 std::vector<int>& insertedPath) {
    if (!textSynced || FindComponent(componentName) == nullptr) {
        return false;
    }
    XmlNode* parent = MutableNode(parentPath);
    if (parent == nullptr || !AcceptsChildren(*parent)) {
        return false;
    }
    const bool rootComponent = componentName == "window" || componentName == "board";
    if (parent->name == "screens" && !rootComponent) {
        return false;
    }
    if (rootComponent && parent->name != "screens") {
        return false;
    }
    parent->children.push_back(MakeComponent(componentName));
    insertedPath = parentPath;
    insertedPath.push_back(static_cast<int>(parent->children.size()) - 1);
    SyncTextFromRoot();
    return true;
}

bool DesignDocument::GroupNodes(const std::vector<std::vector<int>>& paths, std::vector<int>& groupPath) {
    if (!textSynced || paths.empty()) {
        return false;
    }
    std::vector<int> parentPath = paths.front();
    if (parentPath.empty()) {
        return false;
    }
    parentPath.pop_back();
    XmlNode* parent = MutableNode(parentPath);
    if (parent == nullptr || !AcceptsChildren(*parent)) {
        return false;
    }
    std::vector<int> indices;
    for (const std::vector<int>& path : paths) {
        if (path.empty() || path.back() < 0 || path.back() >= static_cast<int>(parent->children.size())) {
            return false;
        }
        // Only siblings can be combined, because the group has to take one parent slot.
        const std::vector<int> pathParent(path.begin(), path.end() - 1);
        if (pathParent != parentPath) {
            return false;
        }
        indices.push_back(path.back());
    }
    std::sort(indices.begin(), indices.end());
    indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
    if (indices.empty()) {
        return false;
    }
    XmlNode group;
    group.name = "group";
    group.location = parent->children[static_cast<std::size_t>(indices.front())].location;
    for (int index : indices) {
        group.children.push_back(parent->children[static_cast<std::size_t>(index)]);
    }
    for (auto it = indices.rbegin(); it != indices.rend(); ++it) {
        parent->children.erase(parent->children.begin() + *it);
    }
    const int insertAt = indices.front();
    parent->children.insert(parent->children.begin() + insertAt, std::move(group));
    groupPath = parentPath;
    groupPath.push_back(insertAt);
    SyncTextFromRoot();
    return true;
}

bool DesignDocument::UngroupNode(const std::vector<int>& path) {
    if (!textSynced || path.empty()) {
        return false;
    }
    std::vector<int> parentPath(path.begin(), path.end() - 1);
    const int childIndex = path.back();
    XmlNode* parent = MutableNode(parentPath);
    if (parent == nullptr || childIndex < 0 || childIndex >= static_cast<int>(parent->children.size())) {
        return false;
    }
    XmlNode& node = parent->children[static_cast<std::size_t>(childIndex)];
    if (node.name != "group") {
        return false;
    }
    std::vector<XmlNode> lifted = std::move(node.children);
    parent->children.erase(parent->children.begin() + childIndex);
    parent->children.insert(parent->children.begin() + childIndex, std::make_move_iterator(lifted.begin()),
                            std::make_move_iterator(lifted.end()));
    SyncTextFromRoot();
    return true;
}

bool DesignDocument::PlaceNode(const std::vector<int>& path, float x, float y, float width, float height) {
    if (!textSynced || path.empty()) {
        return false;
    }
    XmlNode* node = MutableNode(path);
    if (node == nullptr) {
        return false;
    }
    const auto pixels = [](float value) {
        const int rounded = static_cast<int>(std::lround(value));
        return std::to_string(rounded < 1 ? 1 : rounded);
    };
    AddAttribute(*node, "position", "absolute");
    AddAttribute(*node, "x", pixels(x));
    AddAttribute(*node, "y", pixels(y));
    AddAttribute(*node, "width", pixels(width));
    AddAttribute(*node, "height", pixels(height));
    SyncTextFromRoot();
    return true;
}

bool DesignDocument::DeleteNode(const std::vector<int>& path) {
    if (!textSynced || path.empty()) {
        return false;
    }
    std::vector<int> parentPath = path;
    const int childIndex = parentPath.back();
    parentPath.pop_back();
    XmlNode* parent = MutableNode(parentPath);
    if (parent == nullptr || childIndex < 0 || childIndex >= static_cast<int>(parent->children.size())) {
        return false;
    }
    if (parent->name == "screens" && parent->children[static_cast<std::size_t>(childIndex)].name == "window") {
        int windows = 0;
        for (const XmlNode& child : parent->children) {
            if (child.name == "window") {
                ++windows;
            }
        }
        if (windows <= 1) {
            return false;
        }
    }
    parent->children.erase(parent->children.begin() + childIndex);
    SyncTextFromRoot();
    return true;
}

bool DesignDocument::MoveNode(const std::vector<int>& path, int direction) {
    if (!textSynced || path.empty() || direction == 0) {
        return false;
    }
    std::vector<int> parentPath = path;
    const int childIndex = parentPath.back();
    parentPath.pop_back();
    XmlNode* parent = MutableNode(parentPath);
    if (parent == nullptr) {
        return false;
    }
    const int destination = childIndex + (direction < 0 ? -1 : 1);
    if (childIndex < 0 || destination < 0 || childIndex >= static_cast<int>(parent->children.size()) ||
        destination >= static_cast<int>(parent->children.size())) {
        return false;
    }
    std::swap(parent->children[static_cast<std::size_t>(childIndex)], parent->children[static_cast<std::size_t>(destination)]);
    SyncTextFromRoot();
    return true;
}

bool DesignDocument::ReplaceText(std::string xmlText) {
    text = std::move(xmlText);
    SourceFile source;
    source.path = filePath.empty() ? "RoboUIDesigner.xml" : filePath;
    source.text = text;
    ParseResult parsed = ParseXml(source);
    parseDiagnostics = std::move(parsed.diagnostics);
    if (!parsed.root.has_value()) {
        textSynced = false;
        return false;
    }
    const bool reshape = parsed.root->name == "screens" || (parsed.root->name == "window" && ContainsWindow(*parsed.root));
    root = std::move(*parsed.root);
    NormalizeRoots(root);
    if (reshape) {
        SyncTextFromRoot();
    } else {
        textSynced = true;
    }
    return true;
}

std::string DesignDocument::NodeXml(const std::vector<int>& path) const {
    const XmlNode* node = NodeAt(path);
    if (node == nullptr) {
        return {};
    }
    return SerializeXml(*node);
}

bool DesignDocument::PasteXml(const std::vector<int>& parentPath, const std::string& xml, std::vector<int>& insertedPath) {
    if (!textSynced) {
        return false;
    }
    XmlNode* parent = MutableNode(parentPath);
    if (parent == nullptr || !AcceptsChildren(*parent)) {
        return false;
    }
    SourceFile source;
    source.path = filePath;
    source.text = xml;
    ParseResult parsed = ParseXml(source);
    if (!parsed.root.has_value()) {
        return false;
    }
    const bool rootComponent = parsed.root->name == "window" || parsed.root->name == "board";
    if (rootComponent && parent->name != "screens") {
        return false;
    }
    if (parent->name == "screens" && !rootComponent) {
        return false;
    }
    parent->children.push_back(std::move(*parsed.root));
    insertedPath = parentPath;
    insertedPath.push_back(static_cast<int>(parent->children.size()) - 1);
    SyncTextFromRoot();
    return true;
}

bool DesignDocument::AddBoardContent(const std::vector<int>& boardPath, const std::vector<int>& windowPath) {
    if (!textSynced) {
        return false;
    }
    XmlNode* board = MutableNode(boardPath);
    XmlNode* window = MutableNode(windowPath);
    if (board == nullptr || window == nullptr || board->name != "board" || window->name != "window" || board->children.empty()) {
        return false;
    }
    for (const XmlNode& child : board->children) {
        window->children.push_back(child);
    }
    SyncTextFromRoot();
    return true;
}

XmlNode* EnsureTokens(XmlNode& root) {
    for (XmlNode& child : root.children) {
        if (child.name == "tokens") {
            return &child;
        }
    }
    root.children.insert(root.children.begin(), XmlNode{});
    XmlNode* tokens = &root.children.front();
    tokens->name = "tokens";
    return tokens;
}

XmlNode* FindToken(XmlNode& tokens, std::string_view group, std::string_view name) {
    for (XmlNode& child : tokens.children) {
        if (child.name != group) {
            continue;
        }
        const XmlAttribute* attribute = FindAttribute(child, "name");
        if (attribute != nullptr && attribute->value == name) {
            return &child;
        }
    }
    return nullptr;
}

void EraseAttribute(XmlNode& node, std::string_view name) {
    for (std::size_t index = 0; index < node.attributes.size(); ++index) {
        if (node.attributes[index].name == name) {
            node.attributes.erase(node.attributes.begin() + static_cast<std::ptrdiff_t>(index));
            return;
        }
    }
}

bool DesignDocument::SetToken(std::string_view group, std::string_view name, std::string value) {
    if (!textSynced || !root.has_value() || group.empty() || name.empty() || value.empty()) {
        return false;
    }
    XmlNode* tokens = EnsureTokens(*TokenHost(*root));
    XmlNode* token = FindToken(*tokens, group, name);
    if (token == nullptr) {
        XmlNode child;
        child.name = std::string(group);
        AddAttribute(child, "name", std::string(name));
        AddAttribute(child, "value", std::move(value));
        tokens->children.push_back(std::move(child));
    } else {
        EraseAttribute(*token, "removed");
        AddAttribute(*token, "value", std::move(value));
    }
    SyncTextFromRoot();
    return true;
}

bool DesignDocument::RemoveToken(std::string_view group, std::string_view name, bool builtin) {
    if (!textSynced || !root.has_value() || group.empty() || name.empty()) {
        return false;
    }
    XmlNode* tokens = EnsureTokens(*TokenHost(*root));
    XmlNode* token = FindToken(*tokens, group, name);
    if (!builtin) {
        if (token == nullptr) {
            return false;
        }
        const std::size_t index = static_cast<std::size_t>(token - tokens->children.data());
        tokens->children.erase(tokens->children.begin() + static_cast<std::ptrdiff_t>(index));
        SyncTextFromRoot();
        return true;
    }
    if (token == nullptr) {
        XmlNode child;
        child.name = std::string(group);
        AddAttribute(child, "name", std::string(name));
        AddAttribute(child, "removed", "true");
        tokens->children.push_back(std::move(child));
    } else {
        EraseAttribute(*token, "value");
        AddAttribute(*token, "removed", "true");
    }
    SyncTextFromRoot();
    return true;
}

}  // namespace Runtime
