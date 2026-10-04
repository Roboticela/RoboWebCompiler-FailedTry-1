#pragma once

#include "engine/math/Color.h"
#include "engine/types/UiTypes.h"

#include <optional>
#include <string>
#include <string_view>

namespace Runtime {

std::string_view TrimCopyView(std::string_view text);

std::optional<Length> ParseLength(std::string_view text);
std::optional<float> ParseFloat(std::string_view text);
std::optional<Edges> ParseEdges(std::string_view text);
std::optional<Color> ParseColor(std::string_view text);
std::optional<bool> ParseBool(std::string_view text);
std::optional<Alignment> ParseAlignment(std::string_view text);
std::optional<float> ParseFontSize(std::string_view text);
std::optional<int> ParseInteger(std::string_view text);
std::optional<int> ParseZIndex(std::string_view text);
bool IsEventName(std::string_view text);
bool IsAction(std::string_view text);
bool IsBindingExpression(std::string_view text);
std::string BindingKey(std::string_view text);

}  // namespace Runtime
