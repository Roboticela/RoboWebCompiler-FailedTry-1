#include "app/Application.h"

#include "app/LinkRoute.h"
#include "compiler/semantic/UiCompiler.h"
#include "engine/core/Log.h"
#include "platform/windows/Dx11Renderer.h"
#include "platform/windows/WicImageProvider.h"
#include "platform/windows/Win32Window.h"
#include "ui/element/Element.h"
#include "ui/factory/UiFactory.h"
#include "ui/input/PointerInput.h"
#include "xml/diagnostics/Diagnostic.h"

// The shell is what opens a page, and it lives beside the window system rather than inside it: a
// program that draws its own windows still asks the machine's shell to open a name it was given.
#include <shellapi.h>

#include <memory>
#include <string>
#include <unordered_map>
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
    // The handlers the screen was built with, kept so that a screen shown in place of this one is
    // built with the same ones: navigating is not a reason for a program to stop answering.
    std::unordered_map<std::string, std::function<void()>> clicks;
    // The screen this window shows, by the id it was written with, so a link that names a screen can
    // tell which window already shows it.
    std::string screenId;
    float clientWidth = 0.0f;
    float clientHeight = 0.0f;
    bool layoutDirty = true;
    bool paintDirty = true;
};

std::string ScreenId(const IrNode& node) {
    if (const UiValue* value = FindProperty(node, "id")) {
        return value->text;
    }
    return {};
}

// Every screen a document holds, by the id it answers to. A link may name any of them, including the
// ones that do not launch on this device: what a link does is show the screen, and a screen that was
// not shown at startup is exactly the one worth being able to reach.
void CollectScreens(const IrNode& node, std::unordered_map<std::string, const IrNode*>& screens) {
    if (node.kind == NodeKind::WINDOW || node.kind == NodeKind::BOARD) {
        const std::string id = ScreenId(node);
        if (!id.empty()) {
            screens.emplace(id, &node);
        }
    }
    for (const IrNode& child : node.children) {
        CollectScreens(child, screens);
    }
}

