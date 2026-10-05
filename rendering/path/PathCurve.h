#pragma once

#include <string_view>
#include <vector>

namespace Runtime {

struct PathSample {
    float x = 0.0f;
    float y = 0.0f;
};

struct PathContour {
    std::vector<PathSample> points;
    bool closed = false;
};

std::vector<PathSample> SamplePath(std::string_view data);
std::vector<PathContour> SampleContours(std::string_view data);

}  // namespace Runtime
