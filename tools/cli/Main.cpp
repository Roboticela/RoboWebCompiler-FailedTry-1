#include "app/Application.h"
#include "compiler/semantic/UiCompiler.h"
#include "xml/diagnostics/Diagnostic.h"

#include <filesystem>
#include <iostream>
#include <string>

namespace {

void PrintUsage() {
    std::cout << "myui check <file>\n"
              << "myui run <file> [mobile|tablet|desktop]\n"
              << "myui inspect <file>\n"
              << "myui new | build | format | test | package | profile\n";
}

int Check(const std::filesystem::path& path, bool inspect) {
    const Runtime::CompileResult compiled = Runtime::CompileUiFile(path);
    if (!compiled.diagnostics.empty()) {
        Runtime::PrintDiagnostics(compiled.source, compiled.diagnostics, std::cerr);
    }
    if (!compiled.Ok()) {
        return 1;
    }
    if (inspect && compiled.root.has_value()) {
        std::cout << Runtime::FormatIr(*compiled.root);
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        PrintUsage();
        return 2;
    }
    const std::string command = argv[1];
    if (command == "new" || command == "build" || command == "format" || command == "test" || command == "package" ||
        command == "profile") {
        std::cout << "myui " << command << " is planned for a later milestone.\n";
        return 2;
    }
    if (argc < 3) {
        PrintUsage();
        return 2;
    }
    const std::filesystem::path path = argv[2];
    if (command == "check") {
        return Check(path, false);
    }
    if (command == "inspect") {
        return Check(path, true);
    }
    if (command == "run") {
        Runtime::Application application;
        Runtime::RunOptions options;
        if (argc >= 4) {
            const std::string device = argv[3];
            options.hasDevice = true;
            if (device == "mobile") {
                options.device = Runtime::DeviceClass::MOBILE;
            } else if (device == "tablet") {
                options.device = Runtime::DeviceClass::TABLET;
            } else if (device == "desktop") {
                options.device = Runtime::DeviceClass::DESKTOP;
            } else {
                PrintUsage();
                return 2;
            }
        }
        return application.Run(path, {}, options);
    }
    PrintUsage();
    return 2;
}
