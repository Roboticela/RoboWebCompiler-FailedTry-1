#include "compiler/ir/UiIr.h"

#include <sstream>

namespace Runtime {
namespace {

const char* AlignmentName(Alignment alignment) {
    switch (alignment) {
    case Alignment::START:
        return "start";
    case Alignment::CENTER:
        return "center";
    case Alignment::END:
        return "end";
    case Alignment::STRETCH:
        return "stretch";
    }
    return "start";
}

const char* LengthUnitSuffix(LengthUnit unit) {
    switch (unit) {
    case LengthUnit::PIXELS:
        return "px";
    case LengthUnit::PERCENT:
        return "%";
    case LengthUnit::FILL:
        return "fill";
    case LengthUnit::FIT:
        return "fit";
    }
    return "";
}

void WriteIr(std::ostringstream& output, const IrNode& node, int depth) {
    for (int index = 0; index < depth; ++index) {
        output << "  ";
    }
    output << node.name;
    if (!node.id.empty()) {
        output << " id=" << node.id;
    }
    for (const auto& property : node.properties) {
        if (property.first == "id") {
            continue;
        }
        output << ' ' << property.first << '=' << FormatValue(property.second);
    }
    output << '\n';
    for (const IrNode& child : node.children) {
        WriteIr(output, child, depth + 1);
    }
}

}  // namespace

std::string FormatValue(const UiValue& value) {
    std::ostringstream output;
    switch (value.kind) {
    case TypeKind::STRING:
    case TypeKind::EVENT_HANDLER:
        output << '"' << value.text << '"';
        break;
    case TypeKind::BOOL:
        output << (value.boolean ? "true" : "false");
        break;
    case TypeKind::INT:
    case TypeKind::FLOAT:
    case TypeKind::FONT_SIZE:
        output << value.number;
        break;
    case TypeKind::COLOR:
        output << "rgba(" << value.color.r << ',' << value.color.g << ',' << value.color.b << ',' << value.color.a
               << ')';
        break;
    case TypeKind::EDGES:
        output << value.edges.top.value << ' ' << value.edges.right.value << ' ' << value.edges.bottom.value << ' '
               << value.edges.left.value;
        break;
    case TypeKind::LENGTH:
        if (value.length.unit == LengthUnit::FILL || value.length.unit == LengthUnit::FIT) {
            output << LengthUnitSuffix(value.length.unit);
        } else if (value.length.unit == LengthUnit::PERCENT) {
            output << value.length.value << '%';
        } else {
            output << value.length.value << "px";
        }
        break;
    case TypeKind::ALIGNMENT:
        output << AlignmentName(value.alignment);
        break;
    }
    return output.str();
}

std::string FormatIr(const IrNode& node) {
    std::ostringstream output;
    WriteIr(output, node, 0);
    return output.str();
}

}  // namespace Runtime
