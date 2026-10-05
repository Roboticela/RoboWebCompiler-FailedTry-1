#pragma once

#include "xml/ast/XmlAst.h"
#include "xml/diagnostics/Diagnostic.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Runtime {

class DesignDocument {
public:
    bool LoadFile(const std::filesystem::path& file);
    bool LoadText(std::string xmlText, std::string filePath);
    bool Save() const;

    const std::string& Text() const;
    std::string PreviewText() const;
    const std::string& FilePath() const;
    bool TextSynced() const;
    const XmlNode* Root() const;
    const std::vector<Diagnostic>& ParseDiagnostics() const;

    const XmlNode* NodeAt(const std::vector<int>& path) const;
    std::string Attribute(const std::vector<int>& path, std::string_view name) const;
    bool SetAttribute(const std::vector<int>& path, std::string_view name, std::string value);
    bool ClearAttribute(const std::vector<int>& path, std::string_view name);
    bool InsertChild(const std::vector<int>& parentPath, std::string_view componentName, std::vector<int>& insertedPath);
    bool DeleteNode(const std::vector<int>& path);
    bool MoveNode(const std::vector<int>& path, int direction);
    bool ReplaceText(std::string xmlText);
    std::string NodeXml(const std::vector<int>& path) const;
    bool PasteXml(const std::vector<int>& parentPath, const std::string& xml, std::vector<int>& insertedPath);
    bool SetToken(std::string_view group, std::string_view name, std::string value);
    bool RemoveToken(std::string_view group, std::string_view name, bool builtin);

private:
    XmlNode* MutableNode(const std::vector<int>& path);
    void SyncTextFromRoot();
    bool AcceptsChildren(const XmlNode& node) const;

    std::string filePath;
    std::string text;
    std::optional<XmlNode> root;
    std::vector<Diagnostic> parseDiagnostics;
    bool textSynced = false;
};

}  // namespace Runtime
