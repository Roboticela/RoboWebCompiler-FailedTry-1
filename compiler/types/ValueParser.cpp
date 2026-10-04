#include "compiler/types/ValueParser.h"

#include "engine/theme/DevKitTheme.h"

namespace Runtime {
namespace {

bool ParseFloatPrefix(std::string_view text, float& number, std::size_t& consumed) {
    if (text.empty()) {
        return false;
    }
    std::size_t index = 0;
    int sign = 1;
    if (text[index] == '+' || text[index] == '-') {
        if (text[index] == '-') {
            sign = -1;
        }
        ++index;
    }
    if (index >= text.size() || text[index] < '0' || text[index] > '9') {
        return false;
    }
    float value = 0.0f;
    while (index < text.size() && text[index] >= '0' && text[index] <= '9') {
        value = value * 10.0f + static_cast<float>(text[index] - '0');
        ++index;
    }
    if (index < text.size() && text[index] == '.') {
        ++index;
        float place = 0.1f;
        if (index >= text.size() || text[index] < '0' || text[index] > '9') {
            return false;
        }
        while (index < text.size() && text[index] >= '0' && text[index] <= '9') {
            value += static_cast<float>(text[index] - '0') * place;
            place *= 0.1f;
            ++index;
        }
    }
    number = value * static_cast<float>(sign);
    consumed = index;
    return true;
}

std::string ResolveToken(std::string_view text) {
    if (text == "$color.primary" || text == "$color.brand") {
        return "#0E7AFF";
    }
    if (text == "$color.primaryHover" || text == "$color.brandHover") {
        return "#0A6AE8";
    }
    if (text == "$color.primaryActive" || text == "$color.brandActive") {
        return "#0757C7";
    }
    if (text == "$color.onPrimary" || text == "$color.onBrand") {
        return "#FFFFFF";
    }
    if (text == "$color.secondary" || text == "$color.muted" || text == "$color.textSecondary") {
        return "#6E6E73";
    }
    if (text == "$color.foreground" || text == "$color.text") {
        return "#1D1D1F";
    }
    if (text == "$color.textTertiary") {
        return "#86868B";
    }
    if (text == "$color.background") {
        return "#FFFFFF";
    }
    if (text == "$color.surface") {
        return "#F5F5F7";
    }
    if (text == "$color.surfaceVariant") {
        return "#EDEDEF";
    }
    if (text == "$color.border") {
        return "#D2D2D7";
    }
    if (text == "$color.borderStrong") {
        return "#B8B8BD";
    }
    if (text == "$color.focus" || text == "$color.info") {
        return "#0E7AFF";
    }
    if (text == "$color.success") {
        return "#1D7A34";
    }
    if (text == "$color.warning") {
        return "#9A6700";
    }
    if (text == "$color.error" || text == "$color.danger") {
        return "#FF453A";
    }
    if (text == "$color.overlay") {
        return "#00000080";
    }
    if (text == "$space.0") {
        return "0";
    }
    if (text == "$space.xs" || text == "$space.1") {
        return "4";
    }
    if (text == "$space.sm" || text == "$space.2") {
        return "8";
    }
    if (text == "$space.md" || text == "$space.3") {
        return "12";
    }
    if (text == "$space.lg" || text == "$space.4") {
        return "16";
    }
    if (text == "$space.xl" || text == "$space.5") {
        return "24";
    }
    if (text == "$space.2xl" || text == "$space.6") {
        return "32";
    }
    if (text == "$space.3xl" || text == "$space.7") {
        return "48";
    }
    if (text == "$radius.none") {
        return "0";
    }
    if (text == "$radius.sm") {
        return "6";
    }
    if (text == "$radius.md") {
        return "8";
    }
    if (text == "$radius.lg") {
        return "12";
    }
    if (text == "$radius.xl") {
        return "16";
    }
    if (text == "$radius.pill" || text == "$radius.circle") {
        return "980";
    }
    if (text == "$type.display") {
        return "80";
    }
    if (text == "$type.h1" || text == "$type.heading1") {
        return "56";
    }
    if (text == "$type.h2" || text == "$type.heading2") {
        return "40";
    }
    if (text == "$type.h3" || text == "$type.heading3") {
        return "28";
    }
    if (text == "$type.title" || text == "$type.subhead") {
        return "24";
    }
    if (text == "$type.body") {
        return "17";
    }
    if (text == "$type.bodySmall" || text == "$type.label") {
        return "13";
    }
    if (text == "$type.caption" || text == "$type.code") {
        return "12";
    }
    if (text == "$weight.regular") {
        return "400";
    }
    if (text == "$weight.medium") {
        return "500";
    }
    if (text == "$weight.semibold") {
        return "600";
    }
    if (text == "$size.controlSmall") {
        return "32";
    }
    if (text == "$size.controlMedium") {
        return "44";
    }
    if (text == "$size.controlLarge") {
        return "50";
    }
    return std::string(text);
}

std::string_view ExpandToken(std::string_view text, std::string& storage) {
    text = TrimCopyView(text);
    if (!text.empty() && text.front() == '$') {
        storage = ResolveToken(text);
        return storage;
    }
    return text;
}

}  // namespace

std::string_view TrimCopyView(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) {
        text.remove_prefix(1);
    }
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t')) {
        text.remove_suffix(1);
    }
    return text;
}

