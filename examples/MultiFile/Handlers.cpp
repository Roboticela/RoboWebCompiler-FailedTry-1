#include "Handlers.h"

#include "components/text/TextElement.h"
#include "ui/element/Element.h"

namespace Multifile {
namespace {

// What the button does: it says so. The screen is told to lay itself out again, because the words it
// now holds are not the words it was measured with.
void Hello(Runtime::UiContext& context) {
    Runtime::Element* status = context.Find("statusText");
    if (status == nullptr) {
        return;
    }
    if (auto* text = dynamic_cast<Runtime::TextElement*>(status)) {
        text->SetText("Handled in Handlers.cpp");
        context.Invalidate();
    }
}

}  // namespace

Runtime::HandlerMap RegisterHandlers() {
    Runtime::HandlerMap handlers;
    handlers.emplace("hello", Hello);
    return handlers;
}

}  // namespace Multifile
