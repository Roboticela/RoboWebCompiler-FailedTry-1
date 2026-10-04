#include "designer/shell/DesignerApp.h"

#include "compiler/semantic/ComponentCatalog.h"
#include "compiler/semantic/UiCompiler.h"
#include "compiler/types/ValueParser.h"
#include "designer/model/DesignDocument.h"
#include "engine/core/Log.h"
#include "engine/math/Color.h"
#include "platform/windows/Dx11Renderer.h"
#include "platform/windows/Win32Window.h"
#include "ui/element/Element.h"
#include "ui/factory/UiFactory.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>

namespace Runtime {
namespace {

constexpr float TOOLBAR_HEIGHT = 44.0f;
constexpr float LEFT_WIDTH = 240.0f;
constexpr float RIGHT_WIDTH = 280.0f;
constexpr float XML_HEIGHT = 220.0f;
constexpr float ROW_HEIGHT = 28.0f;
constexpr float SNAP_STEP = 4.0f;
constexpr Color COLOR_CHROME{0.953f, 0.957f, 0.965f, 1.0f};
constexpr Color COLOR_PANEL{1.0f, 1.0f, 1.0f, 1.0f};
constexpr Color COLOR_INK{0.067f, 0.067f, 0.067f, 1.0f};
constexpr Color COLOR_MUTED{0.42f, 0.447f, 0.502f, 1.0f};
constexpr Color COLOR_ACCENT{37.0f / 255.0f, 99.0f / 255.0f, 235.0f / 255.0f, 1.0f};
constexpr Color COLOR_SELECTED{0.859f, 0.918f, 0.996f, 1.0f};
constexpr Color COLOR_LINE{0.820f, 0.835f, 0.859f, 1.0f};
constexpr Color COLOR_CANVAS{0.898f, 0.906f, 0.922f, 1.0f};
constexpr Color COLOR_STATUS{0.725f, 0.110f, 0.110f, 1.0f};
constexpr Color COLOR_GRID{0.820f, 0.835f, 0.859f, 1.0f};

enum class EditTarget {
    NONE,
    PROPERTY,
    XML,
};

enum class DragKind {
    NONE,
    PAN,
    RESIZE,
    REORDER,
};

enum class ResizeHandle {
    NONE,
    EAST,
    SOUTH,
    SOUTH_EAST,
};

struct HitRow {
    Rect rect{};
    std::vector<int> path;
    std::string name;
    int depth = 0;
};

struct DevicePreset {
    const char* name;
    float width;
    float height;
};

constexpr DevicePreset DEVICE_PRESETS[] = {
    {"Desktop", 800.0f, 600.0f},
    {"Tablet", 768.0f, 1024.0f},
    {"Mobile", 390.0f, 844.0f},
};

bool SamePath(const std::vector<int>& left, const std::vector<int>& right) {
    return left == right;
}

std::string NodeLabel(const XmlNode& node) {
    std::string label = node.name;
    for (const XmlAttribute& attribute : node.attributes) {
        if (attribute.name == "id" || attribute.name == "text") {
            label += "  ";
            label += attribute.value;
            break;
        }
    }
    return label;
}

Element* ElementAt(Element* node, const std::vector<int>& path) {
    for (const int index : path) {
        if (node == nullptr || index < 0 || index >= static_cast<int>(node->Children().size())) {
            return nullptr;
        }
        node = node->Children()[static_cast<std::size_t>(index)].get();
    }
    return node;
}

bool FindElementPath(Element& node, const Element* target, std::vector<int>& path) {
    if (&node == target) {
        return true;
    }
    int index = 0;
    for (const std::unique_ptr<Element>& child : node.Children()) {
        path.push_back(index);
        if (FindElementPath(*child, target, path)) {
            return true;
        }
        path.pop_back();
        ++index;
    }
    return false;
}

bool FindComponentPath(const XmlNode& node, std::string_view component, std::vector<int>& path) {
    if (node.name == component) {
        return true;
    }
    int index = 0;
    for (const XmlNode& child : node.children) {
        path.push_back(index);
        if (FindComponentPath(child, component, path)) {
            return true;
        }
        path.pop_back();
        ++index;
    }
    return false;
}

void DrawLabel(Renderer& renderer, std::string_view text, const Rect& rect, Color color, float size, TextAlign align) {
    TextStyle style;
    style.fontSize = size;
    style.color = color;
    style.horizontal = align;
    style.vertical = TextAlign::CENTER;
    renderer.DrawString(text, rect, style);
}

void DrawBorder(Renderer& renderer, const Rect& rect, Color color) {
    renderer.FillRectangle(Rect{rect.x, rect.y, rect.width, 1.0f}, color, 0.0f);
    renderer.FillRectangle(Rect{rect.x, rect.y + rect.height - 1.0f, rect.width, 1.0f}, color, 0.0f);
    renderer.FillRectangle(Rect{rect.x, rect.y, 1.0f, rect.height}, color, 0.0f);
    renderer.FillRectangle(Rect{rect.x + rect.width - 1.0f, rect.y, 1.0f, rect.height}, color, 0.0f);
}

std::string Utf8FromWide(wchar_t character) {
    const wchar_t text[2] = {character, L'\0'};
    char buffer[8] = {};
    const int written = WideCharToMultiByte(CP_UTF8, 0, text, 1, buffer, static_cast<int>(sizeof(buffer)), nullptr, nullptr);
    if (written <= 0) {
        return {};
    }
    return std::string(buffer, buffer + written);
}

void CopyText(const std::string& text) {
    const std::wstring wide = ToWide(text);
    if (!OpenClipboard(nullptr)) {
        return;
    }
    EmptyClipboard();
    const std::size_t bytes = (wide.size() + 1) * sizeof(wchar_t);
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (memory != nullptr) {
        void* destination = GlobalLock(memory);
        if (destination != nullptr) {
            memcpy(destination, wide.c_str(), bytes);
            GlobalUnlock(memory);
            SetClipboardData(CF_UNICODETEXT, memory);
        }
    }
    CloseClipboard();
}

std::string PasteText() {
    if (!OpenClipboard(nullptr)) {
        return {};
    }
    std::string result;
    HANDLE memory = GetClipboardData(CF_UNICODETEXT);
    if (memory != nullptr) {
        const wchar_t* wide = static_cast<const wchar_t*>(GlobalLock(memory));
        if (wide != nullptr) {
            const int bytes = WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
            if (bytes > 1) {
                result.resize(static_cast<std::size_t>(bytes - 1));
                WideCharToMultiByte(CP_UTF8, 0, wide, -1, result.data(), bytes, nullptr, nullptr);
            }
            GlobalUnlock(memory);
        }
    }
    CloseClipboard();
    return result;
}

float Snap(float value) {
    return std::round(value / SNAP_STEP) * SNAP_STEP;
}

}  // namespace

int DesignerApp::Run(const DesignerOptions& options) {
    DesignDocument document;
    const std::filesystem::path file = options.file.empty() ? std::filesystem::path("examples/Hello/Hello.xml") : options.file;
    if (!document.LoadFile(file)) {
        LogError("RoboUIDesigner could not open the XML file.");
        for (const Diagnostic& diagnostic : document.ParseDiagnostics()) {
            LogError(diagnostic.message);
        }
        return 1;
    }

    float deviceWidth = static_cast<float>(DEFAULT_WINDOW_WIDTH);
    float deviceHeight = static_cast<float>(DEFAULT_WINDOW_HEIGHT);
    if (const std::optional<Length> width = ParseLength(document.Attribute({}, "width"))) {
        if (width->unit == LengthUnit::PIXELS && width->value >= 1.0f) {
            deviceWidth = width->value;
        }
    }
    if (const std::optional<Length> height = ParseLength(document.Attribute({}, "height"))) {
        if (height->unit == LengthUnit::PIXELS && height->value >= 1.0f) {
            deviceHeight = height->value;
        }
    }
    int deviceIndex = 0;

    std::vector<int> selection;
    EditTarget editTarget = EditTarget::NONE;
    std::string propertyName;
    std::string propertyDraft;
    std::string propertyOriginal;
    bool replaceProperty = false;
    std::size_t xmlCaret = 0;
    int xmlScroll = 0;
    int hierarchyScroll = 0;
    int libraryScroll = 0;
    int inspectorScroll = 0;
    float zoom = 1.0f;
    float panX = 0.0f;
    float panY = 0.0f;
    bool fitted = false;
    DragKind drag = DragKind::NONE;
    ResizeHandle resizeHandle = ResizeHandle::NONE;
    Point dragStart{};
    float reorderAnchor = 0.0f;
    float resizeLeft = 0.0f;
    float resizeTop = 0.0f;
    std::vector<std::string> completions;
    int completionIndex = 0;

    std::unique_ptr<Element> preview;
    std::vector<Diagnostic> compileDiagnostics;
    auto rebuildPreview = [&]() {
        const CompileResult compiled = CompileUi(SourceFile{document.FilePath(), document.Text()});
        compileDiagnostics = compiled.diagnostics;
        if (!compiled.Ok()) {
            return;
        }
        preview = Instantiate(*compiled.root, {});
    };
    rebuildPreview();

    Rect toolbar{};
    Rect library{};
    Rect hierarchy{};
    Rect canvas{};
    Rect inspector{};
    Rect xmlPanel{};
    std::vector<HitRow> libraryRows;
    std::vector<HitRow> hierarchyRows;
    std::vector<HitRow> propertyRows;
    std::vector<HitRow> deviceRows;

    auto artboardScreen = [&]() {
        return Rect{canvas.x + (canvas.width - deviceWidth * zoom) * 0.5f + panX,
                    canvas.y + (canvas.height - deviceHeight * zoom) * 0.5f + panY, deviceWidth * zoom, deviceHeight * zoom};
    };
    auto toArtboard = [&](Point screen) {
        const Rect board = artboardScreen();
        const float safeZoom = zoom > 0.05f ? zoom : 0.05f;
        return Point{(screen.x - board.x) / safeZoom, (screen.y - board.y) / safeZoom};
    };

    auto layoutChrome = [&](float width, float height) {
        toolbar = Rect{0.0f, 0.0f, width, TOOLBAR_HEIGHT};
        library = Rect{0.0f, TOOLBAR_HEIGHT, LEFT_WIDTH, 280.0f};
        hierarchy = Rect{0.0f, library.y + library.height, LEFT_WIDTH, std::max(0.0f, height - library.y - library.height - XML_HEIGHT)};
        xmlPanel = Rect{0.0f, height - XML_HEIGHT, width, XML_HEIGHT};
        inspector = Rect{width - RIGHT_WIDTH, TOOLBAR_HEIGHT, RIGHT_WIDTH, std::max(0.0f, height - TOOLBAR_HEIGHT - XML_HEIGHT)};
        canvas = Rect{LEFT_WIDTH, TOOLBAR_HEIGHT, std::max(0.0f, width - LEFT_WIDTH - RIGHT_WIDTH),
                      std::max(0.0f, height - TOOLBAR_HEIGHT - XML_HEIGHT)};
        libraryRows.clear();
        const char* libraryNames[] = {"frame", "shadow"};
        constexpr int libraryCount = static_cast<int>(sizeof(libraryNames) / sizeof(libraryNames[0]));
        for (int index = 0; index < libraryCount; ++index) {
            HitRow row;
            row.rect = Rect{library.x + 8.0f, library.y + 36.0f + static_cast<float>(index - libraryScroll) * ROW_HEIGHT, library.width - 28.0f,
                            ROW_HEIGHT - 4.0f};
            row.name = libraryNames[index];
            libraryRows.push_back(std::move(row));
        }
        hierarchyRows.clear();
        if (const XmlNode* root = document.Root()) {
            int rowIndex = 0;
            auto walk = [&](auto&& self, const XmlNode& node, const std::vector<int>& path, int depth) -> void {
                HitRow row;
                row.path = path;
                row.depth = depth;
                row.name = NodeLabel(node);
                row.rect = Rect{hierarchy.x + 8.0f, hierarchy.y + 36.0f + static_cast<float>(rowIndex - hierarchyScroll) * ROW_HEIGHT,
                                hierarchy.width - 28.0f, ROW_HEIGHT - 2.0f};
                ++rowIndex;
                hierarchyRows.push_back(std::move(row));
                int childIndex = 0;
                for (const XmlNode& child : node.children) {
                    std::vector<int> childPath = path;
                    childPath.push_back(childIndex);
                    self(self, child, childPath, depth + 1);
                    ++childIndex;
                }
            };
            walk(walk, *root, {}, 0);
        }
        propertyRows.clear();
        if (const XmlNode* selected = document.NodeAt(selection)) {
            const ComponentSpec* spec = FindComponent(selected->name);
            if (spec != nullptr) {
                int propertyCount = 0;
                for (const PropertySpec& property : spec->properties) {
                    if (std::string_view(property.name) != "value") {
                        ++propertyCount;
                    }
                }
                const int visibleRows = std::max(1, static_cast<int>((inspector.height - 72.0f) / 40.0f));
                inspectorScroll = std::max(0, std::min(inspectorScroll, std::max(0, propertyCount - visibleRows)));
                int rowIndex = 0;
                for (const PropertySpec& property : spec->properties) {
                    if (std::string_view(property.name) == "value") {
                        continue;
                    }
                    HitRow row;
                    row.name = property.name;
                    row.rect = Rect{inspector.x + 12.0f, inspector.y + 64.0f + static_cast<float>(rowIndex - inspectorScroll) * 40.0f,
                                    inspector.width - 28.0f, 32.0f};
                    propertyRows.push_back(std::move(row));
                    ++rowIndex;
                }
            }
        }
        deviceRows.clear();
        float deviceX = 280.0f;
        for (const DevicePreset& preset : DEVICE_PRESETS) {
            HitRow row;
            row.name = preset.name;
            row.rect = Rect{deviceX, 8.0f, 88.0f, 28.0f};
            deviceRows.push_back(std::move(row));
            deviceX += 96.0f;
        }
        if (!fitted && canvas.width > 64.0f && canvas.height > 64.0f) {
            const float fitX = (canvas.width - 64.0f) / deviceWidth;
            const float fitY = (canvas.height - 64.0f) / deviceHeight;
            zoom = std::min(fitX, fitY);
            if (zoom > 1.0f) {
                zoom = 1.0f;
            }
            panX = 0.0f;
            panY = 0.0f;
            fitted = true;
        }
    };

    auto selectedScreen = [&]() {
        Rect result{};
        if (preview == nullptr) {
            return result;
        }
        Element* element = ElementAt(preview.get(), selection);
        if (element == nullptr) {
            return result;
        }
        const Rect bounds = element->Bounds();
        const Rect board = artboardScreen();
        result = Rect{board.x + bounds.x * zoom, board.y + bounds.y * zoom, bounds.width * zoom, bounds.height * zoom};
        return result;
    };

    auto refreshCompletions = [&]() {
        completions.clear();
        completionIndex = 0;
        const std::string& source = document.Text();
        if (xmlCaret == 0 || xmlCaret > source.size()) {
            return;
        }
        const std::size_t start = source.rfind('<', xmlCaret - 1);
        if (start == std::string::npos) {
            return;
        }
        const std::string prefix = source.substr(start + 1, xmlCaret - start - 1);
        if (prefix.find_first_of(" \t\r\n>/") != std::string::npos) {
            return;
        }
        for (const ComponentSpec& spec : ComponentCatalog()) {
            const std::string_view name = spec.name;
            if (name.rfind(prefix, 0) == 0) {
                completions.emplace_back(name);
            }
        }
    };

    auto commitProperty = [&]() {
        if (propertyName.empty()) {
            return;
        }
        if (propertyDraft.empty()) {
            document.ClearAttribute(selection, propertyName);
        } else {
            document.SetAttribute(selection, propertyName, propertyDraft);
        }
        rebuildPreview();
    };

    Win32Window window;
    const std::wstring title = L"RoboUIDesigner";
    if (!window.Create(title, 1440, 900)) {
        LogError("Failed to create the RoboUIDesigner window.");
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
    bool paintDirty = true;
    layoutChrome(clientWidth, clientHeight);

    window.SetResize([&](int, int) {
        renderer.Resize(window.PixelWidth(), window.PixelHeight());
        renderer.SetDpi(window.Dpi());
        clientWidth = window.ClientWidthDip();
        clientHeight = window.ClientHeightDip();
        layoutChrome(clientWidth, clientHeight);
        paintDirty = true;
    });

    auto parentForInsert = [&]() {
        std::vector<int> parent = selection;
        const XmlNode* node = document.NodeAt(parent);
        const ComponentSpec* spec = node == nullptr ? nullptr : FindComponent(node->name);
        if (spec != nullptr && spec->acceptsChildren) {
            return parent;
        }
        if (!parent.empty()) {
            parent.pop_back();
        }
        return parent;
    };

    window.SetMouseDown([&](Point point) {
        layoutChrome(clientWidth, clientHeight);
        editTarget = EditTarget::NONE;
        drag = DragKind::NONE;
        for (std::size_t index = 0; index < deviceRows.size(); ++index) {
            if (ContainsPoint(deviceRows[index].rect, point)) {
                deviceIndex = static_cast<int>(index);
                deviceWidth = DEVICE_PRESETS[index].width;
                deviceHeight = DEVICE_PRESETS[index].height;
                fitted = false;
                layoutChrome(clientWidth, clientHeight);
                paintDirty = true;
                return;
            }
        }
        for (const HitRow& row : libraryRows) {
            if (ContainsPoint(row.rect, point) && ContainsPoint(library, point)) {
                std::vector<int> inserted;
                if (document.InsertChild(parentForInsert(), row.name, inserted)) {
                    selection = inserted;
                    rebuildPreview();
                }
                paintDirty = true;
                return;
            }
        }
        for (const HitRow& row : hierarchyRows) {
            if (ContainsPoint(row.rect, point) && ContainsPoint(hierarchy, point)) {
                selection = row.path;
                paintDirty = true;
                return;
            }
        }
        const Rect propertyView{inspector.x, inspector.y + 56.0f, inspector.width, std::max(0.0f, inspector.height - 56.0f)};
        for (const HitRow& row : propertyRows) {
            if (ContainsPoint(row.rect, point) && ContainsPoint(propertyView, point)) {
                editTarget = EditTarget::PROPERTY;
                propertyName = row.name;
                propertyDraft = document.Attribute(selection, propertyName);
                propertyOriginal = propertyDraft;
                replaceProperty = true;
                paintDirty = true;
                return;
            }
        }
        if (ContainsPoint(xmlPanel, point)) {
            editTarget = EditTarget::XML;
            const float localY = point.y - xmlPanel.y - 36.0f;
            const int line = std::max(0, static_cast<int>(localY / 18.0f) + xmlScroll);
            int currentLine = 0;
            std::size_t lineStart = 0;
            const std::string& source = document.Text();
            for (std::size_t index = 0; index <= source.size(); ++index) {
                if (index == source.size() || source[index] == '\n') {
                    if (currentLine == line) {
                        const std::string_view lineText(source.data() + lineStart, index - lineStart);
                        const float localX = std::max(0.0f, point.x - xmlPanel.x - 12.0f);
                        std::size_t column = 0;
                        for (; column < lineText.size(); ++column) {
                            const Size measured = renderer.MeasureText(lineText.substr(0, column + 1), 13.0f, 100000.0f);
                            if (measured.width > localX) {
                                break;
                            }
                        }
                        xmlCaret = lineStart + column;
                        break;
                    }
                    ++currentLine;
                    lineStart = index + 1;
                }
            }
            refreshCompletions();
            paintDirty = true;
            return;
        }
        const Rect selectedRect = selectedScreen();
        if (selectedRect.width > 1.0f) {
            const float handle = 8.0f;
            const Rect east{selectedRect.x + selectedRect.width - handle, selectedRect.y + selectedRect.height * 0.5f - handle, handle * 2.0f, handle * 2.0f};
            const Rect south{selectedRect.x + selectedRect.width * 0.5f - handle, selectedRect.y + selectedRect.height - handle, handle * 2.0f, handle * 2.0f};
            const Rect corner{selectedRect.x + selectedRect.width - handle, selectedRect.y + selectedRect.height - handle, handle * 2.0f, handle * 2.0f};
            if (ContainsPoint(corner, point)) {
                resizeHandle = ResizeHandle::SOUTH_EAST;
            } else if (ContainsPoint(east, point)) {
                resizeHandle = ResizeHandle::EAST;
            } else if (ContainsPoint(south, point)) {
                resizeHandle = ResizeHandle::SOUTH;
            } else {
                resizeHandle = ResizeHandle::NONE;
            }
            if (resizeHandle != ResizeHandle::NONE && preview != nullptr) {
                Element* element = ElementAt(preview.get(), selection);
                if (element != nullptr) {
                    drag = DragKind::RESIZE;
                    resizeLeft = element->Bounds().x;
                    resizeTop = element->Bounds().y;
                    paintDirty = true;
                    return;
                }
            }
        }
        if (ContainsPoint(artboardScreen(), point) && preview != nullptr) {
            const Point local = toArtboard(point);
            Element* hit = preview->HitTest(local);
            std::vector<int> path;
            if (hit != nullptr && FindElementPath(*preview, hit, path)) {
                selection = path;
                drag = DragKind::REORDER;
                dragStart = point;
                reorderAnchor = local.y;
            }
            paintDirty = true;
            return;
        }
        if (ContainsPoint(canvas, point)) {
            drag = DragKind::PAN;
            dragStart = point;
        }
        paintDirty = true;
    });

    window.SetMouseMove([&](Point point) {
        if (drag == DragKind::PAN) {
            panX += point.x - dragStart.x;
            panY += point.y - dragStart.y;
            dragStart = point;
            paintDirty = true;
            return;
        }
        if (drag == DragKind::RESIZE) {
            const Point local = toArtboard(point);
            if (resizeHandle == ResizeHandle::EAST || resizeHandle == ResizeHandle::SOUTH_EAST) {
                const float width = std::max(16.0f, Snap(local.x - resizeLeft));
                document.SetAttribute(selection, "width", std::to_string(static_cast<int>(width)) + "px");
            }
            if (resizeHandle == ResizeHandle::SOUTH || resizeHandle == ResizeHandle::SOUTH_EAST) {
                const float height = std::max(16.0f, Snap(local.y - resizeTop));
                document.SetAttribute(selection, "height", std::to_string(static_cast<int>(height)) + "px");
            }
            rebuildPreview();
            paintDirty = true;
            return;
        }
        if (drag == DragKind::REORDER && !selection.empty()) {
            const Point local = toArtboard(point);
            if (local.y - reorderAnchor > 28.0f) {
                if (document.MoveNode(selection, 1)) {
                    selection.back() += 1;
                    reorderAnchor = local.y;
                    rebuildPreview();
                }
            } else if (reorderAnchor - local.y > 28.0f) {
                if (document.MoveNode(selection, -1)) {
                    selection.back() -= 1;
                    reorderAnchor = local.y;
                    rebuildPreview();
                }
            }
            paintDirty = true;
        }
    });
    window.SetMouseUp([&](Point) {
        drag = DragKind::NONE;
        resizeHandle = ResizeHandle::NONE;
        paintDirty = true;
    });
    window.SetMiddleDown([&](Point point) {
        drag = DragKind::PAN;
        dragStart = point;
    });
    window.SetMiddleUp([&](Point) { drag = DragKind::NONE; });
    window.SetMouseWheel([&](Point point, int delta) {
        layoutChrome(clientWidth, clientHeight);
        if (ContainsPoint(xmlPanel, point)) {
            xmlScroll = std::max(0, xmlScroll - (delta > 0 ? 1 : -1) * 3);
            paintDirty = true;
            return;
        }
        if (ContainsPoint(library, point)) {
            const int libraryCount = static_cast<int>(libraryRows.size());
            const int visibleRows = std::max(1, static_cast<int>((library.height - 36.0f) / ROW_HEIGHT));
            libraryScroll = std::max(0, std::min(std::max(0, libraryCount - visibleRows), libraryScroll - (delta > 0 ? 1 : -1)));
            paintDirty = true;
            return;
        }
        if (ContainsPoint(inspector, point)) {
            const int visibleRows = std::max(1, static_cast<int>((inspector.height - 72.0f) / 40.0f));
            inspectorScroll = std::max(0, std::min(std::max(0, static_cast<int>(propertyRows.size()) - visibleRows), inspectorScroll - (delta > 0 ? 1 : -1)));
            paintDirty = true;
            return;
        }
        if (ContainsPoint(hierarchy, point)) {
            hierarchyScroll = std::max(0, hierarchyScroll - (delta > 0 ? 1 : -1));
            paintDirty = true;
            return;
        }
        if (!ContainsPoint(canvas, point)) {
            return;
        }
        const float factor = delta > 0 ? 1.1f : 0.9f;
        const Rect before = artboardScreen();
        const float localX = (point.x - before.x) / zoom;
        const float localY = (point.y - before.y) / zoom;
        zoom = std::clamp(zoom * factor, 0.2f, 4.0f);
        const Rect after = artboardScreen();
        panX += point.x - (after.x + localX * zoom);
        panY += point.y - (after.y + localY * zoom);
        paintDirty = true;
    });

    window.SetKeyDown([&](unsigned key) {
        const bool control = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        if (control && key == 'S') {
            if (document.Save()) {
                LogInfo("Saved " + document.FilePath());
            }
            paintDirty = true;
            return true;
        }
        if (control && key == 'C') {
            CopyText(document.NodeXml(selection));
            return true;
        }
        if (control && key == 'V') {
            std::vector<int> inserted;
            if (document.PasteXml(parentForInsert(), PasteText(), inserted)) {
                selection = inserted;
                rebuildPreview();
                paintDirty = true;
            }
            return true;
        }
        if (control && key == 'D') {
            std::vector<int> duplicateParent = selection;
            if (!duplicateParent.empty()) {
                duplicateParent.pop_back();
            }
            std::vector<int> inserted;
            if (document.PasteXml(duplicateParent, document.NodeXml(selection), inserted)) {
                selection = inserted;
                rebuildPreview();
                paintDirty = true;
            }
            return true;
        }
        if (key == VK_ESCAPE) {
            if (editTarget == EditTarget::PROPERTY) {
                propertyDraft = propertyOriginal;
                commitProperty();
            }
            editTarget = EditTarget::NONE;
            completions.clear();
            paintDirty = true;
            return true;
        }
        if (editTarget == EditTarget::PROPERTY) {
            if (key == VK_BACK && !propertyDraft.empty()) {
                propertyDraft.pop_back();
                replaceProperty = false;
                commitProperty();
                paintDirty = true;
            } else if (key == VK_RETURN) {
                editTarget = EditTarget::NONE;
                paintDirty = true;
            }
            return true;
        }
        if (editTarget == EditTarget::XML) {
            std::string next = document.Text();
            if (xmlCaret > next.size()) {
                xmlCaret = next.size();
            }
            if (key == VK_BACK && xmlCaret > 0) {
                std::size_t index = xmlCaret;
                do {
                    --index;
                } while (index > 0 && (static_cast<unsigned char>(next[index]) & 0xC0) == 0x80);
                next.erase(index, xmlCaret - index);
                xmlCaret = index;
                document.ReplaceText(std::move(next));
                rebuildPreview();
            } else if (key == VK_DELETE && xmlCaret < next.size()) {
                std::size_t index = xmlCaret + 1;
                while (index < next.size() && (static_cast<unsigned char>(next[index]) & 0xC0) == 0x80) {
                    ++index;
                }
                next.erase(xmlCaret, index - xmlCaret);
                document.ReplaceText(std::move(next));
                rebuildPreview();
            } else if (key == VK_RETURN) {
                if (!completions.empty()) {
                    const std::string& source = document.Text();
                    const std::size_t start = source.rfind('<', xmlCaret == 0 ? 0 : xmlCaret - 1);
                    if (start != std::string::npos) {
                        next.replace(start + 1, xmlCaret - start - 1, completions[static_cast<std::size_t>(completionIndex)]);
                        xmlCaret = start + 1 + completions[static_cast<std::size_t>(completionIndex)].size();
                        document.ReplaceText(std::move(next));
                        completions.clear();
                        rebuildPreview();
                    }
                } else {
                    next.insert(xmlCaret, "\n");
                    ++xmlCaret;
                    document.ReplaceText(std::move(next));
                    rebuildPreview();
                }
            } else if (key == VK_LEFT && xmlCaret > 0) {
                --xmlCaret;
            } else if (key == VK_RIGHT && xmlCaret < next.size()) {
                ++xmlCaret;
            }
            refreshCompletions();
            paintDirty = true;
            return true;
        }
        if (key == VK_DELETE) {
            std::vector<int> parent = selection;
            if (document.DeleteNode(selection)) {
                parent.pop_back();
                selection = parent;
                rebuildPreview();
                paintDirty = true;
            }
            return true;
        }
        return false;
    });

    window.SetChar([&](wchar_t character) {
        const std::string utf8 = Utf8FromWide(character);
        if (utf8.empty() || (GetKeyState(VK_CONTROL) & 0x8000) != 0) {
            return;
        }
        if (editTarget == EditTarget::PROPERTY) {
            if (replaceProperty) {
                propertyDraft.clear();
                replaceProperty = false;
            }
            propertyDraft += utf8;
            commitProperty();
            paintDirty = true;
            return;
        }
        if (editTarget == EditTarget::XML) {
            std::string next = document.Text();
            if (xmlCaret > next.size()) {
                xmlCaret = next.size();
            }
            next.insert(xmlCaret, utf8);
            xmlCaret += utf8.size();
            document.ReplaceText(std::move(next));
            refreshCompletions();
            rebuildPreview();
            paintDirty = true;
        }
    });

    auto paint = [&]() {
        layoutChrome(clientWidth, clientHeight);
        if (preview != nullptr) {
            preview->Measure(renderer, deviceWidth, deviceHeight, true);
            preview->Arrange(Rect{0.0f, 0.0f, deviceWidth, deviceHeight});
        }
        renderer.BeginFrame(COLOR_CHROME);
        renderer.FillRectangle(toolbar, COLOR_PANEL, 0.0f);
        renderer.FillRectangle(Rect{0.0f, TOOLBAR_HEIGHT - 1.0f, clientWidth, 1.0f}, COLOR_LINE, 0.0f);
        DrawLabel(renderer, "RoboUIDesigner", Rect{16.0f, 0.0f, 220.0f, TOOLBAR_HEIGHT}, COLOR_INK, 16.0f, TextAlign::START);
        for (std::size_t index = 0; index < deviceRows.size(); ++index) {
            const bool active = static_cast<int>(index) == deviceIndex;
            renderer.FillRectangle(deviceRows[index].rect, active ? COLOR_ACCENT : COLOR_CHROME, 6.0f);
            DrawLabel(renderer, deviceRows[index].name, deviceRows[index].rect, active ? COLOR_PANEL : COLOR_INK, 13.0f, TextAlign::CENTER);
        }
        DrawLabel(renderer, (std::to_string(static_cast<int>(zoom * 100.0f)) + "%").c_str(), Rect{clientWidth - 80.0f, 0.0f, 64.0f, TOOLBAR_HEIGHT},
                  COLOR_MUTED, 13.0f, TextAlign::CENTER);

        renderer.FillRectangle(library, COLOR_PANEL, 0.0f);
        renderer.FillRectangle(hierarchy, COLOR_PANEL, 0.0f);
        renderer.FillRectangle(inspector, COLOR_PANEL, 0.0f);
        renderer.FillRectangle(Rect{LEFT_WIDTH - 1.0f, TOOLBAR_HEIGHT, 1.0f, clientHeight - TOOLBAR_HEIGHT - XML_HEIGHT}, COLOR_LINE, 0.0f);
        renderer.FillRectangle(Rect{clientWidth - RIGHT_WIDTH, TOOLBAR_HEIGHT, 1.0f, clientHeight - TOOLBAR_HEIGHT - XML_HEIGHT}, COLOR_LINE, 0.0f);
        DrawLabel(renderer, "Library", Rect{library.x + 12.0f, library.y + 4.0f, library.width - 24.0f, 28.0f}, COLOR_MUTED, 12.0f, TextAlign::START);
        auto paintListBar = [&](const Rect& viewport, int itemCount, int scrollRows) {
            const float contentHeight = 36.0f + static_cast<float>(itemCount) * ROW_HEIGHT;
            const float maxScroll = contentHeight - viewport.height;
            if (maxScroll <= 1.0f || viewport.height <= 16.0f) {
                return;
            }
            const Rect track{viewport.x + viewport.width - 14.0f, viewport.y + 8.0f, 8.0f, viewport.height - 16.0f};
            const float thumbHeight = std::min(track.height, std::max(28.0f, track.height * (viewport.height / contentHeight)));
            const float travel = std::max(0.0f, track.height - thumbHeight);
            const float offset = static_cast<float>(scrollRows) * ROW_HEIGHT;
            const float y = track.y + travel * std::min(1.0f, offset / maxScroll);
            renderer.FillRectangle(track, COLOR_LINE, 4.0f);
            renderer.FillRectangle(Rect{track.x, y, track.width, thumbHeight}, COLOR_MUTED, 4.0f);
        };
        renderer.PushClip(library);
        for (const HitRow& row : libraryRows) {
            if (row.rect.y + row.rect.height < library.y + 32.0f || row.rect.y > library.y + library.height) {
                continue;
            }
            renderer.FillRectangle(row.rect, COLOR_CHROME, 6.0f);
            DrawLabel(renderer, row.name, row.rect, COLOR_INK, 13.0f, TextAlign::CENTER);
        }
        renderer.PopClip();
        paintListBar(library, static_cast<int>(libraryRows.size()), libraryScroll);
        DrawLabel(renderer, "Layers", Rect{hierarchy.x + 12.0f, hierarchy.y + 4.0f, hierarchy.width - 24.0f, 28.0f}, COLOR_MUTED, 12.0f, TextAlign::START);
        renderer.PushClip(hierarchy);
        for (const HitRow& row : hierarchyRows) {
            if (row.rect.y + row.rect.height < hierarchy.y + 32.0f || row.rect.y > hierarchy.y + hierarchy.height) {
                continue;
            }
            if (SamePath(row.path, selection)) {
                renderer.FillRectangle(row.rect, COLOR_SELECTED, 4.0f);
            }
            Rect label = row.rect;
            label.x += static_cast<float>(row.depth) * 14.0f + 8.0f;
            label.width -= static_cast<float>(row.depth) * 14.0f + 8.0f;
            DrawLabel(renderer, row.name, label, COLOR_INK, 13.0f, TextAlign::START);
        }
        renderer.PopClip();
        paintListBar(hierarchy, static_cast<int>(hierarchyRows.size()), hierarchyScroll);

        renderer.FillRectangle(canvas, COLOR_CANVAS, 0.0f);
        renderer.PushClip(canvas);
        const Rect board = artboardScreen();
        renderer.SetTransform(board.x, board.y, zoom);
        renderer.PushClip(Rect{0.0f, 0.0f, deviceWidth, deviceHeight});
        renderer.FillRectangle(Rect{0.0f, 0.0f, deviceWidth, deviceHeight}, COLOR_PANEL, 0.0f);
        const float gridStep = 16.0f;
        for (float x = gridStep; x < deviceWidth; x += gridStep) {
            renderer.FillRectangle(Rect{x, 0.0f, 1.0f / zoom, deviceHeight}, COLOR_GRID, 0.0f);
        }
        for (float y = gridStep; y < deviceHeight; y += gridStep) {
            renderer.FillRectangle(Rect{0.0f, y, deviceWidth, 1.0f / zoom}, COLOR_GRID, 0.0f);
        }
        if (preview != nullptr) {
            preview->Paint(renderer);
        }
        renderer.PopClip();
        renderer.SetTransform(0.0f, 0.0f, 1.0f);
        DrawBorder(renderer, board, COLOR_LINE);
        const Rect selectedRect = selectedScreen();
        if (selectedRect.width > 1.0f && selectedRect.height > 1.0f) {
            DrawBorder(renderer, selectedRect, COLOR_ACCENT);
            renderer.FillRectangle(Rect{selectedRect.x + selectedRect.width - 4.0f, selectedRect.y + selectedRect.height * 0.5f - 4.0f, 8.0f, 8.0f}, COLOR_ACCENT, 0.0f);
            renderer.FillRectangle(Rect{selectedRect.x + selectedRect.width * 0.5f - 4.0f, selectedRect.y + selectedRect.height - 4.0f, 8.0f, 8.0f}, COLOR_ACCENT, 0.0f);
            renderer.FillRectangle(Rect{selectedRect.x + selectedRect.width - 4.0f, selectedRect.y + selectedRect.height - 4.0f, 8.0f, 8.0f}, COLOR_ACCENT, 0.0f);
        }
        renderer.PopClip();

        DrawLabel(renderer, "Inspector", Rect{inspector.x + 12.0f, inspector.y + 8.0f, inspector.width - 24.0f, 24.0f}, COLOR_MUTED, 12.0f, TextAlign::START);
        if (const XmlNode* selected = document.NodeAt(selection)) {
            DrawLabel(renderer, selected->name, Rect{inspector.x + 12.0f, inspector.y + 30.0f, inspector.width - 24.0f, 24.0f}, COLOR_INK, 16.0f, TextAlign::START);
        }
        const Rect propertyView{inspector.x, inspector.y + 56.0f, inspector.width, std::max(0.0f, inspector.height - 56.0f)};
        renderer.PushClip(propertyView);
        for (const HitRow& row : propertyRows) {
            if (row.rect.y + row.rect.height < propertyView.y || row.rect.y > propertyView.y + propertyView.height) {
                continue;
            }
            const bool active = editTarget == EditTarget::PROPERTY && propertyName == row.name;
            renderer.FillRectangle(row.rect, active ? COLOR_SELECTED : COLOR_CHROME, 4.0f);
            const std::string value = active ? propertyDraft : document.Attribute(selection, row.name);
            DrawLabel(renderer, row.name, Rect{row.rect.x + 8.0f, row.rect.y, row.rect.width * 0.42f, row.rect.height}, COLOR_MUTED, 12.0f, TextAlign::START);
            DrawLabel(renderer, value.empty() ? "-" : value, Rect{row.rect.x + row.rect.width * 0.42f, row.rect.y, row.rect.width * 0.55f, row.rect.height}, COLOR_INK, 13.0f, TextAlign::START);
        }
        renderer.PopClip();
        const int visibleRows = std::max(1, static_cast<int>((inspector.height - 72.0f) / 40.0f));
        const int propertyCount = static_cast<int>(propertyRows.size());
        if (propertyCount > visibleRows && inspector.height > 80.0f) {
            const float contentHeight = static_cast<float>(propertyCount) * 40.0f;
            const Rect track{inspector.x + inspector.width - 14.0f, propertyView.y + 4.0f, 8.0f, std::max(8.0f, propertyView.height - 8.0f)};
            const float thumbHeight = std::min(track.height, std::max(28.0f, track.height * (propertyView.height / contentHeight)));
            const float travel = std::max(0.0f, track.height - thumbHeight);
            const float maxScroll = static_cast<float>(std::max(1, propertyCount - visibleRows));
            const float y = track.y + travel * (static_cast<float>(inspectorScroll) / maxScroll);
            renderer.FillRectangle(track, COLOR_LINE, 4.0f);
            renderer.FillRectangle(Rect{track.x, y, track.width, thumbHeight}, COLOR_MUTED, 4.0f);
        }

        renderer.FillRectangle(xmlPanel, COLOR_PANEL, 0.0f);
        renderer.FillRectangle(Rect{0.0f, xmlPanel.y, clientWidth, 1.0f}, COLOR_LINE, 0.0f);
        DrawLabel(renderer, "XML", Rect{12.0f, xmlPanel.y + 6.0f, 80.0f, 24.0f}, COLOR_MUTED, 12.0f, TextAlign::START);
        std::string status = document.TextSynced() ? "Synced with the canvas" : "XML has a syntax error";
        if (!compileDiagnostics.empty()) {
            status = compileDiagnostics.front().code + "  " + compileDiagnostics.front().message;
        } else if (!document.ParseDiagnostics().empty()) {
            status = document.ParseDiagnostics().front().message;
        }
        DrawLabel(renderer, status, Rect{80.0f, xmlPanel.y + 6.0f, clientWidth - 96.0f, 24.0f}, compileDiagnostics.empty() && document.TextSynced() ? COLOR_MUTED : COLOR_STATUS, 12.0f, TextAlign::START);
        renderer.PushClip(Rect{0.0f, xmlPanel.y + 34.0f, clientWidth, xmlPanel.height - 34.0f});
        const std::string& source = document.Text();
        int lineNumber = 0;
        std::size_t lineStart = 0;
        float caretX = xmlPanel.x + 12.0f;
        float caretY = xmlPanel.y + 36.0f;
        for (std::size_t index = 0; index <= source.size(); ++index) {
            if (index != source.size() && source[index] != '\n') {
                continue;
            }
            if (lineNumber >= xmlScroll) {
                const float y = xmlPanel.y + 36.0f + static_cast<float>(lineNumber - xmlScroll) * 18.0f;
                if (y > xmlPanel.y + xmlPanel.height) {
                    break;
                }
                const std::string line = source.substr(lineStart, index - lineStart);
                Color lineColor = COLOR_INK;
                if (!line.empty() && line.find('<') != std::string::npos) {
                    lineColor = COLOR_ACCENT;
                }
                DrawLabel(renderer, line, Rect{xmlPanel.x + 12.0f, y, clientWidth - 24.0f, 18.0f}, lineColor, 13.0f, TextAlign::START);
                if (editTarget == EditTarget::XML && xmlCaret >= lineStart && xmlCaret <= index) {
                    const Size measured = renderer.MeasureText(std::string_view(source.data() + lineStart, xmlCaret - lineStart), 13.0f, 100000.0f);
                    caretX = xmlPanel.x + 12.0f + measured.width;
                    caretY = y;
                }
            }
            ++lineNumber;
            lineStart = index + 1;
        }
        if (editTarget == EditTarget::XML) {
            renderer.FillRectangle(Rect{caretX, caretY, 1.0f, 16.0f}, COLOR_ACCENT, 0.0f);
        }
        renderer.PopClip();
        if (!completions.empty()) {
            const Rect popup{xmlPanel.x + 12.0f, xmlPanel.y - std::min(120.0f, static_cast<float>(completions.size()) * 24.0f) - 8.0f, 180.0f,
                             static_cast<float>(completions.size()) * 24.0f};
            renderer.FillRectangle(popup, COLOR_PANEL, 4.0f);
            DrawBorder(renderer, popup, COLOR_LINE);
            for (std::size_t index = 0; index < completions.size(); ++index) {
                const Rect row{popup.x, popup.y + static_cast<float>(index) * 24.0f, popup.width, 24.0f};
                if (static_cast<int>(index) == completionIndex) {
                    renderer.FillRectangle(row, COLOR_SELECTED, 0.0f);
                }
                DrawLabel(renderer, completions[index], row, COLOR_INK, 13.0f, TextAlign::START);
            }
        }
        return renderer.EndFrame();
    };

    if (options.selfTest) {
        std::vector<int> buttonPath;
        if (document.Root() == nullptr || !FindComponentPath(*document.Root(), "frame", buttonPath)) {
            LogError("Self-test could not find a frame.");
            return 1;
        }
        if (!document.SetAttribute(buttonPath, "name", "Designed")) {
            LogError("Self-test could not edit the frame.");
            return 1;
        }
        selection = buttonPath;
        rebuildPreview();
        if (document.Text().find("Designed") == std::string::npos || preview == nullptr) {
            LogError("Self-test preview did not update from XML.");
            return 1;
        }
        clientWidth = window.ClientWidthDip();
        clientHeight = window.ClientHeightDip();
        layoutChrome(clientWidth, clientHeight);
        if (!paint()) {
            return 1;
        }
        LogInfo("RoboUIDesigner self-test ok");
        return 0;
    }

    LogInfo("RoboUIDesigner is open. Ctrl+S saves. Esc clears the editor focus.");
    bool running = true;
    while (running) {
        window.Pump(running);
        if (!running) {
            break;
        }
        if (paintDirty) {
            if (!paint()) {
                return 1;
            }
            paintDirty = false;
        } else {
            window.WaitForMessage();
        }
    }
    return 0;
}

}  // namespace Runtime