bool IsBindingExpression(std::string_view text) {
    text = TrimCopyView(text);
    return text.size() >= 2 && text.front() == '{' && text.back() == '}';
}

std::string BindingKey(std::string_view text) {
    text = TrimCopyView(text);
    if (IsBindingExpression(text)) {
        text.remove_prefix(1);
        text.remove_suffix(1);
        text = TrimCopyView(text);
    }
    return std::string(text);
}

bool IsAction(std::string_view text) {
    text = TrimCopyView(text);
    auto named = [&](std::string_view prefix) {
        return text.size() > prefix.size() && text.substr(0, prefix.size()) == prefix && IsEventName(text.substr(prefix.size()));
    };
    if (named("show:") || named("hide:") || named("toggle:") || named("submit:")) {
        return true;
    }
    if (text.size() > 4 && text.substr(0, 4) == "set:") {
        const std::size_t eq = text.find('=');
        return eq != std::string_view::npos && eq > 4 && eq + 1 < text.size();
    }
    return false;
}

bool IsEventName(std::string_view text) {
    text = TrimCopyView(text);
    if (text.empty() || !((text.front() >= 'A' && text.front() <= 'Z') || (text.front() >= 'a' && text.front() <= 'z') ||
                          text.front() == '_')) {
        return false;
    }
    for (std::size_t index = 1; index < text.size(); ++index) {
        const char character = text[index];
        const bool ok = (character >= 'A' && character <= 'Z') || (character >= 'a' && character <= 'z') ||
                        (character >= '0' && character <= '9') || character == '_';
        if (!ok) {
            return false;
        }
    }
    return true;
}

std::optional<bool> ParseBool(std::string_view text) {
    text = TrimCopyView(text);
    if (text == "true") {
        return true;
    }
    if (text == "false") {
        return false;
    }
    return std::nullopt;
}

std::optional<Alignment> ParseAlignment(std::string_view text) {
    text = TrimCopyView(text);
    if (text == "start" || text == "left" || text == "top") {
        return Alignment::START;
    }
    if (text == "center") {
        return Alignment::CENTER;
    }
    if (text == "end" || text == "right" || text == "bottom") {
        return Alignment::END;
    }
    if (text == "stretch") {
        return Alignment::STRETCH;
    }
    return std::nullopt;
}

std::optional<Length> ParseLength(std::string_view text) {
    std::string tokenStorage;
    text = ExpandToken(text, tokenStorage);
    if (text == "fill") {
        return Length::Fill();
    }
    if (text == "fit") {
        return Length::Fit();
    }
    float number = 0.0f;
    std::size_t consumed = 0;
    if (!ParseFloatPrefix(text, number, consumed)) {
        return std::nullopt;
    }
    std::string_view suffix = TrimCopyView(text.substr(consumed));
    Length length;
    length.value = number;
    if (suffix.empty() || suffix == "px") {
        length.unit = LengthUnit::PIXELS;
        return length;
    }
    if (suffix == "%") {
        length.unit = LengthUnit::PERCENT;
        return length;
    }
    return std::nullopt;
}

std::optional<float> ParseFloat(std::string_view text) {
    std::string tokenStorage;
    text = ExpandToken(text, tokenStorage);
    float number = 0.0f;
    std::size_t consumed = 0;
    if (!ParseFloatPrefix(text, number, consumed) || consumed != text.size()) {
        return std::nullopt;
    }
    return number;
}

