#pragma once

#include "xml/ast/XmlAst.h"

#include <string>

namespace Runtime {

std::string SerializeXml(const XmlNode& root);

}  // namespace Runtime
