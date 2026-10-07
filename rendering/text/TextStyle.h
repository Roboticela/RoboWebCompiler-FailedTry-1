#pragma once

#include "engine/math/Color.h"
#include "engine/types/UiTypes.h"

#include <string>

namespace Runtime {

// Everything a block of text is drawn with. It lives apart from the renderer because the box a
// block covers is decided before anything is drawn: the measurer is handed the same style the
// draw gets, so the line height and the cap on the lines decide the height in both places.
struct TextStyle {
    float fontSize = DEFAULT_TEXT_FONT_SIZE;
    int fontWeight = 400;
    Color color = COLOR_TEXT;
    TextAlign horizontal = TextAlign::START;
    TextAlign vertical = TextAlign::START;
    std::string fontFamily = "Inter";
    std::string fontFile;
    std::string locale = "en-us";
    std::string decorationStyle = "solid";
    std::string fontFeatures;
    Color decorationColor = COLOR_TEXT;
    float letterSpacing = 0.0f;
    float wordSpacing = 0.0f;
    float lineHeight = 0.0f;
    float decorationWidth = 1.0f;
    int maxLines = 0;
    bool italic = false;
    bool rtl = false;
    bool wrap = true;
    bool ellipsis = false;
    bool underline = false;
    bool lineThrough = false;
    bool overline = false;
    bool verticalWriting = false;
};

}  // namespace Runtime
