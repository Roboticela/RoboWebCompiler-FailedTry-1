#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Runtime {

int EditDistance(std::string_view left, std::string_view right);
std::optional<std::string> SuggestName(std::string_view word, const std::vector<std::string>& options);

}  // namespace Runtime
