#include "app/Application.h"

#include "compiler/semantic/UiCompiler.h"
#include "engine/core/Log.h"
#include "platform/windows/Dx11Renderer.h"
#include "platform/windows/Win32Window.h"
#include "ui/element/Element.h"
#include "ui/factory/UiFactory.h"
#include "ui/input/PointerInput.h"
#include "xml/diagnostics/Diagnostic.h"

namespace Runtime {

void UiContext::Bind(Element* rootElement) {
    root = rootElement;
}

void UiContext::Invalidate() {
    invalidated = true;
}

bool UiContext::ConsumeInvalidated() {
    const bool value = invalidated;
    invalidated = false;
    return value;
}

Element* UiContext::Find(std::string_view id) const {
    return root == nullptr ? nullptr : root->FindById(id);
}

int Application::Run(const std::filesystem::path& xmlPath, const HandlerMap& handlers, RunOptions options) {
    CompileResult compiled = CompileUiFile(xmlPath);
    if (!compiled.Ok()) {
        PrintDiagnostics(compiled.source, compiled.diagnostics, std::cerr);
        return 1;
    }

    std::string title = "Runtime";
    float width = static_cast<float>(DEFAULT_WINDOW_WIDTH);
    float height = static_cast<float>(DEFAULT_WINDOW_HEIGHT);
    if (const UiValue* value = FindProperty(*compiled.root, "title")) {
        title = value->text;
    }
    if (const UiValue* value = FindProperty(*compiled.root, "width")) {
        width = value->length.value;
    }
    if (const UiValue* value = FindProperty(*compiled.root, "height")) {
        height = value->length.value;
    }

    UiContext context;
    std::unordered_map<std::string, std::function<void()>> clicks;
    for (const auto& entry : handlers) {
        clicks.emplace(entry.first, [&context, handler = entry.second]() { handler(context); });
    }
    std::unique_ptr<Element> root = Instantiate(*compiled.root, clicks);
    if (root == nullptr) {
        LogError("Failed to instantiate the UI.");
        return 1;
    }
    context.Bind(root.get());

    Win32Window window;
    if (!window.Create(ToWide(title), static_cast<int>(width), static_cast<int>(height))) {
        LogError("Failed to create the window.");
        return 1;
    }

    Dx11Renderer renderer;
    if (!renderer.Initialize(window.Hwnd(), window.PixelWidth(), window.PixelHeight())) {
        LogError("Failed to create the Direct3D 11 renderer.");
        return 1;
    }
    renderer.SetDpi(window.Dpi());

    float clientWidth = window.ClientWidthDip();
    float clientHeight = window.ClientHeightDip();
    bool layoutDirty = true;
    bool paintDirty = true;
    PointerInput input;

    window.SetResize([&](int pixelWidth, int pixelHeight) {
        renderer.Resize(pixelWidth, pixelHeight);
        renderer.SetDpi(window.Dpi());
        clientWidth = window.ClientWidthDip();
        clientHeight = window.ClientHeightDip();
        layoutDirty = true;
        paintDirty = true;
    });
    window.SetMouseMove([&](Point point) {
        if (input.OnMove(*root, point)) {
            paintDirty = true;
        }
        window.SetHandCursor(input.HoverWantsHand());
    });
    window.SetMouseUp([&](Point point) {
        if (input.OnUp(*root, point)) {
            paintDirty = true;
        }
    });
    window.SetMouseLeave([&]() {
        if (input.OnLeave()) {
            paintDirty = true;
        }
        window.SetHandCursor(false);
    });
    Element* focused = nullptr;
    window.SetMouseDown([&](Point point) {
        input.OnDown(*root, point);
        Element* hit = input.Pressed();
        if (focused != nullptr && focused != hit) {
            focused->SetFocused(false);
        }
        focused = nullptr;
        if (hit != nullptr && hit->TakesTextFocus()) {
            focused = hit;
            focused->SetFocused(true);
        }
        paintDirty = true;
    });
    window.SetMouseWheel([&](Point point, int delta) {
        Element* hit = root->HitTest(point);
        for (Element* node = hit; node != nullptr; node = node->Parent()) {
            if (node->OnWheel(delta)) {
                layoutDirty = true;
                paintDirty = true;
                break;
            }
        }
    });
    window.SetKeyDown([&](unsigned key) {
        if (focused != nullptr && focused->OnKeyDown(key)) {
            paintDirty = true;
            return true;
        }
        return false;
    });
    window.SetChar([&](wchar_t character) {
        if (focused != nullptr && focused->OnChar(character)) {
            paintDirty = true;
        }
    });

    if (!options.selfTest) {
        LogInfo("Runtime window is open. Press Esc to close.");
    }

    bool running = true;
    bool selfTestClicked = false;
    int sizeWaits = 0;
    while (running) {
        window.Pump(running);
        if (!running) {
            break;
        }
        if (clientWidth < 1.0f || clientHeight < 1.0f) {
            clientWidth = window.ClientWidthDip();
            clientHeight = window.ClientHeightDip();
            if (clientWidth < 1.0f || clientHeight < 1.0f) {
                ++sizeWaits;
                if (sizeWaits > 120) {
                    LogError("The window client size stayed empty.");
                    return 1;
                }
                continue;
            }
            layoutDirty = true;
        }
        if (context.ConsumeInvalidated() || root->TakeLayoutDirty()) {
            layoutDirty = true;
            paintDirty = true;
        }
        if (layoutDirty) {
            root->SetViewport(clientWidth, clientHeight);
            root->Measure(renderer, clientWidth, clientHeight, true);
            root->Arrange(Rect{0.0f, 0.0f, clientWidth, clientHeight});
            layoutDirty = false;
        }
        if (options.selfTest && !selfTestClicked) {
            Element* frame = root->FindById("mark");
            if (frame == nullptr || frame->Bounds().width < 1.0f || frame->Bounds().height < 1.0f) {
                LogError("Self-test could not find a laid-out frame.");
                return 1;
            }
            const Rect bounds = frame->Bounds();
            const Point center{bounds.x + bounds.width * 0.5f, bounds.y + bounds.height * 0.5f};
            input.OnDown(*root, center);
            input.OnUp(*root, center);
            selfTestClicked = true;
            continue;
        }
        if (paintDirty) {
            renderer.BeginFrame(COLOR_WINDOW_BACKGROUND);
            root->Paint(renderer);
            if (!renderer.EndFrame()) {
                return 1;
            }
            paintDirty = false;
            if (options.selfTest) {
                return 0;
            }
        } else if (!options.selfTest) {
            window.WaitForMessage();
        }
    }
    return 0;
}

}  // namespace Runtime
