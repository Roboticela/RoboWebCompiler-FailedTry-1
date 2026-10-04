#pragma once

#include "xml/ast/XmlAst.h"
#include "xml/diagnostics/Diagnostic.h"

#include <optional>
#include <vector>

namespace Runtime {

struct ParseResult {
    std::optional<XmlNode> root;
    std::vector<Diagnostic> diagnostics;
};

ParseResult ParseXml(const SourceFile& source);

}  // namespace Runtime
