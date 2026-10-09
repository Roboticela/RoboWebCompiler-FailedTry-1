#include "xml/project/ProjectLoader.h"

#include "xml/parser/XmlParser.h"

#include <algorithm>
#include <system_error>
#include <unordered_map>
#include <unordered_set>

namespace Runtime {
namespace {

// How many documents deep an import may lead before the chain is refused. A chain this long is a
// mistake or a loop the cycle check cannot see, and either way it is not something to read.
constexpr int MAX_IMPORT_DEPTH = 32;

void AddDiagnostic(std::vector<Diagnostic>& diagnostics, std::string code, std::string message, SourceLocation location,
                   int span, DiagnosticSeverity severity = DiagnosticSeverity::FAILURE) {
    Diagnostic diagnostic;
    diagnostic.severity = severity;
    diagnostic.code = std::move(code);
    diagnostic.message = std::move(message);
    diagnostic.location = std::move(location);
    diagnostic.spanLength = span < 1 ? 1 : span;
    diagnostics.push_back(std::move(diagnostic));
}

const XmlAttribute* FindAttribute(const XmlNode& node, std::string_view name) {
    for (const XmlAttribute& attribute : node.attributes) {
        if (attribute.name == name) {
            return &attribute;
        }
    }
    return nullptr;
}

std::string AttributeText(const XmlNode& node, std::string_view name) {
    if (const XmlAttribute* attribute = FindAttribute(node, name)) {
        return attribute->value;
    }
    return {};
}

// The place a name is written, which is what a reader has to point at when the name is wrong. The
// value of the attribute is what is wrong, so it is pointed at rather than the element around it.
SourceLocation NameLocation(const XmlNode& node, std::string_view name) {
    if (const XmlAttribute* attribute = FindAttribute(node, name)) {
        return attribute->valueLocation;
    }
    return node.location;
}

int NameSpan(const XmlNode& node, std::string_view name) {
    if (const XmlAttribute* attribute = FindAttribute(node, name)) {
        return attribute->valueSpan;
    }
    return static_cast<int>(node.name.size());
}

// The name a file is known by, so that the same file named two ways is one file. A path that cannot
// be resolved is compared as it was written, which is enough to catch a name that repeats itself.
std::string CanonicalPath(const std::filesystem::path& path) {
    std::error_code error;
    const std::filesystem::path resolved = std::filesystem::weakly_canonical(path, error);
    if (!error) {
        return resolved.string();
    }
    return path.lexically_normal().string();
}

struct Loader {
    std::vector<SourceFile> sources;
    std::unordered_map<std::string, std::size_t> sourceIndex;
    // Every file fully read, so a file embedded twice is read once, and the same file imported by two
    // documents is one library rather than two.
    std::unordered_map<std::string, XmlNode> processed;
    // The files whose definitions were already borrowed, so a document reached by two paths does not
    // offer the same definitions twice.
    std::unordered_set<std::string> borrowed;
    // The files being read right now, which is what a name leading back into itself is caught by.
    std::vector<std::string> reading;
    std::vector<Diagnostic> diagnostics;
};

void RecordSource(Loader& loader, const SourceFile& source) {
    if (loader.sourceIndex.count(source.path) != 0) {
        return;
    }
    loader.sourceIndex.emplace(source.path, loader.sources.size());
    loader.sources.push_back(source);
}

bool InReadStack(const Loader& loader, const std::string& path) {
    return std::find(loader.reading.begin(), loader.reading.end(), path) != loader.reading.end();
}

bool InlineReferences(XmlNode& node, const std::filesystem::path& directory, Loader& loader, int depth);

// Reads one named file, resolving every document it names in turn, and answers with the file's root
// or nothing when it could not be read. The place the name was written is reported against, so a
// name that leads nowhere is pointed at the name rather than at the file it would have named.
const XmlNode* ReadNamed(const std::string& name, const XmlNode& from, const std::filesystem::path& directory,
                         Loader& loader, int depth) {
    const SourceLocation location = NameLocation(from, SOURCE_ATTRIBUTE);
    const int span = NameSpan(from, SOURCE_ATTRIBUTE);
    if (name.empty()) {
        AddDiagnostic(loader.diagnostics, DIAGNOSTIC_IMPORT_ROOT, "A document named here has no name.", location, span);
        return nullptr;
    }
    if (depth > MAX_IMPORT_DEPTH) {
        AddDiagnostic(loader.diagnostics, DIAGNOSTIC_TREE_DEPTH,
                      "Documents named by other documents are nested too deeply.", location, span);
        return nullptr;
    }
    // The name is joined to the folder of the file that wrote it and then written as one path, so a
    // name written with a slash reads the same as one written without, and the path a diagnostic
    // shows is the path of the file rather than the two pieces it was built out of.
    const std::filesystem::path path =
        (directory.empty() ? std::filesystem::path(name) : directory / name).lexically_normal();
    const std::string key = CanonicalPath(path);
    if (InReadStack(loader, key)) {
        AddDiagnostic(loader.diagnostics, DIAGNOSTIC_IMPORT_CYCLE,
                      "'" + name + "' is already being read, so it cannot be read here.", location, span);
        return nullptr;
    }
    const auto cached = loader.processed.find(key);
    if (cached != loader.processed.end()) {
        return &cached->second;
    }
    std::string error;
    std::optional<SourceFile> source = ReadSourceFile(path, error);
    if (!source.has_value()) {
        AddDiagnostic(loader.diagnostics, DIAGNOSTIC_CANNOT_READ_FILE, error, location, span);
        return nullptr;
    }
    RecordSource(loader, *source);
    ParseResult parsed = ParseXml(*source);
    loader.diagnostics.insert(loader.diagnostics.end(), parsed.diagnostics.begin(), parsed.diagnostics.end());
    if (!parsed.root.has_value() || HasFailure(parsed.diagnostics)) {
        return nullptr;
    }
    loader.reading.push_back(key);
    InlineReferences(*parsed.root, path.parent_path(), loader, depth + 1);
    loader.reading.pop_back();
    auto inserted = loader.processed.emplace(key, std::move(*parsed.root));
    return &inserted.first->second;
}

// Every definition a document holds, wherever it keeps it. A screen keeps its components inside the
// boxes it lays out rather than at the top of the file, so borrowing from a screen means reaching
// into those boxes: a definition is a definition wherever it stands, and what makes a screen
// different from a library is that its content is not borrowed, not that its definitions are harder
// to find. A definition is not reached into in turn, because what a set holds is read as the set is
// read, and taking the variants out of their set would take them away from the name they answer to.
void CollectDefinitions(const XmlNode& node, std::vector<XmlNode>& into) {
    for (const XmlNode& child : node.children) {
        if (IsDefinitionElement(child.name)) {
            into.push_back(child);
            continue;
        }
        CollectDefinitions(child, into);
    }
}

// The definitions a document offers to the documents that import it. A library is written to be
// borrowed from, so everything in it has to be a definition and anything else is reported; a screen
// may hold anything, and what is borrowed from it is every definition it holds.
void BorrowDefinitions(const XmlNode& root, std::vector<XmlNode>& into, const XmlNode& from, Loader& loader) {
    if (root.name != "library" && root.name != "window" && root.name != "board" && root.name != "screens") {
        AddDiagnostic(loader.diagnostics, DIAGNOSTIC_IMPORT_ROOT,
                      "'" + root.name + "' is not a document that can be borrowed from; a document is a 'library', a "
                      "'window', a 'board', or a 'screens'.",
                      NameLocation(from, SOURCE_ATTRIBUTE), NameSpan(from, SOURCE_ATTRIBUTE));
        return;
    }
    if (root.name != "library") {
        CollectDefinitions(root, into);
        return;
    }
    for (const XmlNode& child : root.children) {
        if (IsDefinitionElement(child.name)) {
            into.push_back(child);
            continue;
        }
        AddDiagnostic(loader.diagnostics, DIAGNOSTIC_IMPORT_IGNORED,
                      "'" + child.name + "' is not a definition and is ignored when this library is imported.",
                      child.location, static_cast<int>(child.name.size()), DiagnosticSeverity::WARNING);
    }
}

bool InlineReferences(XmlNode& node, const std::filesystem::path& directory, Loader& loader, int depth) {
    std::vector<XmlNode> children;
    children.reserve(node.children.size());
    for (XmlNode& child : node.children) {
        if (child.name == "import") {
            const std::string name = AttributeText(child, SOURCE_ATTRIBUTE);
            const XmlNode* root = ReadNamed(name, child, directory, loader, depth);
            if (root == nullptr) {
                continue;
            }
            const std::filesystem::path path =
                (directory.empty() ? std::filesystem::path(name) : directory / name).lexically_normal();
            const std::string key = CanonicalPath(path);
            if (!loader.borrowed.insert(key).second) {
                // What this document offers is already here, so borrowing it again would name every
                // definition twice.
                continue;
            }
            std::vector<XmlNode> definitions;
            BorrowDefinitions(*root, definitions, child, loader);
            for (XmlNode& definition : definitions) {
                definition.fromImport = true;
                children.push_back(std::move(definition));
            }
            continue;
        }
        if (child.name == "embed") {
            // An embed that names no file is a place waiting for one, so it holds what it declares and
            // inlines nothing. That is what lets a designer place an embed before the file it will
            // show exists, and what lets a document be laid out with a part still to be written.
            const std::string name = AttributeText(child, SOURCE_ATTRIBUTE);
            if (name.empty()) {
                InlineReferences(child, directory, loader, depth);
            } else {
                const XmlNode* root = ReadNamed(name, child, directory, loader, depth);
                if (root != nullptr) {
                    child.children = root->children;
                }
            }
            children.push_back(std::move(child));
            continue;
        }
        InlineReferences(child, directory, loader, depth);
        children.push_back(std::move(child));
    }
    node.children = std::move(children);
    return true;
}

}  // namespace

bool IsDefinitionElement(std::string_view name) {
    return name == "component" || name == "componentSet" || name == "tokens" || name == "style" || name == "state" ||
           name == "animate" || name == "animation";
}

ProjectLoad LoadProject(const SourceFile& entry, const std::filesystem::path& baseDirectory) {
    Loader loader;
    RecordSource(loader, entry);
    ProjectLoad load;
    ParseResult parsed = ParseXml(entry);
    loader.diagnostics.insert(loader.diagnostics.end(), parsed.diagnostics.begin(), parsed.diagnostics.end());
    if (parsed.root.has_value() && !HasFailure(parsed.diagnostics)) {
        std::string key;
        if (!entry.path.empty()) {
            key = CanonicalPath(entry.path);
        }
        loader.reading.push_back(key);
        InlineReferences(*parsed.root, baseDirectory, loader, 1);
        loader.reading.pop_back();
        load.root = std::move(*parsed.root);
    }
    load.sources = std::move(loader.sources);
    load.diagnostics = std::move(loader.diagnostics);
    return load;
}

}  // namespace Runtime
