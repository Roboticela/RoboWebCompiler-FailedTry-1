#pragma once

#include "compiler/ir/UiIr.h"

#include <functional>
#include <memory>
#include <unordered_map>

namespace Runtime {

class Element;

std::unique_ptr<Element> Instantiate(const IrNode& root, const std::unordered_map<std::string, std::function<void()>>& clicks);

}  // namespace Runtime
