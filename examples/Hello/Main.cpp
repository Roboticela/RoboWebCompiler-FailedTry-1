#include "app/Application.h"

#include "engine/core/Log.h"

#include <string>

#ifndef HELLO_XML_PATH
#define HELLO_XML_PATH "examples/Hello/Hello.xml"
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
    return application.Run(HELLO_XML_PATH, {}, options);
}
