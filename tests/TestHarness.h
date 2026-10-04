#pragma once

#include <iostream>

namespace Runtime {

struct TestContext {
    int failed = 0;

    void Check(bool condition, const char* expression, const char* file, int line) {
        if (condition) {
            return;
        }
        ++failed;
        std::cerr << file << ':' << line << " failed: " << expression << '\n';
    }
};

}  // namespace Runtime

#define CHECK(context, expression) (context).Check((expression), #expression, __FILE__, __LINE__)
