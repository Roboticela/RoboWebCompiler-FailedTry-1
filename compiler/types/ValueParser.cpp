#include "compiler/types/ValueParser.h"

#include "compiler/semantic/Foundations.h"
#include "engine/theme/DevKitTheme.h"

#include <cctype>
#include <string>

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

// True when two keywords are the same word written differently: a keyword in a document is read
// the way every other one is, with case, spaces, dashes, and underscores carrying no meaning.
bool MatchesFolded(std::string_view left, std::string_view right) {
    const auto fold = [](std::string_view text) {
        std::string folded;
        for (unsigned char character : text) {
            if (character == '_' || character == '-' || character == ' ') {
                continue;
            }
            folded.push_back(static_cast<char>(std::tolower(character)));
        }
        return folded;
    };
    return fold(left) == fold(right);
}

std::string ResolveToken(std::string_view text) {
    if (text.size() > 1 && text.front() == '$') {
        const std::string key(text.substr(1));
        if (FoundationSuppressed(key)) {
            return std::string(text);
        }
        if (const char* value = FoundationValue(key)) {
            return value;
        }
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
    if (text == "$radius.none") {
        return "0";
    }
    if (text == "$type.body") {
        return "17";
    }
    if (text == "$type.bodySmall" || text == "$type.label") {
        return "13";
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
    if (text.size() > 6 && text.substr(0, 6) == "async ") {
        text.remove_prefix(6);
    } else if (text.size() > 9 && text.substr(0, 9) == "reactive ") {
        text.remove_prefix(9);
    } else if (!text.empty() && (text.front() == '=' || text.front() == '!')) {
        text.remove_prefix(1);
    }
    return std::string(TrimCopyView(text));
}

BindMode BindingModeOf(std::string_view text) {
    text = TrimCopyView(text);
    if (IsBindingExpression(text)) {
        text.remove_prefix(1);
        text.remove_suffix(1);
        text = TrimCopyView(text);
    }
    if (text.size() > 6 && text.substr(0, 6) == "async ") {
        return BindMode::ASYNC;
    }
    if (text.size() > 9 && text.substr(0, 9) == "reactive ") {
        return BindMode::REACTIVE;
    }
    if (!text.empty() && text.front() == '=') {
        return BindMode::TWO_WAY;
    }
    if (!text.empty() && text.front() == '!') {
        return BindMode::ONE_TIME;
    }
    if (text.find('+') != std::string_view::npos) {
        return BindMode::COMPUTED;
    }
    return BindMode::ONE_WAY;
}

bool IsVerb(std::string_view verb) {
    static const char* verbs[] = {"navigate", "back",     "forward", "open",      "close",    "toggle", "show",      "hide",
                                  "focus",    "blur",     "set",     "reset",     "animate",  "scrollTo", "select",  "deselect",
                                  "submit",   "copy",     "paste",   "download",  "openURL",  "showDialog", "showToast", "play",
                                  "pause",    "stop"};
    for (const char* candidate : verbs) {
        if (verb == candidate) {
            return true;
        }
    }
    return false;
}

bool IsActionCall(std::string_view text) {
    text = TrimCopyView(text);
    if (text.empty()) {
        return false;
    }
    std::size_t end = 0;
    while (end < text.size()) {
        const char character = text[end];
        const bool ok = (character >= 'A' && character <= 'Z') || (character >= 'a' && character <= 'z') ||
                        (character >= '0' && character <= '9') || character == '_';
        if (!ok) {
            break;
        }
        ++end;
    }
    if (end == 0 || !IsVerb(text.substr(0, end))) {
        return false;
    }
    text.remove_prefix(end);
    text = TrimCopyView(text);
    if (text.empty()) {
        return true;
    }
    if (text.front() != '(' || text.back() != ')') {
        return false;
    }
    int depth = 0;
    for (std::size_t index = 0; index < text.size(); ++index) {
        if (text[index] == '(') {
            ++depth;
        } else if (text[index] == ')') {
            --depth;
            if (depth < 0) {
                return false;
            }
        }
    }
    return depth == 0;
}

bool IsAction(std::string_view text) {
    text = TrimCopyView(text);
    if (text.find(';') != std::string_view::npos || text.find('(') != std::string_view::npos) {
        bool any = false;
        while (!text.empty()) {
            const std::size_t split = text.find(';');
            const std::string_view part = TrimCopyView(split == std::string_view::npos ? text : text.substr(0, split));
            if (!part.empty()) {
                any = true;
                if (!IsActionCall(part)) {
                    return false;
                }
            }
            if (split == std::string_view::npos) {
                break;
            }
            text.remove_prefix(split + 1);
            text = TrimCopyView(text);
        }
        return any;
    }
    if (IsActionCall(text)) {
        return true;
    }
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
    if (text == "fill" || text == "parent") {
        return Length::Fill();
    }
    if (text == "fit") {
        return Length::Fit();
    }
    if (text == "screen") {
        return Length::Screen();
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

std::optional<float> ParseLineHeight(std::string_view text) {
    // A line height is a multiple of the font size, or the word that leaves the choice to the font.
    // The word is answered as zero, the multiple no line can be shorter than: it is what the
    // renderer already reads as the spacing the font asks for, so a document that says "auto" and
    // one that says "0" mean the same thing rather than needing a second value in the tree.
    std::string tokenStorage;
    const std::string_view resolved = TrimCopyView(ExpandToken(text, tokenStorage));
    if (MatchesFolded(resolved, "auto")) {
        return 0.0f;
    }
    return ParseFloat(text);
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
