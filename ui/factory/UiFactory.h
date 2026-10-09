#pragma once

#include "compiler/ir/UiIr.h"

#include <functional>
#include <memory>
#include <unordered_map>

namespace Runtime {

class Element;

// What a program does with the names a link carries: the name the system opens, the screen of this
// document the link points at, and the frame it is opened in. A program that hands none gets links
// that are drawn and do nothing, which is what lets a document be shown before the program behind it
// exists.
using LinkHandler = std::function<void(const std::string& href, const std::string& to, const std::string& target)>;

// `documentRoot` is the tree a copy of a component is resolved against, which is the document rather
// than the screen being built: a definition borrowed from another file is part of the document and
// not of every screen that uses it. A null tree means the node being built is the document.
std::unique_ptr<Element> Instantiate(const IrNode& root, const std::unordered_map<std::string, std::function<void()>>& clicks,
                                     const LinkHandler& onLink = {}, const IrNode* documentRoot = nullptr);

}  // namespace Runtime
