#pragma once

#include "rendering/text/TextStyle.h"

#include <string_view>

namespace Runtime {

struct ShapedText {
    float width = 0.0f;
    float height = 0.0f;
    bool rtl = false;
    bool shaped = false;
    bool fallback = false;
};

ShapedText ShapeText(std::string_view text, const TextStyle& style, float maxWidth);

}  // namespace Runtime
