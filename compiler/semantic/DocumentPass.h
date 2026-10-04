#pragma once

#include "compiler/ir/UiIr.h"
#include "engine/theme/VisualStyle.h"
#include "xml/ast/XmlAst.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace Runtime {

struct DocumentModel {
    std::unordered_map<std::string, VisualStyle> styles;
    std::unordered_map<std::string, XmlNode> components;
    std::vector<std::pair<std::string, std::string>> states;
};

bool PrepareDocument(XmlNode& root, DocumentModel& model, std::vector<Diagnostic>& diagnostics);

}  // namespace Runtime
