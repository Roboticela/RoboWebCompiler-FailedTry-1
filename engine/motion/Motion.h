#pragma once

#include "engine/theme/VisualStyle.h"

#include <string>
#include <vector>

namespace Runtime {

struct MotionSample {
    std::string target;
    std::string property;
    float number = 0.0f;
    bool hasNumber = false;
    std::string text;
};

std::vector<MotionSample> SampleMotions(const std::vector<MotionDecl>& motions, float time, int staggerIndex, bool hover);

}  // namespace Runtime
