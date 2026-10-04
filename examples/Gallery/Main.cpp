#include "app/Application.h"

#include "components/basic/TextBlock.h"
#include "engine/core/Log.h"

#include <string>

#ifndef GALLERY_XML_PATH
#define GALLERY_XML_PATH "examples/Gallery/Gallery.xml"
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
    handlers.emplace("gallery", [&handlerRan](Runtime::UiContext& context) {
        handlerRan = true;
        if (Runtime::TextBlock* status = context.FindText("status")) {
            status->SetText("Gallery handler ran");
        }
        context.Invalidate();
        Runtime::LogInfo("gallery handler ran");
    });

    Runtime::Application application;
    Runtime::RunOptions options;
    options.selfTest = selfTest;
    const int code = application.Run(GALLERY_XML_PATH, handlers, options);
    if (selfTest && !handlerRan) {
        Runtime::LogError("Self-test did not receive the button click.");
        return 1;
    }
    return code;
}
