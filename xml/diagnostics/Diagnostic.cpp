#include "xml/diagnostics/Diagnostic.h"

#include <fstream>
#include <sstream>

namespace Runtime {
namespace {

std::string_view LineText(const std::string& text, int lineNumber) {
    int line = 1;
    std::size_t start = 0;
    for (std::size_t index = 0; index <= text.size(); ++index) {
        if (index == text.size() || text[index] == '\n') {
            if (line == lineNumber) {
                std::size_t end = index;
                if (end > start && text[end - 1] == '\r') {
                    --end;
                }
                return std::string_view(text.data() + start, end - start);
            }
            start = index + 1;
            ++line;
        }
    }
    return {};
}

}  // namespace

std::optional<SourceFile> ReadSourceFile(const std::filesystem::path& path, std::string& error) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        error = "Cannot read file '" + path.string() + "'.";
        return std::nullopt;
    }
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    SourceFile source;
    source.path = path.string();
    source.text = buffer.str();
    if (source.text.size() >= 3 && static_cast<unsigned char>(source.text[0]) == 0xEF &&
        static_cast<unsigned char>(source.text[1]) == 0xBB &&
        static_cast<unsigned char>(source.text[2]) == 0xBF) {
        source.text.erase(0, 3);
    }
    return source;
}

bool HasFailure(const std::vector<Diagnostic>& diagnostics) {
    for (const Diagnostic& diagnostic : diagnostics) {
        if (diagnostic.severity == DiagnosticSeverity::FAILURE) {
            return true;
        }
    }
    return false;
}

void PrintDiagnostics(const SourceFile& source, const std::vector<Diagnostic>& diagnostics, std::ostream& output) {
    for (const Diagnostic& diagnostic : diagnostics) {
        const char* severity = "note";
        if (diagnostic.severity == DiagnosticSeverity::WARNING) {
            severity = "warning";
        } else if (diagnostic.severity == DiagnosticSeverity::FAILURE) {
            severity = "error";
        }
        output << diagnostic.location.path << ':' << diagnostic.location.line << ':' << diagnostic.location.column
               << '\n';
        output << severity << ' ' << diagnostic.code << ": " << diagnostic.message << '\n';
        if (diagnostic.location.line > 0) {
            const std::string_view line = LineText(source.text, diagnostic.location.line);
            output << line << '\n';
            int caretColumn = diagnostic.location.column;
            if (caretColumn < 1) {
                caretColumn = 1;
            }
            for (int index = 1; index < caretColumn; ++index) {
                output << ' ';
            }
            int span = diagnostic.spanLength < 1 ? 1 : diagnostic.spanLength;
            for (int index = 0; index < span; ++index) {
                output << '^';
            }
            output << '\n';
        }
    }
}

}  // namespace Runtime
