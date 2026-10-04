#include "xml/serializer/XmlSerializer.h"

namespace Runtime {
namespace {

void AppendIndent(std::string& output, int depth) {
    for (int index = 0; index < depth; ++index) {
        output += "    ";
    }
}

void AppendEscaped(std::string& output, const std::string& value) {
    for (const char character : value) {
        switch (character) {
        case '&':
            output += "&amp;";
            break;
        case '"':
            output += "&quot;";
            break;
        case '<':
            output += "&lt;";
            break;
        case '>':
            output += "&gt;";
            break;
        default:
            output.push_back(character);
            break;
        }
    }
}

void WriteNode(std::string& output, const XmlNode& node, int depth) {
    AppendIndent(output, depth);
    output.push_back('<');
    output += node.name;
    for (const XmlAttribute& attribute : node.attributes) {
        output.push_back(' ');
        output += attribute.name;
        output += "=\"";
        AppendEscaped(output, attribute.value);
        output.push_back('"');
    }
    if (node.children.empty()) {
        output += "/>\n";
        return;
    }
    output += ">\n";
    for (const XmlNode& child : node.children) {
        WriteNode(output, child, depth + 1);
    }
    AppendIndent(output, depth);
    output += "</";
    output += node.name;
    output += ">\n";
}

}  // namespace

std::string SerializeXml(const XmlNode& root) {
    std::string output;
    WriteNode(output, root, 0);
    return output;
}

}  // namespace Runtime
