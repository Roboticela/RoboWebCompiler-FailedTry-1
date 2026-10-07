#include "app/Application.h"

#include "compiler/semantic/UiCompiler.h"
#include "engine/core/Log.h"
#include "platform/windows/Dx11Renderer.h"
#include "platform/windows/Win32Window.h"
#include "ui/element/Element.h"
#include "ui/factory/UiFactory.h"
#include "ui/input/PointerInput.h"
#include "xml/diagnostics/Diagnostic.h"

#include <memory>
#include <string>
#include <vector>

namespace Runtime {
namespace {

struct LiveSurface {
    std::unique_ptr<Element> root;
    Win32Window window;
    Dx11Renderer renderer;
    PointerInput input;
    Element* focused = nullptr;
    UiContext context;
    float clientWidth = 0.0f;
    float clientHeight = 0.0f;
    bool layoutDirty = true;
    bool paintDirty = true;
};

float MonitorWidthDip() {
    const float dpi = static_cast<float>(GetDpiForSystem());
    const float scale = dpi < 1.0f ? 1.0f : dpi / 96.0f;
    return static_cast<float>(GetSystemMetrics(SM_CXSCREEN)) / scale;
}

void CollectWindows(const IrNode& node, std::vector<const IrNode*>& windows) {
    if (node.name == "screens") {
        for (const IrNode& child : node.children) {
            if (child.kind == NodeKind::WINDOW) {
                windows.push_back(&child);
            }
        }
        return;
    }
    if (node.kind == NodeKind::WINDOW) {
        windows.push_back(&node);
    }
}

std::string WindowTitle(const IrNode& node) {
    if (const UiValue* value = FindProperty(node, "title")) {
        if (!value->text.empty()) {
            return value->text;
        }
    }
    if (const UiValue* value = FindProperty(node, "name")) {
        if (!value->text.empty()) {
            return value->text;
        }
    }
    return "Runtime";
}

float WindowLength(const IrNode& node, const char* name, float fallback) {
    const UiValue* value = FindProperty(node, name);
    if (value == nullptr || value->length.value < 1.0f) {
        return fallback;
    }
    return value->length.value;
}

}  // namespace

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

    const DeviceClass device = options.hasDevice ? options.device : DeviceClassForWidth(MonitorWidthDip());
    std::vector<const IrNode*> windows;
    CollectWindows(*compiled.root, windows);
    std::vector<const IrNode*> launching;
    for (const IrNode* window : windows) {
        if (WindowLaunchesOn(*window, device)) {
            launching.push_back(window);
        }
    }
    if (launching.empty()) {
        LogError("No window launches on this device.");
        return 1;
    }

