#pragma once

#include "engine/math/Geometry.h"

#include <string_view>

namespace Runtime {

class TextMeasurer {
public:
    virtual ~TextMeasurer() = default;
    virtual Size MeasureText(std::string_view text, float fontSize, float maxWidth) = 0;
};

}  // namespace Runtime
