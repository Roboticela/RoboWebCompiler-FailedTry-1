#include "compiler/semantic/Suggestions.h"

#include <algorithm>
#include <vector>

namespace Runtime {

int EditDistance(std::string_view left, std::string_view right) {
    if (left.size() > 64 || right.size() > 64) {
        return 1000;
    }
    std::vector<int> previous(right.size() + 1);
    std::vector<int> current(right.size() + 1);
    for (std::size_t column = 0; column <= right.size(); ++column) {
        previous[column] = static_cast<int>(column);
    }
    for (std::size_t row = 1; row <= left.size(); ++row) {
        current[0] = static_cast<int>(row);
        for (std::size_t column = 1; column <= right.size(); ++column) {
            const int cost = left[row - 1] == right[column - 1] ? 0 : 1;
            current[column] = std::min(previous[column] + 1, std::min(current[column - 1] + 1, previous[column - 1] + cost));
        }
        std::swap(previous, current);
    }
    return previous[right.size()];
}

std::optional<std::string> SuggestName(std::string_view word, const std::vector<std::string>& options) {
    int bestDistance = 3;
    std::optional<std::string> best;
    for (const std::string& option : options) {
        const int distance = EditDistance(word, option);
        if (distance < bestDistance) {
            bestDistance = distance;
            best = option;
        }
    }
    return best;
}

}  // namespace Runtime
