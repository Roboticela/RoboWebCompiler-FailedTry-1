#include "app/Application.h"

#include "components/basic/TextBlock.h"
#include "engine/core/Log.h"

#include <string>

#ifndef HELLO_XML_PATH
#define HELLO_XML_PATH "examples/Hello/Hello.xml"
#endif

int main(int argc, char** argv) {
    bool selfTest = false;
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--self-test") {
            selfTest = true;
        }
    }

    bool handlerRan = false;
    Runtime::HandlerMap handlers;
    handlers.emplace("hello", [&handlerRan](Runtime::UiContext& context) {
        handlerRan = true;
        if (Runtime::TextBlock* status = context.FindText("status")) {
            status->SetText("Hello from C++");
        }
        context.Invalidate();
        Runtime::LogInfo("hello handler ran");
    });

    Runtime::Application application;
    Runtime::RunOptions options;
    options.selfTest = selfTest;
    const int code = application.Run(HELLO_XML_PATH, handlers, options);
    if (selfTest && !handlerRan) {
        Runtime::LogError("Self-test did not receive the button click.");
        return 1;
    }
    return code;
}
