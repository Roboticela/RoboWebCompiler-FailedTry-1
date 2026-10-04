#pragma once

#include <filesystem>
#include <string>

namespace Runtime {

struct DesignerOptions {
    bool selfTest = false;
    std::filesystem::path file;
};

class DesignerApp {
public:
    int Run(const DesignerOptions& options);
};

}  // namespace Runtime
