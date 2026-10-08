#pragma once

#include "engine/math/Geometry.h"
#include "rendering/text/TextStyle.h"

#include <string>
#include <vector>

namespace Runtime {

// One run of a paragraph: a stretch of text drawn with a style of its own, so a word can be bold,
// larger, or another colour inside a line that is none of those. The style of a run is the style of
// the block it sits in with the values that run wrote laid over it, which is what makes a run take
// the font of its block unless it says otherwise.
struct TextRun {
    // The text of the run as it is drawn: the case the block asked for is already applied, because
    // the case of a run is decided before the lines are broken.
    std::string text;
    TextStyle style;
};

// The box a paragraph of runs covers, and where each of its runs landed. The runs are placed by the
// line breaking of the whole paragraph, so a run is part of the line it sits on rather than a box
// beside it, and a run that wraps covers more than one line and is given the box around all of
// them. The boxes are written in the space of the paragraph, from the top-left corner of the box
// the paragraph was laid out in, and there is one box per run whatever the run holds: a run with no
// text in it covers nothing and is given a box of no size.
struct TextParagraph {
    Size size;
    std::vector<Rect> runs;
};

}  // namespace Runtime
