#include "designer/model/DesignDocument.h"

#include "compiler/semantic/ComponentCatalog.h"
#include "xml/parser/XmlParser.h"
#include "xml/serializer/XmlSerializer.h"

#include <fstream>

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

XmlNode MakeComponent(std::string_view name) {
    XmlNode node;
    node.name = std::string(name);
    if (name == "frame") {
        AddAttribute(node, "width", "160");
        AddAttribute(node, "height", "80");
        AddAttribute(node, "background", "#0E7AFF");
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
    parent->children.push_back(MakeComponent(componentName));
    insertedPath = parentPath;
    insertedPath.push_back(static_cast<int>(parent->children.size()) - 1);
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
    root = std::move(*parsed.root);
    textSynced = true;
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
    parent->children.push_back(std::move(*parsed.root));
    insertedPath = parentPath;
    insertedPath.push_back(static_cast<int>(parent->children.size()) - 1);
    SyncTextFromRoot();
    return true;
}

}  // namespace Runtime
