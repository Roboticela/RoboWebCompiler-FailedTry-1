#include "xml/parser/XmlParser.h"

#include "xml/lexer/XmlLexer.h"

namespace Runtime {
namespace {

class XmlParser {
public:
    explicit XmlParser(const SourceFile& source) : lexer(source.text, source.path) {
        Advance();
    }

    ParseResult Parse() {
        ParseResult result;
        if (current.kind == XmlTokenKind::END_OF_FILE) {
            Fail(result, "Document is empty.", current.location, 1);
            return result;
        }
        if (current.kind == XmlTokenKind::FAILURE) {
            Fail(result, current.text, current.location, current.spanLength);
            return result;
        }
        auto root = ParseElement(result);
        if (!root.has_value()) {
            return result;
        }
        if (current.kind == XmlTokenKind::FAILURE) {
            Fail(result, current.text, current.location, current.spanLength);
            return result;
        }
        if (current.kind != XmlTokenKind::END_OF_FILE) {
            Fail(result, "Unexpected content after the root element.", current.location, current.spanLength);
            return result;
        }
        if (!HasFailure(result.diagnostics)) {
            result.root = std::move(*root);
        }
        return result;
    }

private:
    void Advance() {
        if (lookAhead.has_value()) {
            current = *lookAhead;
            lookAhead.reset();
            return;
        }
        current = lexer.Next();
    }

    XmlToken PeekNext() {
        if (!lookAhead.has_value()) {
            lookAhead = lexer.Next();
        }
        return *lookAhead;
    }

    void Fail(ParseResult& result, std::string message, SourceLocation location, int span) const {
        Diagnostic diagnostic;
        diagnostic.severity = DiagnosticSeverity::FAILURE;
        diagnostic.code = DIAGNOSTIC_SYNTAX;
        diagnostic.message = std::move(message);
        diagnostic.location = std::move(location);
        diagnostic.spanLength = span;
        result.diagnostics.push_back(std::move(diagnostic));
    }

    bool Accept(XmlTokenKind kind) {
        if (current.kind != kind) {
            return false;
        }
        Advance();
        return true;
    }

    bool Expect(XmlTokenKind kind, const char* message, ParseResult& result) {
        if (Accept(kind)) {
            return true;
        }
        if (current.kind == XmlTokenKind::FAILURE) {
            Fail(result, current.text, current.location, current.spanLength);
        } else {
            Fail(result, message, current.location, current.spanLength);
        }
        return false;
    }

    std::optional<XmlNode> ParseElement(ParseResult& result) {
        if (!Expect(XmlTokenKind::OPEN_ANGLE, "Expected '<'.", result)) {
            return std::nullopt;
        }
        if (current.kind == XmlTokenKind::FAILURE) {
            Fail(result, current.text, current.location, current.spanLength);
            return std::nullopt;
        }
        if (current.kind != XmlTokenKind::IDENTIFIER) {
            Fail(result, "Expected an element name.", current.location, current.spanLength);
            return std::nullopt;
        }

        XmlNode node;
        node.name = current.text;
        node.location = current.location;
        Advance();

        while (current.kind == XmlTokenKind::IDENTIFIER) {
            XmlAttribute attribute;
            attribute.name = current.text;
            attribute.nameLocation = current.location;
            Advance();
            if (!Expect(XmlTokenKind::EQUALS, "Expected '=' after the attribute name.", result)) {
                return std::nullopt;
            }
            if (current.kind == XmlTokenKind::FAILURE) {
                Fail(result, current.text, current.location, current.spanLength);
                return std::nullopt;
            }
            if (current.kind != XmlTokenKind::STRING) {
                Fail(result, "Expected an attribute value.", current.location, current.spanLength);
                return std::nullopt;
            }
            attribute.value = current.text;
            attribute.valueLocation = current.location;
            attribute.valueSpan = current.spanLength;
            node.attributes.push_back(std::move(attribute));
            Advance();
        }

        if (current.kind == XmlTokenKind::FAILURE) {
            Fail(result, current.text, current.location, current.spanLength);
            return std::nullopt;
        }

        if (Accept(XmlTokenKind::SLASH)) {
            if (!Expect(XmlTokenKind::CLOSE_ANGLE, "Expected '/>' to close the element.", result)) {
                return std::nullopt;
            }
            return node;
        }

        if (!Expect(XmlTokenKind::CLOSE_ANGLE, "Expected '>' after the start tag.", result)) {
            return std::nullopt;
        }

        while (current.kind == XmlTokenKind::OPEN_ANGLE && PeekNext().kind != XmlTokenKind::SLASH) {
            auto child = ParseElement(result);
            if (!child.has_value()) {
                return std::nullopt;
            }
            node.children.push_back(std::move(*child));
        }

        if (current.kind == XmlTokenKind::FAILURE) {
            Fail(result, current.text, current.location, current.spanLength);
            return std::nullopt;
        }
        if (current.kind != XmlTokenKind::OPEN_ANGLE) {
            Fail(result, "Unclosed element '" + node.name + "'.", node.location, static_cast<int>(node.name.size()));
            return std::nullopt;
        }
        Advance();
        if (!Expect(XmlTokenKind::SLASH, "Expected an end tag.", result)) {
            return std::nullopt;
        }
        if (current.kind != XmlTokenKind::IDENTIFIER) {
            Fail(result, "Expected an end tag name.", current.location, current.spanLength);
            return std::nullopt;
        }
        if (current.text != node.name) {
            Fail(result, "Mismatched end tag. Expected '" + node.name + "' and found '" + current.text + "'.",
                current.location, current.spanLength);
            return std::nullopt;
        }
        Advance();
        if (!Expect(XmlTokenKind::CLOSE_ANGLE, "Expected '>' after the end tag.", result)) {
            return std::nullopt;
        }
        return node;
    }

    XmlLexer lexer;
    XmlToken current;
    std::optional<XmlToken> lookAhead;
};

}  // namespace

ParseResult ParseXml(const SourceFile& source) {
    XmlParser parser(source);
    return parser.Parse();
}

}  // namespace Runtime
