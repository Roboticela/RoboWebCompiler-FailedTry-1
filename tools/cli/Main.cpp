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
              << "myui files <file>\n"
              << "myui new | build | format | test | package | profile\n";
}

int Check(const std::filesystem::path& path, bool inspect) {
    const Runtime::CompileResult compiled = Runtime::CompileUiFile(path);
    if (!compiled.diagnostics.empty()) {
        // A document may be read from several files, so every diagnostic is shown beside the file it
        // was found in.
        Runtime::PrintDiagnostics(compiled.sources, compiled.diagnostics, std::cerr);
    }
    if (!compiled.Ok()) {
        return 1;
    }
    if (inspect && compiled.root.has_value()) {
        std::cout << Runtime::FormatIr(*compiled.root);
    }
    return 0;
}

// The files a document is made of, in the order they were read, with the one the program named first.
// A document split across several files is one document, so what it is made of is worth being able to
// ask: it is the answer to "why is this element here", and the list a re-reading has to watch.
int Files(const std::filesystem::path& path) {
    const Runtime::CompileResult compiled = Runtime::CompileUiFile(path);
    if (!compiled.diagnostics.empty()) {
        Runtime::PrintDiagnostics(compiled.sources, compiled.diagnostics, std::cerr);
    }
    if (!compiled.sources.empty()) {
        std::cout << "0 " << compiled.sources.front().path << '\n';
        for (std::size_t index = 1; index < compiled.sources.size(); ++index) {
            std::cout << index << ' ' << compiled.sources[index].path << '\n';
        }
    }
    return compiled.Ok() ? 0 : 1;
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
    if (command == "files") {
        return Files(path);
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
