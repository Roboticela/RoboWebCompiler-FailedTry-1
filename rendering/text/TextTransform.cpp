#include "rendering/text/TextTransform.h"

#include <cctype>

namespace Runtime {

std::string TextInCase(std::string_view text, std::string_view textCase) {
    if (textCase.empty() || textCase == "none" || textCase == "original" || textCase == "mixed") {
        return std::string(text);
    }
    std::string result(text);
    if (textCase == "uppercase") {
        for (char& character : result) {
            character = static_cast<char>(std::toupper(static_cast<unsigned char>(character)));
        }
    } else if (textCase == "lowercase") {
        for (char& character : result) {
            character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
        }
    } else if (textCase == "capitalize") {
        bool word = true;
        for (char& character : result) {
            const unsigned char value = static_cast<unsigned char>(character);
            if (std::isspace(value)) {
                word = true;
            } else if (word) {
                character = static_cast<char>(std::toupper(value));
                word = false;
            } else {
                character = static_cast<char>(std::tolower(value));
            }
        }
    }
    return result;
}

}  // namespace Runtime