std::optional<Edges> ParseEdges(std::string_view text) {
    text = TrimCopyView(text);
    Length parts[4];
    int count = 0;
    while (!text.empty() && count < 4) {
        const std::size_t gap = text.find_first_of(" \t");
        const std::string_view token = gap == std::string_view::npos ? text : text.substr(0, gap);
        const std::optional<Length> length = ParseLength(token);
        if (!length.has_value()) {
            return std::nullopt;
        }
        parts[count] = *length;
        ++count;
        if (gap == std::string_view::npos) {
            text = {};
            break;
        }
        text = TrimCopyView(text.substr(gap));
    }
    if (count == 0 || !text.empty()) {
        return std::nullopt;
    }
    if (count == 1) {
        return Edges::All(parts[0]);
    }
    if (count == 2) {
        return Edges{parts[0], parts[1], parts[0], parts[1]};
    }
    if (count == 3) {
        return Edges{parts[0], parts[1], parts[2], parts[1]};
    }
    return Edges{parts[0], parts[1], parts[2], parts[3]};
}

std::optional<int> ParseInteger(std::string_view text) {
    std::string tokenStorage;
    text = ExpandToken(text, tokenStorage);
    if (text.empty()) {
        return std::nullopt;
    }
    int sign = 1;
    std::size_t index = 0;
    if (text[index] == '+' || text[index] == '-') {
        if (text[index] == '-') {
            sign = -1;
        }
        ++index;
    }
    if (index >= text.size() || text[index] < '0' || text[index] > '9') {
        return std::nullopt;
    }
    int number = 0;
    while (index < text.size() && text[index] >= '0' && text[index] <= '9') {
        number = number * 10 + (text[index] - '0');
        ++index;
    }
    if (index != text.size()) {
        return std::nullopt;
    }
    return number * sign;
}

std::optional<int> ParseZIndex(std::string_view text) {
    text = TrimCopyView(text);
    if (text == "desktop") {
        return Z_DESKTOP;
    }
    if (text == "window") {
        return Z_WINDOW;
    }
    if (text == "dock") {
        return Z_DOCK;
    }
    if (text == "menubar") {
        return Z_MENUBAR;
    }
    if (text == "notifications") {
        return Z_NOTIFICATIONS;
    }
    if (text == "spotlight" || text == "modal") {
        return Z_MODAL;
    }
    if (text == "contextMenu") {
        return Z_CONTEXT_MENU;
    }
    if (text == "tooltip") {
        return Z_TOOLTIP;
    }
    return ParseInteger(text);
}

std::optional<float> ParseFontSize(std::string_view text) {
    auto length = ParseLength(text);
    if (!length.has_value() || length->unit != LengthUnit::PIXELS) {
        return std::nullopt;
    }
    return length->value;
}

std::optional<Color> ParseColor(std::string_view text) {
    std::string tokenStorage;
    text = ExpandToken(text, tokenStorage);
    if (text == "black") {
        return Color{0.0f, 0.0f, 0.0f, 1.0f};
    }
    if (text == "white") {
        return Color{1.0f, 1.0f, 1.0f, 1.0f};
    }
    if (text == "red") {
        return Color{1.0f, 0.0f, 0.0f, 1.0f};
    }
    if (text == "green") {
        return Color{0.0f, 0.5f, 0.0f, 1.0f};
    }
    if (text == "blue") {
        return Color{0.0f, 0.0f, 1.0f, 1.0f};
    }
    if (text == "transparent") {
        return Color{0.0f, 0.0f, 0.0f, 0.0f};
    }
    if (text == "gray" || text == "grey") {
        return Color{0.5f, 0.5f, 0.5f, 1.0f};
    }
    if (text.empty() || text.front() != '#') {
        return std::nullopt;
    }
    std::string_view hex = text.substr(1);
    auto nibble = [](char character) -> int {
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
    };
    auto channel = [&](std::size_t offset, int digits) -> float {
        int value = 0;
        for (int index = 0; index < digits; ++index) {
            const int part = nibble(hex[offset + static_cast<std::size_t>(index)]);
            if (part < 0) {
                return -1.0f;
            }
            value = (value << 4) | part;
        }
        const float maxValue = digits == 1 ? 15.0f : 255.0f;
        return static_cast<float>(value) / maxValue;
    };
    Color color;
    if (hex.size() == 3 || hex.size() == 4) {
        color.r = channel(0, 1);
        color.g = channel(1, 1);
        color.b = channel(2, 1);
        color.a = hex.size() == 4 ? channel(3, 1) : 1.0f;
    } else if (hex.size() == 6 || hex.size() == 8) {
        color.r = channel(0, 2);
        color.g = channel(2, 2);
        color.b = channel(4, 2);
        color.a = hex.size() == 8 ? channel(6, 2) : 1.0f;
    } else {
        return std::nullopt;
    }
    if (color.r < 0.0f || color.g < 0.0f || color.b < 0.0f || color.a < 0.0f) {
        return std::nullopt;
    }
    return color;
}

}  // namespace Runtime
