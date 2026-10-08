#pragma once

#include "engine/math/Geometry.h"
#include "rendering/text/TextRun.h"
#include "rendering/text/TextStyle.h"

#include <string_view>
#include <vector>

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

    // The box a paragraph of runs covers, and where each of its runs landed. A paragraph is measured
    // as one block because that is how it is drawn: the runs of it break and wrap together, so a
    // bold word is a bold stretch of a line rather than a box of its own beside the others. The
    // width is the width the lines are held in, and the height is the box the paragraph is placed
    // in, which is what the alignment of the block is against. A height of zero or less is a box
    // that is only as tall as the lines are, and the runs are placed at its top.
    virtual TextParagraph MeasureParagraph(const std::vector<TextRun>& runs, const TextStyle& paragraph, float width, float height) = 0;
};

}  // namespace Runtime