// The windows of one document and the screens it holds: what a link is answered with. A link is
// answered after the input that asked for it has finished rather than while it is being handled,
// because showing a screen takes the element that was clicked out of the tree, and an element cannot
// be taken out from under itself.
struct Stage {
    std::vector<std::unique_ptr<LiveSurface>> surfaces;
    std::unordered_map<std::string, const IrNode*> screens;
    const IrNode* document = nullptr;
    LiveSurface* pendingSurface = nullptr;
    std::string pendingScreen;
    bool pendingRaise = false;
    // How many links have been answered, so a run that clicks one can tell that something answered it
    // rather than that nothing did.
    int answeredLinks = 0;
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

LinkHandler MakeLinkHandler(Stage& stage, LiveSurface& view);

// Shows a screen in the window a link stands in. The old tree is let go only once the new one is
// built, so a screen that cannot be built leaves the window as it was rather than blank.
void ShowScreen(Stage& stage, LiveSurface& view, const IrNode& screen) {
    std::unique_ptr<Element> built = Instantiate(screen, view.clicks, MakeLinkHandler(stage, view), stage.document);
    if (built == nullptr) {
        LogError("A screen named by a link could not be built.");
        return;
    }
    // The pointers the pointer input keeps are into the tree that is about to go, so they are let go
    // before it does rather than left pointing at what is no longer there.
    view.input.OnLeave();
    view.focused = nullptr;
    view.root = std::move(built);
    view.screenId = ScreenId(screen);
    view.context.Bind(view.root.get());
    view.layoutDirty = true;
    view.paintDirty = true;
    const std::string shown = ScreenId(screen);
    LogInfo("Showed screen '" + shown + "'.");
    if (view.window.Hwnd() != nullptr) {
        SetWindowTextW(view.window.Hwnd(), ToWide(WindowTitle(screen)).c_str());
    }
}

// What a link is answered with. A page is opened with the system, which is the only thing a name
// outside this document can mean; a screen is shown or brought forward, which is a window the runtime
// already has. Neither is done here: what is done here is written down, and it is done after the input
// that asked for it has finished.
LinkHandler MakeLinkHandler(Stage& stage, LiveSurface& view) {
    return [&stage, &view](const std::string& href, const std::string& to, const std::string& target) {
        ++stage.answeredLinks;
        const LinkRoute route = RouteLink(href, to, target);
        if (route.action == LinkAction::OPEN) {
            const std::wstring page = ToWide(route.name);
            SHELLEXECUTEINFOW request{};
            request.cbSize = sizeof(request);
            // A name that opens nowhere is a mistake in a document rather than something to say to the
            // person who clicked, so the shell is asked not to raise anything of its own about it: the
            // failure is written down where the program's other troubles are written down.
            request.fMask = SEE_MASK_FLAG_NO_UI;
            request.lpVerb = L"open";
            request.lpFile = page.c_str();
            request.nShow = SW_SHOWNORMAL;
            if (!ShellExecuteExW(&request)) {
                LogError("A link could not be opened: " + route.name);
            } else {
                LogInfo("Opened '" + route.name + "'.");
            }
            return;
        }
        if (route.action == LinkAction::NONE) {
            return;
        }
        stage.pendingSurface = &view;
        stage.pendingScreen = route.name;
        stage.pendingRaise = route.action == LinkAction::RAISE;
    };
}

// The link a person clicked, answered once the click is over. A screen asked for in a window of its
// own is brought forward when a window already shows it, and shown in the window the link stood in
// when none does -- a link that does nothing because its screen happens to be closed is not a link.
void ApplyPendingLink(Stage& stage) {
    if (stage.pendingSurface == nullptr || stage.pendingScreen.empty()) {
        return;
    }
    LiveSurface* view = stage.pendingSurface;
    const std::string id = stage.pendingScreen;
    const bool raise = stage.pendingRaise;
    stage.pendingSurface = nullptr;
    stage.pendingScreen.clear();
    stage.pendingRaise = false;
    if (raise) {
        for (const std::unique_ptr<LiveSurface>& other : stage.surfaces) {
            if (other.get() == view || other->screenId != id || other->window.Hwnd() == nullptr) {
                continue;
            }
            ShowWindow(other->window.Hwnd(), SW_RESTORE);
            SetForegroundWindow(other->window.Hwnd());
            return;
        }
    }
    const auto found = stage.screens.find(id);
    if (found != stage.screens.end()) {
        ShowScreen(stage, *view, *found->second);
    }
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
    // The picture the machine can read is offered to the runtime here, at the place that knows it is
    // running on the platform, so that everything above this line reads a picture without knowing
    // which machine it is on.
    RegisterWicImageProvider();
    CompileResult compiled = CompileUiFile(xmlPath);
    if (!compiled.Ok()) {
        // A document may be read from several files, so the report names the file each diagnostic was
        // found in rather than the file the program was started with.
        PrintDiagnostics(compiled.sources, compiled.diagnostics, std::cerr);
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

    Stage stage;
    stage.document = compiled.root ? &*compiled.root : nullptr;
    CollectScreens(*compiled.root, stage.screens);
    stage.surfaces.reserve(launching.size());
    for (const IrNode* node : launching) {
        auto surface = std::make_unique<LiveSurface>();
        LiveSurface* view = surface.get();
        for (const auto& entry : handlers) {
            view->clicks.emplace(entry.first, [view, handler = entry.second]() { handler(view->context); });
        }
        view->screenId = ScreenId(*node);
        // The screen is built, and the document it belongs to is what its copies of components are
        // resolved against: a definition borrowed from another file lives in the document, not in
        // every screen that places a copy of it. The links the screen holds are answered here, where
        // the windows they can reach and the screens they can show are in hand.
        view->root = Instantiate(*node, view->clicks, MakeLinkHandler(stage, *view), stage.document);
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
        stage.surfaces.push_back(std::move(surface));
    }

    if (!options.selfTest) {
        LogInfo(std::to_string(stage.surfaces.size()) + " window(s) open. Press Esc to close one.");
    }

    bool running = true;
    bool selfTestClicked = false;
    int sizeWaits = 0;
    while (running) {
        stage.surfaces.front()->window.Pump(running);
        if (!running) {
            break;
        }
        // A link clicked a moment ago is answered here, once the click is over: the screen it asked
        // for takes the place of the one that was clicked, which cannot happen while the click is
        // still being handled.
        ApplyPendingLink(stage);
        bool anyOpen = false;
        bool paintWaiting = true;
        bool animating = false;
        for (const std::unique_ptr<LiveSurface>& surface : stage.surfaces) {
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
            if (options.selfTest && !selfTestClicked && surface.get() == stage.surfaces.front().get()) {
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
                const bool wasLink = frame->Kind() == NodeKind::LINK;
                const int answeredBefore = stage.answeredLinks;
                const Rect bounds = frame->Bounds();
                const Point center{bounds.x + bounds.width * 0.5f, bounds.y + bounds.height * 0.5f};
                surface->input.OnDown(*surface->root, center);
                surface->input.OnUp(*surface->root, center);
                // A link the self-test clicks has to be answered rather than merely drawn: a link
                // that is wired to nothing is exactly the failure a run like this is for, and it is
                // invisible to a test that only checks the window still opens.
                if (wasLink && stage.answeredLinks == answeredBefore) {
                    LogError("The self-test clicked a link and nothing answered it.");
                    return 1;
                }
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
            // A link the self-test clicked is answered before the run ends, so a document whose links
            // are part of what it does is exercised rather than merely built.
            ApplyPendingLink(stage);
            return 0;
        }
        if (options.selfTest) {
            continue;
        }
        if (animating) {
            // Keep advancing an in-flight scroll or motion at about 60 frames a second
            // even while no input arrives, instead of spinning or stalling.
            stage.surfaces.front()->window.WaitForMessage(16);
        } else if (paintWaiting) {
            stage.surfaces.front()->window.WaitForMessage();
        }
    }
    return 0;
}

}  // namespace Runtime
