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

}  // namespace Runtime
