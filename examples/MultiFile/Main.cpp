#include "app/Application.h"

#include "Handlers.h"

#include <string>

// The document this program shows is one file, and that file is made of others: it imports its
// tokens, its style, and its component from Theme.xml, and it embeds Parts/Header.xml where the
// header stands. The program itself is three files for the same reason.
#ifndef MULTIFILE_XML_PATH
#define MULTIFILE_XML_PATH "examples/MultiFile/MultiFile.xml"
#endif

int main(int argc, char** argv) {
    bool selfTest = false;
    for (int index = 1; index < argc; ++index) {
        if (std::string(argv[index]) == "--self-test") {
            selfTest = true;
        }
    }
    Runtime::Application application;
    Runtime::RunOptions options;
    options.selfTest = selfTest;
    return application.Run(MULTIFILE_XML_PATH, Multifile::RegisterHandlers(), options);
}
