#pragma once

#include <string>

namespace Runtime {

struct SourceLocation {
    std::string path;
    int line = 1;
    int column = 1;
};

}  // namespace Runtime