    std::vector<std::unique_ptr<LiveSurface>> surfaces;
    surfaces.reserve(launching.size());
    for (const IrNode* node : launching) {
        auto surface = std::make_unique<LiveSurface>();
        LiveSurface* view = surface.get();
        std::unordered_map<std::string, std::function<void()>> clicks;
        for (const auto& entry : handlers) {
            clicks.emplace(entry.first, [view, handler = entry.second]() { handler(view->context); });
        }
        view->root = Instantiate(*node, clicks);
        if (view->root == nullptr) {
            LogError("Failed to instantiate a window.");
            return 1;
        }
        view->context.Bind(view->root.get());
        const std::string title = WindowTitle(*node);
        const float width = WindowLength(*node, "width", static_cast<float>(DEFAULT_WINDOW_WIDTH));
        const float height = WindowLength(*node, "height", static_cast<float>(DEFAULT_WINDOW_HEIGHT));
        if (!view->window.Create(ToWide(title), static_cast<int>(width), static_cast<int>(height))) {
            LogError("Failed to create a window.");
            return 1;
        }
        if (!view->renderer.Initialize(view->window.Hwnd(), view->window.PixelWidth(), view->window.PixelHeight())) {
            LogError("Failed to create the Direct3D 11 renderer.");
            return 1;
        }
        view->renderer.SetDpi(view->window.Dpi());
        view->clientWidth = view->window.ClientWidthDip();
        view->clientHeight = view->window.ClientHeightDip();
        view->window.SetResize([view](int pixelWidth, int pixelHeight) {
            view->renderer.Resize(pixelWidth, pixelHeight);
            view->renderer.SetDpi(view->window.Dpi());
            view->clientWidth = view->window.ClientWidthDip();
            view->clientHeight = view->window.ClientHeightDip();
            view->layoutDirty = true;
            view->paintDirty = true;
        });
        view->window.SetMouseMove([view](Point point) {
            if (view->root != nullptr && view->input.OnMove(*view->root, point)) {
                view->paintDirty = true;
            }
            view->window.SetHandCursor(view->input.HoverWantsHand());
        });
        view->window.SetMouseUp([view](Point point) {
            if (view->root != nullptr && view->input.OnUp(*view->root, point)) {
                view->paintDirty = true;
            }
        });
        view->window.SetMouseLeave([view]() {
            if (view->input.OnLeave()) {
                view->paintDirty = true;
            }
            view->window.SetHandCursor(false);
        });
        view->window.SetMouseDown([view](Point point) {
            if (view->root == nullptr) {
                return;
            }
            view->input.OnDown(*view->root, point);
            Element* hit = view->input.Pressed();
            if (view->focused != nullptr && view->focused != hit) {
                view->focused->SetFocused(false);
            }
            view->focused = nullptr;
            if (hit != nullptr && hit->TakesTextFocus()) {
                view->focused = hit;
                view->focused->SetFocused(true);
            }
            view->paintDirty = true;
        });
        view->window.SetMiddleDown([view](Point point) {
            if (view->root != nullptr) {
                view->input.OnDown(*view->root, point);
                view->paintDirty = true;
            }
        });
        view->window.SetMiddleUp([view](Point point) {
            if (view->root != nullptr && view->input.OnUp(*view->root, point, false)) {
                view->paintDirty = true;
            }
        });
        view->window.SetMouseWheel([view](Point point, int delta, int axis) {
            if (view->root == nullptr) {
                return;
            }
            const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            Element* hit = view->root->HitTest(point);
            if (hit == nullptr) {
                hit = view->root.get();
            }
            for (Element* node = hit; node != nullptr; node = node->Parent()) {
                if (node->OnWheel(delta, shift, axis)) {
                    view->layoutDirty = true;
                    view->paintDirty = true;
                    break;
                }
            }
        });
        view->window.SetKeyDown([view](unsigned key) {
            KeyEvent event;
            event.ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            event.shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            event.alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
            event.meta = (GetKeyState(VK_LWIN) & 0x8000) != 0 || (GetKeyState(VK_RWIN) & 0x8000) != 0;
            if (key == VK_RETURN) {
                event.key = "Enter";
                event.code = "Enter";
            } else if (key == VK_SPACE) {
                event.key = "Space";
                event.code = "Space";
            } else if (key == VK_TAB) {
                event.key = "Tab";
                event.code = "Tab";
            } else if (key >= 'A' && key <= 'Z') {
                event.key = std::string(1, static_cast<char>(key));
                event.code = "Key" + event.key;
            } else {
                event.key = std::to_string(key);
                event.code = event.key;
            }
            if (key == VK_TAB && view->root != nullptr) {
                view->focused = view->root->NextFocus(view->focused, event.shift);
                view->paintDirty = true;
                return true;
            }
            if (view->focused != nullptr && view->focused->OnKeyEvent(event)) {
                view->paintDirty = true;
                return true;
            }
            if (view->focused != nullptr && view->focused->OnKeyDown(key, event.shift)) {
                view->paintDirty = true;
                return true;
            }
            Element* start = view->input.Hovered();
            if (start == nullptr) {
                start = view->focused;
            }
            if (start == nullptr) {
                start = view->root.get();
            }
            for (Element* node = start; node != nullptr; node = node->Parent()) {
                if (node->OnKeyDown(key, event.shift)) {
                    view->layoutDirty = true;
                    view->paintDirty = true;
                    return true;
                }
            }
            return false;
        });
        view->window.SetChar([view](wchar_t character) {
            if (view->focused != nullptr && view->focused->OnChar(character)) {
                view->paintDirty = true;
            }
        });
        surfaces.push_back(std::move(surface));
    }

