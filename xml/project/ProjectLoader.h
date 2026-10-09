#pragma once

#include "xml/ast/XmlAst.h"
#include "xml/diagnostics/Diagnostic.h"

#include <filesystem>
#include <optional>
#include <string_view>
#include <vector>

namespace Runtime {

// A document may be written across several files, and a file may say that it is part of another one.
// Two elements do that, and both name the file they read with `src`, resolved beside the file that
// wrote it rather than beside wherever the program was started from:
//
//   `import`  borrows the definitions of another document: the components, sets, tokens, styles,
//             states, and animations it holds become available here, and nothing of it is drawn.
//   `embed`   inlines the content of another document at the place it stands, so the tree that is
//             laid out and drawn is one tree made of several files.
//
// So `library` is the root of a file that is written to be borrowed from: it holds definitions and
// nothing else, which is what makes it possible to read a library on its own as well as to import it.
bool IsDefinitionElement(std::string_view name);

// The filename an import, an embed, or a link writes.
inline constexpr const char* SOURCE_ATTRIBUTE = "src";

struct ProjectLoad {
    // The entry document, with every import replaced by what it borrowed and every embed inlined.
    std::optional<XmlNode> root;
    // Every file the entry document was read from, the entry first, so a diagnostic can be shown
    // beside the file it was found in rather than beside the file the program was started with.
    std::vector<SourceFile> sources;
    std::vector<Diagnostic> diagnostics;
};

// Reads a document and every document it imports or embeds. A name that leads back to a document
// already being read is a cycle: what it would mean is not defined, so it is refused rather than
// read until the stack runs out. `baseDirectory` is where the entry document's own names resolve.
ProjectLoad LoadProject(const SourceFile& entry, const std::filesystem::path& baseDirectory);

}  // namespace Runtime
