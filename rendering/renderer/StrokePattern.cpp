#include "rendering/renderer/StrokePattern.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <string>

namespace Runtime {
namespace {

std::string_view TrimView(std::string_view text) {
    std::size_t begin = 0;
    std::size_t end = text.size();
    const auto blank = [](char value) { return std::isspace(static_cast<unsigned char>(value)) != 0; };
    while (begin < end && blank(text[begin])) {
        ++begin;
    }
    while (end > begin && blank(text[end - 1])) {
        --end;
    }
    return text.substr(begin, end - begin);
}

}  // namespace

std::vector<float> ParseDashPattern(std::string_view value, float gap) {
    std::vector<float> dashes;
    const std::string text(TrimView(value));
    if (text.empty() || text == "none" || text == "solid") {
        return dashes;
    }
    std::vector<float> numbers;
    const char* scan = text.c_str();
    char* end = nullptr;
    while (*scan != '\0') {
        const float number = std::strtof(scan, &end);
        if (end == scan) {
            // A separator, or anything unreadable, moves the scan along; a pattern with no number
            // in it at all was already treated as solid above.
            ++scan;
            continue;
        }
        if (number > 0.0f) {
            numbers.push_back(std::min(number, MAX_DASH_LENGTH));
        }
        scan = end;
    }
    if (numbers.empty()) {
        return dashes;
    }
    if (numbers.size() == 1) {
        const float on = numbers.front();
        const float off = gap > 0.0f ? std::min(gap, MAX_DASH_LENGTH) : on;
        dashes = {on, off};
        return dashes;
    }
    dashes = numbers;
    if (dashes.size() % 2 != 0) {
        dashes.push_back(numbers.front());
    }
    return dashes;
}

}  // namespace Runtime
