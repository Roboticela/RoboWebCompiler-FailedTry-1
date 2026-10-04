#pragma once

#include "engine/core/SourceLocation.h"

#include <string>
#include <string_view>

namespace Runtime {

enum class XmlTokenKind {
    END_OF_FILE,
    OPEN_ANGLE,
    CLOSE_ANGLE,
    SLASH,
    EQUALS,
    IDENTIFIER,
    STRING,
    FAILURE,
};

struct XmlToken {
    XmlTokenKind kind = XmlTokenKind::END_OF_FILE;
    std::string text;
    SourceLocation location;
    int spanLength = 1;
};

class XmlLexer {
public:
    XmlLexer(std::string_view text, std::string path);

    XmlToken Next();

private:
    void SkipIgnored();
    void AdvanceByte();
    XmlToken Make(XmlTokenKind kind, std::string tokenText, SourceLocation location, int spanLength) const;
    XmlToken ReadIdentifier(SourceLocation start);
    XmlToken ReadString(SourceLocation start, char quote);
    bool DecodeEntity(std::string& output, SourceLocation& errorLocation, std::string& error);

    std::string_view text;
    std::string path;
    std::size_t index = 0;
    int line = 1;
    int column = 1;
};

}  // namespace Runtime
