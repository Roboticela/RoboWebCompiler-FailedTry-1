#pragma once

#include "engine/math/Geometry.h"
#include "rendering/text/TextStyle.h"

#include <string_view>

namespace Runtime {

// The box a block of text covers. Measuring and drawing are one decision, not two: the style
// carries the line height and the cap on the lines, both of which decide the height of the box,
// so a measurer that is not told them cannot answer with the box the draw will fill.
class TextMeasurer {
public:
    virtual ~TextMeasurer() = default;
    virtual Size MeasureText(std::string_view text, const TextStyle& style, float maxWidth) = 0;

    // A block asked for by its size alone is measured with everything else left at the value the
    // platform draws with, which is all a caller that only knows a font size can say.
    Size MeasureText(std::string_view text, float fontSize, float maxWidth) {
        TextStyle style;
        style.fontSize = fontSize;
        return MeasureText(text, style, maxWidth);
    }
};

}  // namespace Runtime