    if (!options.selfTest) {
        LogInfo(std::to_string(surfaces.size()) + " window(s) open. Press Esc to close one.");
    }

    bool running = true;
    bool selfTestClicked = false;
    int sizeWaits = 0;
    while (running) {
        surfaces.front()->window.Pump(running);
        if (!running) {
            break;
        }
        bool anyOpen = false;
        bool paintWaiting = true;
        bool animating = false;
        for (const std::unique_ptr<LiveSurface>& surface : surfaces) {
            if (surface->window.Hwnd() == nullptr || surface->root == nullptr) {
                continue;
            }
            anyOpen = true;
            if (surface->clientWidth < 1.0f || surface->clientHeight < 1.0f) {
                surface->clientWidth = surface->window.ClientWidthDip();
                surface->clientHeight = surface->window.ClientHeightDip();
                if (surface->clientWidth < 1.0f || surface->clientHeight < 1.0f) {
                    continue;
                }
                surface->layoutDirty = true;
            }
            if (surface->context.ConsumeInvalidated() || surface->root->TakeLayoutDirty()) {
                surface->layoutDirty = true;
                surface->paintDirty = true;
            }
            if (surface->root->Tick(1.0f / 60.0f)) {
                surface->paintDirty = true;
                animating = true;
            }
            if (surface->layoutDirty) {
                surface->root->SetViewport(surface->clientWidth, surface->clientHeight);
                surface->root->Measure(surface->renderer, surface->clientWidth, surface->clientHeight, true);
                surface->root->Arrange(Rect{0.0f, 0.0f, surface->clientWidth, surface->clientHeight});
                surface->layoutDirty = false;
            }
            if (options.selfTest && !selfTestClicked && surface.get() == surfaces.front().get()) {
                // A document may name the element the self-test clicks by giving it the id `mark`.
                // When it does not, the root stands for the document itself: it is laid out in the
                // box the window gave it, and a click at its centre is dispatched through the same
                // tree a human click would reach. Requiring every document to carry the id would
                // make the self-test fail on documents that are perfectly well laid out.
                Element* frame = surface->root->FindById("mark");
                if (frame == nullptr) {
                    frame = surface->root.get();
                }
                if (frame == nullptr || frame->Bounds().width < 1.0f || frame->Bounds().height < 1.0f) {
                    LogError("Self-test could not find a laid-out frame.");
                    return 1;
                }
                // What the run actually did is written down, so a passing self-test says which box
                // was found rather than only that one was.
                const std::string marked = frame->Id().empty() ? std::string() : " '" + frame->Id() + "'";
                LogInfo("Self-test clicked " + std::string(NodeKindName(frame->Kind())) + marked + " at its centre.");
                const Rect bounds = frame->Bounds();
                const Point center{bounds.x + bounds.width * 0.5f, bounds.y + bounds.height * 0.5f};
                surface->input.OnDown(*surface->root, center);
                surface->input.OnUp(*surface->root, center);
                selfTestClicked = true;
            }
            if (surface->paintDirty) {
                paintWaiting = false;
                surface->renderer.BeginFrame(COLOR_WINDOW_BACKGROUND);
                surface->root->Paint(surface->renderer);
                if (!surface->renderer.EndFrame()) {
                    return 1;
                }
                surface->paintDirty = false;
            }
        }
        if (!anyOpen) {
            ++sizeWaits;
            if (sizeWaits > 120) {
                LogError("The window client size stayed empty.");
                return 1;
            }
            continue;
        }
        sizeWaits = 0;
        if (options.selfTest && selfTestClicked) {
            return 0;
        }
        if (options.selfTest) {
            continue;
        }
        if (animating) {
            // Keep advancing an in-flight scroll or motion at about 60 frames a second
            // even while no input arrives, instead of spinning or stalling.
            surfaces.front()->window.WaitForMessage(16);
        } else if (paintWaiting) {
            surfaces.front()->window.WaitForMessage();
        }
    }
    return 0;
}

}  // namespace Runtime
