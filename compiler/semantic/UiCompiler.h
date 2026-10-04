#pragma once

#include "compiler/ir/UiIr.h"

#include <filesystem>

namespace Runtime {

CompileResult CompileUi(const SourceFile& source);
CompileResult CompileUiFile(const std::filesystem::path& path);

}  // namespace Runtime
