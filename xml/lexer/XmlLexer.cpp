#include "xml/lexer/XmlLexer.h"

#include <cctype>

namespace Runtime {
namespace {

bool IsNameStart(unsigned char character) {
    return std::isalpha(character) != 0 || character == '_';
}

bool IsNameContinue(unsigned char character) {
    return std::isalnum(character) != 0 || character == '_' || character == '-';
}

int HexValue(unsigned char character) {
    if (character >= '0' && character <= '9') {
        return character - '0';
    }
    if (character >= 'a' && character <= 'f') {
        return character - 'a' + 10;
    }
    if (character >= 'A' && character <= 'F') {
        return character - 'A' + 10;
    }
    return -1;
}

void AppendCodePoint(std::string& output, unsigned int codePoint) {
    if (codePoint <= 0x7F) {
        output.push_back(static_cast<char>(codePoint));
    } else if (codePoint <= 0x7FF) {
        output.push_back(static_cast<char>(0xC0 | ((codePoint >> 6) & 0x1F)));
        output.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
    } else if (codePoint <= 0xFFFF) {
        output.push_back(static_cast<char>(0xE0 | ((codePoint >> 12) & 0x0F)));
        output.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
        output.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
    } else {
        output.push_back(static_cast<char>(0xF0 | ((codePoint >> 18) & 0x07)));
        output.push_back(static_cast<char>(0x80 | ((codePoint >> 12) & 0x3F)));
        output.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
        output.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
    }
}

}  // namespace

XmlLexer::XmlLexer(std::string_view text, std::string path) : text(text), path(std::move(path)) {}

void XmlLexer::AdvanceByte() {
    if (index >= text.size()) {
        return;
    }
    const unsigned char character = static_cast<unsigned char>(text[index]);
    ++index;
    if (character == '\r') {
        ++line;
        column = 1;
        if (index < text.size() && text[index] == '\n') {
            ++index;
        }
        return;
    }
    if (character == '\n') {
        ++line;
        column = 1;
        return;
    }
    if ((character & 0xC0) != 0x80) {
        ++column;
    }
}

XmlToken XmlLexer::Make(XmlTokenKind kind, std::string tokenText, SourceLocation location, int spanLength) const {
    XmlToken token;
    token.kind = kind;
    token.text = std::move(tokenText);
    token.location = std::move(location);
    token.spanLength = spanLength;
    return token;
}

void XmlLexer::SkipIgnored() {
    while (index < text.size()) {
        const unsigned char character = static_cast<unsigned char>(text[index]);
        if (character == ' ' || character == '\t' || character == '\r' || character == '\n') {
            AdvanceByte();
            continue;
        }
        if (character == '<' && index + 3 < text.size() && text[index + 1] == '!' && text[index + 2] == '-' &&
            text[index + 3] == '-') {
            AdvanceByte();
            AdvanceByte();
            AdvanceByte();
            AdvanceByte();
            while (index + 2 < text.size() &&
                   !(text[index] == '-' && text[index + 1] == '-' && text[index + 2] == '>')) {
                AdvanceByte();
            }
            if (index + 2 < text.size()) {
                AdvanceByte();
                AdvanceByte();
                AdvanceByte();
            }
            continue;
        }
        if (character == '<' && index + 1 < text.size() && text[index + 1] == '?') {
            AdvanceByte();
            AdvanceByte();
            while (index + 1 < text.size() && !(text[index] == '?' && text[index + 1] == '>')) {
                AdvanceByte();
            }
            if (index + 1 < text.size()) {
                AdvanceByte();
                AdvanceByte();
            }
            continue;
        }
        break;
    }
}

XmlToken XmlLexer::ReadIdentifier(SourceLocation start) {
    const std::size_t begin = index;
    AdvanceByte();
    while (index < text.size() && IsNameContinue(static_cast<unsigned char>(text[index]))) {
        AdvanceByte();
    }
    const int span = column - start.column;
    return Make(XmlTokenKind::IDENTIFIER, std::string(text.substr(begin, index - begin)), start, span > 0 ? span : 1);
}

bool XmlLexer::DecodeEntity(std::string& output, SourceLocation& errorLocation, std::string& error) {
    const SourceLocation start{path, line, column};
    AdvanceByte();
    std::string name;
    if (index < text.size() && text[index] == '#') {
        AdvanceByte();
        unsigned int codePoint = 0;
        bool hex = false;
        if (index < text.size() && (text[index] == 'x' || text[index] == 'X')) {
            hex = true;
            AdvanceByte();
        }
        bool any = false;
        while (index < text.size() && text[index] != ';') {
            const int value = HexValue(static_cast<unsigned char>(text[index]));
            if (!hex && (text[index] < '0' || text[index] > '9')) {
                error = "Invalid numeric character reference.";
                errorLocation = start;
                return false;
            }
            if (hex && value < 0) {
                error = "Invalid numeric character reference.";
                errorLocation = start;
                return false;
            }
            any = true;
            if (hex) {
                codePoint = (codePoint << 4) + static_cast<unsigned int>(value);
            } else {
                codePoint = codePoint * 10 + static_cast<unsigned int>(text[index] - '0');
            }
            AdvanceByte();
        }
        if (!any || index >= text.size() || text[index] != ';') {
            error = "Unterminated character reference.";
            errorLocation = start;
            return false;
        }
        AdvanceByte();
        AppendCodePoint(output, codePoint);
        return true;
    }
    while (index < text.size() && std::isalnum(static_cast<unsigned char>(text[index])) != 0) {
        name.push_back(text[index]);
        AdvanceByte();
    }
    if (index >= text.size() || text[index] != ';') {
        error = "Unterminated character reference.";
        errorLocation = start;
        return false;
    }
    AdvanceByte();
    if (name == "lt") {
        output.push_back('<');
    } else if (name == "gt") {
        output.push_back('>');
    } else if (name == "amp") {
        output.push_back('&');
    } else if (name == "quot") {
        output.push_back('"');
    } else if (name == "apos") {
        output.push_back('\'');
    } else {
        error = "Unknown character reference '&" + name + ";'.";
        errorLocation = start;
        return false;
    }
    return true;
}

XmlToken XmlLexer::ReadString(SourceLocation start, char quote) {
    AdvanceByte();
    const SourceLocation valueLocation{path, line, column};
    std::string value;
    std::string error;
    SourceLocation errorLocation;
    while (index < text.size()) {
        const char character = text[index];
        if (character == quote) {
            AdvanceByte();
            return Make(XmlTokenKind::STRING, std::move(value), valueLocation,
                        static_cast<int>(value.size()) > 0 ? static_cast<int>(value.size()) : 1);
        }
        if (character == '\n' || character == '\r') {
            return Make(XmlTokenKind::FAILURE, "Unterminated string.", start, 1);
        }
        if (character == '&') {
            if (!DecodeEntity(value, errorLocation, error)) {
                return Make(XmlTokenKind::FAILURE, error, errorLocation, 1);
            }
            continue;
        }
        value.push_back(character);
        AdvanceByte();
    }
    return Make(XmlTokenKind::FAILURE, "Unterminated string.", start, 1);
}

XmlToken XmlLexer::Next() {
    SkipIgnored();
    if (index >= text.size()) {
        return Make(XmlTokenKind::END_OF_FILE, {}, SourceLocation{path, line, column}, 1);
    }
    const SourceLocation start{path, line, column};
    const char character = text[index];
    if (character == '<') {
        AdvanceByte();
        return Make(XmlTokenKind::OPEN_ANGLE, "<", start, 1);
    }
    if (character == '>') {
        AdvanceByte();
        return Make(XmlTokenKind::CLOSE_ANGLE, ">", start, 1);
    }
    if (character == '/') {
        AdvanceByte();
        return Make(XmlTokenKind::SLASH, "/", start, 1);
    }
    if (character == '=') {
        AdvanceByte();
        return Make(XmlTokenKind::EQUALS, "=", start, 1);
    }
    if (character == '"' || character == '\'') {
        return ReadString(start, character);
    }
    if (IsNameStart(static_cast<unsigned char>(character))) {
        return ReadIdentifier(start);
    }
    AdvanceByte();
    return Make(XmlTokenKind::FAILURE, std::string("Unexpected character '") + character + "'.", start, 1);
}

}  // namespace Runtime
