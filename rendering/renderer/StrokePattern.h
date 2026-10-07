#pragma once

#include <string_view>
#include <vector>

namespace Runtime {

// The longest run a dash pattern is allowed to hold, so a pattern written as a runaway number
// cannot ask the renderer for a dash longer than any box a document could draw.
inline constexpr float MAX_DASH_LENGTH = 512.0f;

// Reads a dash pattern out of the attribute that writes it: alternating painted and unpainted
// lengths in pixels, written as numbers in one string. One number is a dash length, and its gap is
// `gap`, or the dash itself when no gap was given. Several numbers alternate on and off, and an odd
// count repeats its first number so the pattern always closes a pair. A pattern with nothing
// painted in it, or nothing readable at all, is empty, and an empty pattern strokes solid.
//
// A line and a path are both stroked, so both read the same attribute the same way, which is why
// the reading lives here rather than inside either of them.
std::vector<float> ParseDashPattern(std::string_view text, float gap);

}  // namespace Runtime
