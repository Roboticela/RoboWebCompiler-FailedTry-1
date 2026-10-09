#pragma once

#include "engine/core/SourceLocation.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

namespace Runtime {

inline constexpr const char* DIAGNOSTIC_SYNTAX = "UI000";
inline constexpr const char* DIAGNOSTIC_UNKNOWN_COMPONENT = "UI001";
inline constexpr const char* DIAGNOSTIC_DUPLICATE_ID = "UI002";
inline constexpr const char* DIAGNOSTIC_CANNOT_READ_FILE = "UI007";
inline constexpr const char* DIAGNOSTIC_ROOT_WINDOW = "UI010";
inline constexpr const char* DIAGNOSTIC_TYPE_MISMATCH = "UI014";
inline constexpr const char* DIAGNOSTIC_VALUE_RANGE = "UI016";
inline constexpr const char* DIAGNOSTIC_UNEXPECTED_CHILD = "UI020";
inline constexpr const char* DIAGNOSTIC_DUPLICATE_PROPERTY = "UI021";
inline constexpr const char* DIAGNOSTIC_TREE_DEPTH = "UI022";
inline constexpr const char* DIAGNOSTIC_EVENT_SIGNATURE = "UI031";
inline constexpr const char* DIAGNOSTIC_BINDING = "UI040";
inline constexpr const char* DIAGNOSTIC_UNKNOWN_PROPERTY = "UI102";
// A child of a copy of a component that names neither a layer of its definition nor a place of it:
// nothing can place such a child, so it is reported rather than dropped in silence.
inline constexpr const char* DIAGNOSTIC_UNKNOWN_OVERRIDE = "UI105";
// A document that imports a document that imports it back, so neither can be read first.
inline constexpr const char* DIAGNOSTIC_IMPORT_CYCLE = "UI106";
// A document named by an import that cannot be borrowed from: its root is not a document.
inline constexpr const char* DIAGNOSTIC_IMPORT_ROOT = "UI107";
// A definition-shaped library that holds something a definition cannot be, so the entry is ignored.
inline constexpr const char* DIAGNOSTIC_IMPORT_IGNORED = "UI108";
// A link that names nowhere to go, or names a screen the document does not hold.
inline constexpr const char* DIAGNOSTIC_LINK_TARGET = "UI109";
inline constexpr const char* DIAGNOSTIC_LINK_UNKNOWN = "UI110";

enum class DiagnosticSeverity : std::uint8_t {
    NOTE,
    WARNING,
    FAILURE,
};

struct Diagnostic {
    DiagnosticSeverity severity = DiagnosticSeverity::FAILURE;
    std::string code;
    std::string message;
    SourceLocation location;
    int spanLength = 1;
};

struct SourceFile {
    std::string path;
    std::string text;
};

std::optional<SourceFile> ReadSourceFile(const std::filesystem::path& path, std::string& error);

bool HasFailure(const std::vector<Diagnostic>& diagnostics);

void PrintDiagnostics(const SourceFile& source, const std::vector<Diagnostic>& diagnostics, std::ostream& output);

// The same report over a document that was read from several files: each diagnostic is shown beside
// the file it was found in, which is the file its location names. A diagnostic whose file was not
// kept is reported without the line it points at rather than against the wrong one.
void PrintDiagnostics(const std::vector<SourceFile>& sources, const std::vector<Diagnostic>& diagnostics, std::ostream& output);

}  // namespace Runtime
