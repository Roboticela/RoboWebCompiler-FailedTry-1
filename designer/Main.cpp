#include "designer/shell/DesignerApp.h"
#include "platform/windows/WicImageProvider.h"

#include <filesystem>
#include <iostream>
#include <string>

#ifndef DESIGNER_HELLO_XML
#define DESIGNER_HELLO_XML "examples/Hello/Hello.xml"
#endif

int main(int argc, char** argv) {
    // The designer previews documents that name pictures, so it offers the runtime the reader the
    // platform has before any of them is looked at.
    Runtime::RegisterWicImageProvider();
    Runtime::DesignerOptions options;
    options.file = DESIGNER_HELLO_XML;
    std::filesystem::path argumentPath;
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--self-test") {
            options.selfTest = true;
        } else {
            argumentPath = argument;
        }
    }
    if (!argumentPath.empty()) {
        std::filesystem::path folder = argumentPath.lexically_normal();
        if (folder.filename().empty()) {
            folder = folder.parent_path();
        }
        std::error_code error;
        if (std::filesystem::is_directory(folder, error)) {
            const std::string name = folder.filename().string();
            const std::filesystem::path xml = folder / (name + ".xml");
            const std::filesystem::path header = folder / (name + ".h");
            const std::filesystem::path source = folder / (name + ".cpp");
            if (!std::filesystem::is_regular_file(xml, error) || !std::filesystem::is_regular_file(header, error) ||
                !std::filesystem::is_regular_file(source, error)) {
                std::cerr << "Expected " << name << ".xml, " << name << ".h, and " << name << ".cpp in " << folder.string() << ".\n";
                return 1;
            }
            options.file = xml;
            options.folder = folder;
        } else {
            options.file = argumentPath;
        }
    }
    Runtime::DesignerApp designer;
    return designer.Run(options);
}
