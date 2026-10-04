#include "designer/shell/DesignerApp.h"

#include <string>

#ifndef DESIGNER_HELLO_XML
#define DESIGNER_HELLO_XML "examples/Hello/Hello.xml"
#endif

int main(int argc, char** argv) {
    Runtime::DesignerOptions options;
    options.file = DESIGNER_HELLO_XML;
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--self-test") {
            options.selfTest = true;
        } else {
            options.file = argument;
        }
    }
    Runtime::DesignerApp designer;
    return designer.Run(options);
}
