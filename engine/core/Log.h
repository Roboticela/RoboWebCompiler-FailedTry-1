#pragma once

#include <iostream>
#include <string_view>

namespace Runtime {

inline void LogInfo(std::string_view message) {
    std::cout << message << std::endl;
}

inline void LogError(std::string_view message) {
    std::cerr << message << std::endl;
}

}  // namespace Runtime
