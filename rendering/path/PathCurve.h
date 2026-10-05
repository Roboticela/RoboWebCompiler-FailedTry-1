#pragma once

#include <string_view>
#include <vector>

namespace Runtime {

struct PathSample {
    float x = 0.0f;
    float y = 0.0f;
};

std::vector<PathSample> SamplePath(std::string_view data);

}  // namespace Runtime
