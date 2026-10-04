#include "designer/shell/DesignerApp.h"

#include "compiler/semantic/ComponentCatalog.h"
#include "compiler/semantic/UiCompiler.h"
#include "compiler/types/ValueParser.h"
#include "designer/model/DesignDocument.h"
#include "designer/tools/EditorTool.h"
#include "engine/core/Log.h"
#include "engine/math/Color.h"
#include "platform/windows/Dx11Renderer.h"
#include "platform/windows/Win32Window.h"
#include "components/frame/Frame.h"
#include "ui/element/Element.h"
#include "ui/factory/UiFactory.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

namespace Runtime {
namespace {

constexpr float TOOLBAR_HEIGHT = 48.0f;
constexpr float ICON_SIDEBAR = 72.0f;
constexpr float LEFT_WIDTH = 318.0f;
constexpr float RIGHT_WIDTH = 318.0f;
constexpr float XML_HEIGHT = 168.0f;
constexpr float ROW_HEIGHT = 32.0f;
constexpr float SNAP_STEP = 4.0f;
constexpr float RULER_SIZE = 22.0f;
constexpr Color COLOR_CHROME{0x18 / 255.0f, 0x18 / 255.0f, 0x1A / 255.0f, 1.0f};
constexpr Color COLOR_PANEL{0x18 / 255.0f, 0x18 / 255.0f, 0x1A / 255.0f, 1.0f};
constexpr Color COLOR_FIELD{0x21 / 255.0f, 0x24 / 255.0f, 0x26 / 255.0f, 1.0f};
constexpr Color COLOR_INK{1.0f, 1.0f, 1.0f, 1.0f};
constexpr Color COLOR_MUTED{0x8F / 255.0f, 0x9D / 255.0f, 0xA3 / 255.0f, 1.0f};
constexpr Color COLOR_ACCENT{0x7E / 255.0f, 1.0f, 0xF5 / 255.0f, 1.0f};
constexpr Color COLOR_SELECT{0x00 / 255.0f, 0xD1 / 255.0f, 0xB8 / 255.0f, 1.0f};
constexpr Color COLOR_SELECT_WASH{0x00 / 255.0f, 0xD1 / 255.0f, 0xB8 / 255.0f, 0.28f};
constexpr Color COLOR_SELECTED{0x2E / 255.0f, 0x34 / 255.0f, 0x34 / 255.0f, 1.0f};
constexpr Color COLOR_LINE{0x2E / 255.0f, 0x34 / 255.0f, 0x34 / 255.0f, 1.0f};
constexpr Color COLOR_CANVAS{0xBF / 255.0f, 0xBF / 255.0f, 0xBF / 255.0f, 1.0f};
constexpr Color COLOR_BOARD{1.0f, 1.0f, 1.0f, 1.0f};
constexpr Color COLOR_STATUS{1.0f, 0x32 / 255.0f, 0x77 / 255.0f, 1.0f};
constexpr Color COLOR_GRID{0xE8 / 255.0f, 0xEA / 255.0f, 0xEE / 255.0f, 1.0f};
constexpr Color COLOR_RULER{0xE8 / 255.0f, 0xEA / 255.0f, 0xEE / 255.0f, 1.0f};
constexpr Color COLOR_TICK{0x49 / 255.0f, 0x5E / 255.0f, 0x74 / 255.0f, 1.0f};
constexpr Color COLOR_NAV{0x3B / 255.0f, 0x6F / 255.0f, 0xF0 / 255.0f, 1.0f};
constexpr Color COLOR_NAV_HOVER{0x4C / 255.0f, 0x80 / 255.0f, 0xF6 / 255.0f, 1.0f};
constexpr Color COLOR_HOVER{0x2A / 255.0f, 0x30 / 255.0f, 0x34 / 255.0f, 1.0f};

enum class EditTarget {
    NONE,
    PROPERTY,
    XML,
    FILTER,
};

enum class DragKind {
    NONE,
    PAN,
    RESIZE,
    MOVE,
    REORDER,
    GUIDE,
    MARQUEE,
};

enum class ResizeHandle {
    NONE,
    NORTH,
    SOUTH,
    EAST,
    WEST,
    NORTH_EAST,
    NORTH_WEST,
    SOUTH_EAST,
    SOUTH_WEST,
};

struct EditorGuide {
    bool vertical = false;
    float position = 0.0f;
};

struct EditorDragNode {
    std::vector<int> path;
    float localX = 0.0f;
    float localY = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    float boardX = 0.0f;
    float boardY = 0.0f;
};

bool RectsOverlap(const Rect& left, const Rect& right) {
    return left.x < right.x + right.width && left.x + left.width > right.x && left.y < right.y + right.height && left.y + left.height > right.y;
}

WindowCursor CursorForHandle(ResizeHandle handle) {
    switch (handle) {
    case ResizeHandle::NORTH:
    case ResizeHandle::SOUTH:
        return WindowCursor::SIZE_NS;
    case ResizeHandle::EAST:
    case ResizeHandle::WEST:
        return WindowCursor::SIZE_WE;
    case ResizeHandle::NORTH_WEST:
    case ResizeHandle::SOUTH_EAST:
        return WindowCursor::SIZE_NWSE;
    case ResizeHandle::NORTH_EAST:
    case ResizeHandle::SOUTH_WEST:
        return WindowCursor::SIZE_NESW;
    case ResizeHandle::NONE:
        return WindowCursor::ARROW;
    }
    return WindowCursor::ARROW;
}

struct FileEntry {
    std::filesystem::path path;
    std::string name;
    int depth = 0;
    bool directory = false;
    bool expanded = false;
    Rect rect{};
    Rect twist{};
};

struct HitRow {
    Rect rect{};
    Rect eye{};
    Rect lock{};
    Rect twist{};
    std::vector<int> path;
    std::string name;
    int depth = 0;
    bool hidden = false;
    bool locked = false;
    bool branch = false;
    bool expanded = true;
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

bool AttributeIsFalse(const XmlNode& node, std::string_view name) {
    for (const XmlAttribute& attribute : node.attributes) {
        if (attribute.name == name && attribute.value == "false") {
            return true;
        }
    }
    return false;
}

bool AttributeEquals(const XmlNode& node, std::string_view name, std::string_view value) {
    for (const XmlAttribute& attribute : node.attributes) {
        if (attribute.name == name && attribute.value == value) {
            return true;
        }
    }
    return false;
}

std::string LowerCopy(std::string_view text) {
    std::string lower(text);
    for (char& character : lower) {
        character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    }
    return lower;
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

Point ContentOrigin(Element* element) {
    if (element == nullptr) {
        return {};
    }
    if (Frame* frame = dynamic_cast<Frame*>(element)) {
        return Point{frame->ContentX(), frame->ContentY()};
    }
    return Point{element->Bounds().x, element->Bounds().y};
}

float ContentSpan(Element* element, bool vertical) {
    if (Frame* frame = dynamic_cast<Frame*>(element)) {
        return vertical ? frame->ContentWidth() : frame->ContentHeight();
    }
    if (element == nullptr) {
        return 0.0f;
    }
    return vertical ? element->Bounds().width : element->Bounds().height;
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

void DrawIconLine(Renderer& renderer, float x1, float y1, float x2, float y2, Color color) {
    const float dx = x2 - x1;
    const float dy = y2 - y1;
    const float steps = std::max(1.0f, std::max(std::fabs(dx), std::fabs(dy)));
    const int count = static_cast<int>(steps);
    for (int index = 0; index <= count; ++index) {
        const float t = static_cast<float>(index) / steps;
        renderer.FillRectangle(Rect{x1 + dx * t, y1 + dy * t, 1.5f, 1.5f}, color, 0.5f);
    }
}

void DrawNavIcon(Renderer& renderer, float x, float y, int kind, Color color) {
    if (kind == 0) {
        renderer.StrokeRectangle(Rect{x + 1.0f, y + 6.0f, 16.0f, 11.0f}, color, 1.25f, 2.0f);
        renderer.StrokeRectangle(Rect{x + 1.0f, y + 3.0f, 7.0f, 4.0f}, color, 1.25f, 1.0f);
    } else if (kind == 1) {
        renderer.StrokeRectangle(Rect{x + 4.0f, y + 1.0f, 13.0f, 15.0f}, color, 1.25f, 1.5f);
        renderer.StrokeRectangle(Rect{x + 1.0f, y + 4.0f, 13.0f, 15.0f}, color, 1.25f, 1.5f);
    } else if (kind == 2) {
        renderer.StrokeRectangle(Rect{x + 4.0f, y + 4.0f, 10.0f, 10.0f}, color, 1.25f, 1.0f);
        renderer.FillRectangle(Rect{x + 1.0f, y + 1.0f, 3.5f, 3.5f}, color, 1.0f);
        renderer.FillRectangle(Rect{x + 13.5f, y + 1.0f, 3.5f, 3.5f}, color, 1.0f);
        renderer.FillRectangle(Rect{x + 1.0f, y + 13.5f, 3.5f, 3.5f}, color, 1.0f);
        renderer.FillRectangle(Rect{x + 13.5f, y + 13.5f, 3.5f, 3.5f}, color, 1.0f);
    } else if (kind == 3) {
        renderer.StrokeRectangle(Rect{x + 1.0f, y + 3.0f, 16.0f, 13.0f}, color, 1.25f, 2.0f);
        renderer.FillRectangle(Rect{x + 4.0f, y + 6.0f, 3.5f, 3.5f}, color, 2.0f);
        DrawIconLine(renderer, x + 3.0f, y + 13.0f, x + 8.0f, y + 9.0f, color);
        DrawIconLine(renderer, x + 8.0f, y + 9.0f, x + 15.0f, y + 13.0f, color);
    } else {
        const float points[][2] = {{9.0f, 1.0f}, {16.0f, 5.0f}, {16.0f, 13.0f}, {9.0f, 17.0f}, {2.0f, 13.0f}, {2.0f, 5.0f}};
        for (int index = 0; index < 6; ++index) {
            const int next = (index + 1) % 6;
            DrawIconLine(renderer, x + points[index][0], y + points[index][1], x + points[next][0], y + points[next][1], color);
        }
    }
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

std::filesystem::path FindProjectRoot() {
    std::error_code error;
    std::filesystem::path cursor = std::filesystem::current_path(error);
    for (int step = 0; step < 6 && !cursor.empty(); ++step) {
        if (std::filesystem::exists(cursor / "CMakeLists.txt", error)) {
            return cursor;
        }
        cursor = cursor.parent_path();
    }
    return std::filesystem::current_path(error);
}

bool IsHiddenEntry(const std::filesystem::path& path) {
    const std::string name = path.filename().string();
    return name == ".git" || name == "build" || name == ".vs";
}

std::filesystem::path UniqueChild(const std::filesystem::path& directory, const std::string& stem, const std::string& extension) {
    std::error_code error;
    std::filesystem::path candidate = directory / (stem + extension);
    int index = 2;
    while (std::filesystem::exists(candidate, error)) {
        candidate = directory / (stem + " " + std::to_string(index) + extension);
        ++index;
    }
    return candidate;
}

void DrawExplorerIcon(Renderer& renderer, float x, float y, int kind, Color color) {
    if (kind == 0 || kind == 3) {
        renderer.StrokeRectangle(Rect{x + 1.0f, y + 6.0f, 14.0f, 9.0f}, color, 1.25f, 2.0f);
        renderer.StrokeRectangle(Rect{x + 1.0f, y + 3.0f, 6.0f, 4.0f}, color, 1.25f, 1.0f);
        if (kind == 3) {
            renderer.FillRectangle(Rect{x + 11.0f, y + 1.0f, 5.0f, 1.5f}, color, 0.0f);
            renderer.FillRectangle(Rect{x + 12.5f, y, 1.5f, 5.0f}, color, 0.0f);
        }
    } else if (kind == 1 || kind == 2) {
        renderer.StrokeRectangle(Rect{x + 3.0f, y + 1.0f, 11.0f, 14.0f}, color, 1.25f, 1.5f);
        renderer.FillRectangle(Rect{x + 5.0f, y + 5.0f, 7.0f, 1.0f}, color, 0.0f);
        renderer.FillRectangle(Rect{x + 5.0f, y + 8.0f, 7.0f, 1.0f}, color, 0.0f);
        if (kind == 2) {
            renderer.FillRectangle(Rect{x + 11.0f, y + 12.0f, 5.0f, 1.5f}, color, 0.0f);
            renderer.FillRectangle(Rect{x + 12.5f, y + 11.0f, 1.5f, 5.0f}, color, 0.0f);
        }
    } else {
        for (int index = 0; index < 10; ++index) {
            const float start = 0.8f + static_cast<float>(index) * 0.45f;
            const float end = start + 0.45f;
            DrawIconLine(renderer, x + 8.0f + std::cos(start) * 6.0f, y + 8.0f + std::sin(start) * 6.0f, x + 8.0f + std::cos(end) * 6.0f,
                         y + 8.0f + std::sin(end) * 6.0f, color);
        }
        DrawIconLine(renderer, x + 13.0f, y + 3.0f, x + 13.0f, y + 7.0f, color);
        DrawIconLine(renderer, x + 13.0f, y + 3.0f, x + 9.0f, y + 3.0f, color);
    }
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
    std::vector<std::vector<int>> selections;
    std::vector<int> hoverPath;
    std::vector<std::vector<int>> collapsedPaths;
    std::vector<std::vector<int>> lockedPaths;
    std::string layerFilter;
    Rect pageRow{};
    Rect boardButton{};
    Rect filterRow{};
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
    std::vector<int> pressPath;
    float resizeOriginX = 0.0f;
    float resizeOriginY = 0.0f;
    float resizeOriginWidth = 0.0f;
    float resizeOriginHeight = 0.0f;
    bool showGrid = true;
    bool showRulers = true;
    unsigned lastClickTick = 0;
    std::vector<int> lastClickPath;
    unsigned lastTitleTick = 0;
    int lastTitleIndex = -1;
    std::vector<EditorGuide> guides;
    bool guideDraft = false;
    EditorGuide draftGuide{};
    bool snapXActive = false;
    bool snapYActive = false;
    float snapX = 0.0f;
    float snapY = 0.0f;
    std::vector<EditorDragNode> dragNodes;
    Rect dragUnion{};
    bool resizeGroup = false;
    Point marqueeAnchor{};
    Point marqueePoint{};
    std::vector<std::string> undoStack;
    std::vector<std::string> redoStack;
    std::string historyBefore;
    bool historyOpen = false;
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
    std::vector<HitRow> toolRows;
    std::vector<HitRow> navRows;
    int leftNav = 0;
    int navHover = -1;
    const std::filesystem::path projectRoot = FindProjectRoot();
    std::vector<std::string> openFolders{projectRoot.generic_string()};
    std::vector<FileEntry> fileEntries;
    std::string selectedFile;
    int fileHover = -1;
    int fileActionHover = 0;
    int fileScroll = 0;
    bool fileMenuOpen = false;
    Point fileMenuPoint{};
    std::string fileMenuTarget;
    bool fileMenuDirectory = false;
    int fileMenuHover = -1;
    std::vector<HitRow> fileMenuRows;
    bool fileRenaming = false;
    std::string renameDraft;
    std::string renameOriginal;
    bool filesDirty = true;
    unsigned lastFileTick = 0;
    std::string lastFileKey;
    Rect fileNewRect{};
    Rect folderNewRect{};
    Rect refreshRect{};
    Rect iconSidebar{};
    Rect fullSidebar{};
    EditorTool activeTool = EditorTool::SELECT;

    auto artboardScreen = [&]() {
        return Rect{canvas.x + (canvas.width - deviceWidth * zoom) * 0.5f + panX,
                    canvas.y + (canvas.height - deviceHeight * zoom) * 0.5f + panY, deviceWidth * zoom, deviceHeight * zoom};
    };
    auto toArtboard = [&](Point screen) {
        const Rect board = artboardScreen();
        const float safeZoom = zoom > 0.05f ? zoom : 0.05f;
        return Point{(screen.x - board.x) / safeZoom, (screen.y - board.y) / safeZoom};
    };
    auto workspace = [&]() {
        const float inset = showRulers ? RULER_SIZE : 0.0f;
        return Rect{canvas.x + inset, canvas.y + inset, std::max(0.0f, canvas.width - inset), std::max(0.0f, canvas.height - inset)};
    };

    auto reloadFiles = [&]() {
        fileEntries.clear();
        std::error_code error;
        auto collect = [&](auto&& self, const std::filesystem::path& directory, int depth) -> void {
            std::vector<std::filesystem::directory_entry> children;
            for (std::filesystem::directory_iterator it(directory, error), end; it != end; it.increment(error)) {
                if (error) {
                    error.clear();
                    continue;
                }
                if (IsHiddenEntry(it->path())) {
                    continue;
                }
                children.push_back(*it);
            }
            std::sort(children.begin(), children.end(), [](const std::filesystem::directory_entry& left, const std::filesystem::directory_entry& right) {
                std::error_code sortError;
                const bool leftDirectory = left.is_directory(sortError);
                const bool rightDirectory = right.is_directory(sortError);
                if (leftDirectory != rightDirectory) {
                    return leftDirectory;
                }
                return LowerCopy(left.path().filename().string()) < LowerCopy(right.path().filename().string());
            });
            for (const std::filesystem::directory_entry& child : children) {
                std::error_code childError;
                FileEntry entry;
                entry.path = child.path();
                entry.name = child.path().filename().string();
                entry.depth = depth;
                entry.directory = child.is_directory(childError);
                entry.expanded = entry.directory && std::find(openFolders.begin(), openFolders.end(), entry.path.generic_string()) != openFolders.end();
                fileEntries.push_back(entry);
                if (entry.expanded) {
                    self(self, entry.path, depth + 1);
                }
            }
        };
        collect(collect, projectRoot, 0);
        filesDirty = false;
    };
    auto fileDestination = [&]() {
        std::error_code error;
        if (!selectedFile.empty()) {
            const std::filesystem::path selected(selectedFile);
            if (std::filesystem::is_directory(selected, error)) {
                return selected;
            }
            if (!selected.parent_path().empty()) {
                return selected.parent_path();
            }
        }
        return projectRoot;
    };
    auto createExplorerFile = [&]() {
        const std::filesystem::path destination = fileDestination();
        const std::filesystem::path created = UniqueChild(destination, "Untitled", ".xml");
        std::ofstream output(created);
        if (!output) {
            return;
        }
        output << "<window width=\"800\" height=\"600\" title=\"Untitled\">\n"
               << "    <frame name=\"Board 1\" position=\"absolute\" x=\"0\" y=\"0\" width=\"800\" height=\"600\" background=\"#FFFFFF\"/>\n"
               << "</window>\n";
        output.close();
        const std::string folder = destination.generic_string();
        if (std::find(openFolders.begin(), openFolders.end(), folder) == openFolders.end()) {
            openFolders.push_back(folder);
        }
        selectedFile = created.generic_string();
        filesDirty = true;
    };
    auto createExplorerFolder = [&]() {
        const std::filesystem::path destination = fileDestination();
        const std::filesystem::path created = UniqueChild(destination, "New folder", "");
        std::error_code error;
        std::filesystem::create_directory(created, error);
        if (error) {
            return;
        }
        const std::string folder = destination.generic_string();
        if (std::find(openFolders.begin(), openFolders.end(), folder) == openFolders.end()) {
            openFolders.push_back(folder);
        }
        openFolders.push_back(created.generic_string());
        selectedFile = created.generic_string();
        filesDirty = true;
    };
    auto closeFileMenu = [&]() {
        fileMenuOpen = false;
        fileMenuHover = -1;
        fileMenuRows.clear();
    };
    auto commitFileRename = [&]() {
        if (!fileRenaming || renameDraft.empty() || renameDraft.find_first_of("\\/:*?\"<>|") != std::string::npos) {
            fileRenaming = false;
            return;
        }
        const std::filesystem::path source(selectedFile);
        const std::filesystem::path destination = source.parent_path() / renameDraft;
        std::error_code error;
        if (destination != source) {
            if (std::filesystem::exists(destination, error)) {
                fileRenaming = false;
                return;
            }
            std::filesystem::rename(source, destination, error);
            if (!error) {
                const std::string from = source.generic_string();
                const std::string to = destination.generic_string();
                for (std::string& folder : openFolders) {
                    if (folder == from || (folder.size() > from.size() && folder.compare(0, from.size(), from) == 0 && folder[from.size()] == '/')) {
                        folder.replace(0, from.size(), to);
                    }
                }
                selectedFile = to;
                filesDirty = true;
            }
        }
        fileRenaming = false;
    };
    auto applyFileAction = [&](int action) {
        const std::filesystem::path target = fileMenuTarget.empty() ? projectRoot : std::filesystem::path(fileMenuTarget);
        closeFileMenu();
        if (action == 0) {
            const std::string extension = LowerCopy(target.extension().string());
            if (extension == ".xml" && document.LoadFile(target)) {
                selection.clear();
                selections.clear();
                rebuildPreview();
            }
        } else if (action == 1 || action == 2) {
            selectedFile = target.generic_string();
            if (action == 1) {
                createExplorerFile();
            } else {
                createExplorerFolder();
            }
        } else if (action == 3) {
            selectedFile = target.generic_string();
            renameOriginal = target.filename().string();
            renameDraft = renameOriginal;
            fileRenaming = true;
        } else if (action == 4) {
            if (target != projectRoot) {
                std::error_code error;
                const std::string key = target.generic_string();
                if (std::filesystem::is_directory(target, error)) {
                    std::filesystem::remove_all(target, error);
                } else {
                    std::filesystem::remove(target, error);
                }
                if (!error) {
                    openFolders.erase(std::remove_if(openFolders.begin(), openFolders.end(),
                                                     [&](const std::string& folder) {
                                                         return folder == key || (folder.size() > key.size() && folder.compare(0, key.size(), key) == 0 && folder[key.size()] == '/');
                                                     }),
                                      openFolders.end());
                    if (selectedFile == key || (selectedFile.size() > key.size() && selectedFile.compare(0, key.size(), key) == 0 && selectedFile[key.size()] == '/')) {
                        selectedFile.clear();
                    }
                    filesDirty = true;
                }
            }
        } else if (action == 5) {
            CopyText(target.string());
        } else if (action == 6) {
            filesDirty = true;
        }
    };

    auto layoutChrome = [&](float width, float height) {
        toolbar = Rect{0.0f, 0.0f, width, TOOLBAR_HEIGHT};
        const float sideHeight = std::max(0.0f, height - TOOLBAR_HEIGHT - XML_HEIGHT);
        iconSidebar = Rect{0.0f, TOOLBAR_HEIGHT, ICON_SIDEBAR, sideHeight};
        fullSidebar = Rect{ICON_SIDEBAR, TOOLBAR_HEIGHT, std::max(0.0f, LEFT_WIDTH - ICON_SIDEBAR), sideHeight};
        hierarchy = Rect{fullSidebar.x, fullSidebar.y, fullSidebar.width, 0.0f};
        library = Rect{fullSidebar.x, fullSidebar.y, fullSidebar.width, 0.0f};
        xmlPanel = Rect{0.0f, height - XML_HEIGHT, width, XML_HEIGHT};
        inspector = Rect{width - RIGHT_WIDTH, TOOLBAR_HEIGHT, RIGHT_WIDTH, std::max(0.0f, height - TOOLBAR_HEIGHT - XML_HEIGHT)};
        canvas = Rect{LEFT_WIDTH, TOOLBAR_HEIGHT, std::max(0.0f, width - LEFT_WIDTH - RIGHT_WIDTH),
                      std::max(0.0f, height - TOOLBAR_HEIGHT - XML_HEIGHT)};
        libraryRows.clear();
        const char* libraryNames[] = {"frame", "text", "image", "vector", "path", "line", "group", "scroll", "component", "instance", "slot", "shadow"};
        constexpr int libraryCount = static_cast<int>(sizeof(libraryNames) / sizeof(libraryNames[0]));
        for (int index = 0; index < libraryCount; ++index) {
            HitRow row;
            row.rect = Rect{library.x + 8.0f, library.y + 36.0f + static_cast<float>(index - libraryScroll) * ROW_HEIGHT, library.width - 28.0f,
                            ROW_HEIGHT - 4.0f};
            row.name = libraryNames[index];
            libraryRows.push_back(std::move(row));
        }
        boardButton = Rect{hierarchy.x + hierarchy.width - 96.0f, hierarchy.y + 2.0f, 84.0f, 22.0f};
        pageRow = Rect{hierarchy.x + 8.0f, hierarchy.y + 28.0f, std::max(0.0f, hierarchy.width - 16.0f), 26.0f};
        filterRow = Rect{hierarchy.x + 8.0f, hierarchy.y + 60.0f, std::max(0.0f, hierarchy.width - 16.0f), 26.0f};
        hierarchyRows.clear();
        if (const XmlNode* root = document.Root()) {
            const std::string query = LowerCopy(layerFilter);
            const float listTop = hierarchy.y + 96.0f;
            int rowIndex = 0;
            auto matches = [&](auto&& self, const XmlNode& node) -> bool {
                if (query.empty()) {
                    return true;
                }
                if (LowerCopy(NodeLabel(node)).find(query) != std::string::npos) {
                    return true;
                }
                for (const XmlNode& child : node.children) {
                    if (self(self, child)) {
                        return true;
                    }
                }
                return false;
            };
            auto walk = [&](auto&& self, const XmlNode& node, const std::vector<int>& path, int depth) -> void {
                if (!matches(matches, node)) {
                    return;
                }
                const bool collapsed = std::find(collapsedPaths.begin(), collapsedPaths.end(), path) != collapsedPaths.end();
                HitRow row;
                row.path = path;
                row.depth = depth;
                row.name = NodeLabel(node);
                row.branch = !node.children.empty();
                row.expanded = row.branch && (!collapsed || !query.empty());
                row.hidden = AttributeIsFalse(node, "visible") || AttributeEquals(node, "display", "none");
                row.locked = false;
                for (std::size_t count = 1; count <= path.size(); ++count) {
                    const std::vector<int> prefix(path.begin(), path.begin() + static_cast<std::ptrdiff_t>(count));
                    if (std::find(lockedPaths.begin(), lockedPaths.end(), prefix) != lockedPaths.end()) {
                        row.locked = true;
                        break;
                    }
                }
                row.rect = Rect{hierarchy.x + 8.0f, listTop + static_cast<float>(rowIndex - hierarchyScroll) * ROW_HEIGHT,
                                std::max(0.0f, hierarchy.width - 28.0f), ROW_HEIGHT - 2.0f};
                row.twist = Rect{row.rect.x + static_cast<float>(depth) * 14.0f, row.rect.y + 6.0f, 16.0f, 16.0f};
                row.eye = Rect{row.rect.x + row.rect.width - 40.0f, row.rect.y + 6.0f, 16.0f, 16.0f};
                row.lock = Rect{row.rect.x + row.rect.width - 18.0f, row.rect.y + 6.0f, 16.0f, 16.0f};
                ++rowIndex;
                const bool expanded = row.expanded;
                hierarchyRows.push_back(std::move(row));
                if (!expanded) {
                    return;
                }
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
        navRows.clear();
        const char* navNames[] = {"Explorer", "Pages", "Library", "Assets", "Variables"};
        float navY = TOOLBAR_HEIGHT + 12.0f;
        for (int index = 0; index < 5; ++index) {
            HitRow row;
            row.name = navNames[index];
            row.depth = index;
            row.rect = Rect{0.0f, navY, ICON_SIDEBAR, 70.0f};
            navRows.push_back(std::move(row));
            navY += 74.0f;
        }
        const float actionY = fullSidebar.y + 36.0f;
        refreshRect = Rect{fullSidebar.x + fullSidebar.width - 36.0f, actionY, 28.0f, 28.0f};
        folderNewRect = Rect{refreshRect.x - 32.0f, actionY, 28.0f, 28.0f};
        fileNewRect = Rect{folderNewRect.x - 32.0f, actionY, 28.0f, 28.0f};
        if (leftNav == 0 && filesDirty) {
            reloadFiles();
        }
        if (leftNav != 0) {
            for (FileEntry& entry : fileEntries) {
                entry.rect = {};
                entry.twist = {};
            }
        } else {
            const float listTop = fullSidebar.y + 76.0f;
            const float listHeight = std::max(0.0f, fullSidebar.height - 76.0f);
            const int visibleRows = std::max(1, static_cast<int>(listHeight / 28.0f));
            fileScroll = std::max(0, std::min(fileScroll, std::max(0, static_cast<int>(fileEntries.size()) - visibleRows)));
            for (int index = 0; index < static_cast<int>(fileEntries.size()); ++index) {
                FileEntry& entry = fileEntries[static_cast<std::size_t>(index)];
                const float y = listTop + static_cast<float>(index - fileScroll) * 28.0f;
                const float indent = 10.0f + static_cast<float>(entry.depth) * 14.0f;
                entry.rect = Rect{fullSidebar.x + 6.0f, y, std::max(0.0f, fullSidebar.width - 12.0f), 26.0f};
                entry.twist = Rect{fullSidebar.x + indent, y + 8.0f, 10.0f, 10.0f};
            }
        }
        toolRows.clear();
        const EditorTool tools[] = {EditorTool::SELECT, EditorTool::FRAME, EditorTool::TEXT, EditorTool::IMAGE, EditorTool::LINE, EditorTool::HAND, EditorTool::ZOOM};
        float toolX = 168.0f;
        for (const EditorTool tool : tools) {
            HitRow row;
            row.name = EditorToolShortcut(tool);
            row.depth = static_cast<int>(tool);
            row.rect = Rect{toolX, 8.0f, 32.0f, 32.0f};
            toolRows.push_back(std::move(row));
            toolX += 36.0f;
        }
        deviceRows.clear();
        float deviceX = 440.0f;
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
        fileMenuRows.clear();
        if (fileMenuOpen) {
            const char* labels[8] = {};
            int actions[8] = {};
            int count = 0;
            auto addItem = [&](const char* label, int action) {
                labels[count] = label;
                actions[count] = action;
                ++count;
            };
            if (!fileMenuTarget.empty() && !fileMenuDirectory) {
                const std::string extension = LowerCopy(std::filesystem::path(fileMenuTarget).extension().string());
                if (extension == ".xml") {
                    addItem("Open", 0);
                }
                addItem("Rename", 3);
                addItem("Delete", 4);
                addItem("Copy path", 5);
            } else if (fileMenuDirectory) {
                addItem("New file", 1);
                addItem("New folder", 2);
                if (std::filesystem::path(fileMenuTarget) != projectRoot) {
                    addItem("Rename", 3);
                    addItem("Delete", 4);
                }
                addItem("Copy path", 5);
                addItem("Refresh", 6);
            } else {
                addItem("New file", 1);
                addItem("New folder", 2);
                addItem("Refresh", 6);
            }
            const float menuWidth = 176.0f;
            const float menuHeight = 8.0f + static_cast<float>(count) * 30.0f;
            float menuX = fileMenuPoint.x;
            float menuY = fileMenuPoint.y;
            if (menuX + menuWidth > width - 8.0f) {
                menuX = std::max(8.0f, width - menuWidth - 8.0f);
            }
            if (menuY + menuHeight > height - 8.0f) {
                menuY = std::max(8.0f, height - menuHeight - 8.0f);
            }
            for (int index = 0; index < count; ++index) {
                HitRow row;
                row.name = labels[index];
                row.depth = actions[index];
                row.rect = Rect{menuX + 4.0f, menuY + 4.0f + static_cast<float>(index) * 30.0f, menuWidth - 8.0f, 28.0f};
                fileMenuRows.push_back(std::move(row));
            }
        }
    };

    auto screenOfPath = [&](const std::vector<int>& path) {
        Rect result{};
        if (preview == nullptr) {
            return result;
        }
        Element* element = ElementAt(preview.get(), path);
        if (element == nullptr) {
            return result;
        }
        const Rect bounds = element->Bounds();
        const Rect board = artboardScreen();
        return Rect{board.x + bounds.x * zoom, board.y + bounds.y * zoom, bounds.width * zoom, bounds.height * zoom};
    };
    auto selectedScreen = [&]() {
        Rect result{};
        bool any = false;
        auto include = [&](const std::vector<int>& path) {
            const Rect screen = screenOfPath(path);
            if (screen.width < 0.5f && screen.height < 0.5f) {
                return;
            }
            if (!any) {
                result = screen;
                any = true;
                return;
            }
            const float right = std::max(result.x + result.width, screen.x + screen.width);
            const float bottom = std::max(result.y + result.height, screen.y + screen.height);
            result.x = std::min(result.x, screen.x);
            result.y = std::min(result.y, screen.y);
            result.width = right - result.x;
            result.height = bottom - result.y;
        };
        if (selections.empty()) {
            include(selection);
        } else {
            for (const std::vector<int>& path : selections) {
                include(path);
            }
        }
        return result;
    };
    auto pathSelected = [&](const std::vector<int>& path) {
        return std::find(selections.begin(), selections.end(), path) != selections.end();
    };
    auto selectOnly = [&](const std::vector<int>& path) {
        selections.clear();
        if (!path.empty()) {
            selections.push_back(path);
        }
        selection = path;
    };
    auto toggleSelect = [&](const std::vector<int>& path) {
        const auto found = std::find(selections.begin(), selections.end(), path);
        if (found != selections.end()) {
            selections.erase(found);
            selection = selections.empty() ? std::vector<int>{} : selections.back();
            return;
        }
        selections.push_back(path);
        selection = path;
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

    auto zoomAt = [&](Point point, float factor) {
        const Rect before = artboardScreen();
        const float localX = (point.x - before.x) / zoom;
        const float localY = (point.y - before.y) / zoom;
        zoom = std::clamp(zoom * factor, 0.1f, 16.0f);
        const Rect after = artboardScreen();
        panX += point.x - (after.x + localX * zoom);
        panY += point.y - (after.y + localY * zoom);
    };

    auto pixels = [](float value) { return std::to_string(static_cast<int>(std::lround(value))); };
    auto parentOfSelection = [&]() -> Element* {
        if (preview == nullptr || selection.empty()) {
            return nullptr;
        }
        std::vector<int> parentPath = selection;
        parentPath.pop_back();
        return parentPath.empty() ? preview.get() : ElementAt(preview.get(), parentPath);
    };
    auto selectionLocal = [&](float& x, float& y, float& width, float& height) {
        Element* element = preview == nullptr ? nullptr : ElementAt(preview.get(), selection);
        Element* parent = parentOfSelection();
        if (element == nullptr || parent == nullptr) {
            return false;
        }
        const Point origin = ContentOrigin(parent);
        x = element->Bounds().x - origin.x;
        y = element->Bounds().y - origin.y;
        width = element->Bounds().width;
        height = element->Bounds().height;
        return true;
    };
    auto snapAxis = [&](float value, bool vertical) {
        Element* parent = parentOfSelection();
        const Point origin = ContentOrigin(parent);
        const float originAxis = vertical ? origin.x : origin.y;
        const float boardValue = originAxis + value;
        float bestBoard = originAxis + Snap(value);
        float bestDistance = std::fabs(bestBoard - boardValue);
        const float threshold = 6.0f / std::max(zoom, 0.1f);
        for (const EditorGuide& guide : guides) {
            if (guide.vertical != vertical) {
                continue;
            }
            const float distance = std::fabs(guide.position - boardValue);
            if (distance <= threshold && distance < bestDistance) {
                bestBoard = guide.position;
                bestDistance = distance;
            }
        }
        if (parent != nullptr && preview != nullptr) {
            Element* selectedElement = ElementAt(preview.get(), selection);
            for (const std::unique_ptr<Element>& child : parent->Children()) {
                if (child.get() == selectedElement) {
                    continue;
                }
                const float start = vertical ? child->Bounds().x - origin.x : child->Bounds().y - origin.y;
                const float span = vertical ? child->Bounds().width : child->Bounds().height;
                for (const float edge : {originAxis + start, originAxis + start + span * 0.5f, originAxis + start + span}) {
                    const float distance = std::fabs(edge - boardValue);
                    if (distance <= threshold && distance < bestDistance) {
                        bestBoard = edge;
                        bestDistance = distance;
                    }
                }
            }
        }
        if (parent != nullptr) {
            for (const float edge : {originAxis, originAxis + ContentSpan(parent, vertical)}) {
                const float distance = std::fabs(edge - boardValue);
                if (distance <= threshold && distance < bestDistance) {
                    bestBoard = edge;
                    bestDistance = distance;
                }
            }
        }
        return bestBoard - originAxis;
    };
    auto placeSelection = [&](float x, float y, float width, float height) {
        if (selection.empty()) {
            return;
        }
        document.SetAttribute(selection, "position", "absolute");
        document.SetAttribute(selection, "x", pixels(x));
        document.SetAttribute(selection, "y", pixels(y));
        document.SetAttribute(selection, "width", pixels(std::max(16.0f, width)));
        document.SetAttribute(selection, "height", pixels(std::max(16.0f, height)));
        rebuildPreview();
    };
    auto captureDragNodes = [&]() {
        dragNodes.clear();
        float minX = 1000000.0f;
        float minY = 1000000.0f;
        float maxX = -1000000.0f;
        float maxY = -1000000.0f;
        std::vector<std::vector<int>> fallback;
        const std::vector<std::vector<int>>* paths = &selections;
        if (selections.empty()) {
            fallback.push_back(selection);
            paths = &fallback;
        }
        for (const std::vector<int>& path : *paths) {
            if (path.empty() || preview == nullptr) {
                continue;
            }
            Element* element = ElementAt(preview.get(), path);
            if (element == nullptr) {
                continue;
            }
            std::vector<int> parentPath = path;
            parentPath.pop_back();
            Element* parent = ElementAt(preview.get(), parentPath);
            EditorDragNode node;
            node.path = path;
            node.boardX = element->Bounds().x;
            node.boardY = element->Bounds().y;
            node.width = element->Bounds().width;
            node.height = element->Bounds().height;
            const Point origin = ContentOrigin(parent);
            node.localX = node.boardX - origin.x;
            node.localY = node.boardY - origin.y;
            minX = std::min(minX, node.boardX);
            minY = std::min(minY, node.boardY);
            maxX = std::max(maxX, node.boardX + node.width);
            maxY = std::max(maxY, node.boardY + node.height);
            dragNodes.push_back(std::move(node));
        }
        dragUnion = Rect{minX, minY, std::max(0.0f, maxX - minX), std::max(0.0f, maxY - minY)};
    };

    auto remember = [&]() {
        if (!historyOpen) {
            historyBefore = document.Text();
            historyOpen = true;
        }
    };
    auto finishEdit = [&]() {
        if (!historyOpen) {
            return;
        }
        historyOpen = false;
        if (document.Text() != historyBefore) {
            undoStack.push_back(std::move(historyBefore));
            if (undoStack.size() > 80) {
                undoStack.erase(undoStack.begin());
            }
            redoStack.clear();
        }
        historyBefore.clear();
    };
    auto undo = [&]() {
        finishEdit();
        if (undoStack.empty()) {
            return;
        }
        redoStack.push_back(document.Text());
        std::string text = std::move(undoStack.back());
        undoStack.pop_back();
        document.ReplaceText(std::move(text));
        editTarget = EditTarget::NONE;
        rebuildPreview();
        paintDirty = true;
    };
    auto redo = [&]() {
        finishEdit();
        if (redoStack.empty()) {
            return;
        }
        undoStack.push_back(document.Text());
        std::string text = std::move(redoStack.back());
        redoStack.pop_back();
        document.ReplaceText(std::move(text));
        editTarget = EditTarget::NONE;
        rebuildPreview();
        paintDirty = true;
    };
    auto boardRect = [&](int index) {
        Rect box{0.0f, static_cast<float>(index) * (deviceHeight + 80.0f), deviceWidth, deviceHeight};
        if (preview != nullptr && index >= 0 && index < static_cast<int>(preview->Children().size())) {
            const Rect bounds = preview->Children()[static_cast<std::size_t>(index)]->Bounds();
            if (bounds.width > 1.0f && bounds.height > 1.0f) {
                box = bounds;
            }
        }
        return box;
    };
    auto rectsOverlap = [](const Rect& left, const Rect& right) {
        return left.x < right.x + right.width && left.x + left.width > right.x && left.y < right.y + right.height && left.y + left.height > right.y;
    };
    auto solidifyBoards = [&]() {
        if (preview == nullptr || document.Root() == nullptr) {
            return;
        }
        const int count = static_cast<int>(preview->Children().size());
        for (int index = 0; index < count; ++index) {
            const std::vector<int> path{index};
            const Rect box = boardRect(index);
            const std::string position = document.Attribute(path, "position");
            const std::string width = document.Attribute(path, "width");
            const std::string height = document.Attribute(path, "height");
            if (position != "absolute" && position != "fixed") {
                document.SetAttribute(path, "position", "absolute");
                document.SetAttribute(path, "x", pixels(box.x));
                document.SetAttribute(path, "y", pixels(box.y));
            }
            if (width.empty() || width == "fill" || width == "fit") {
                document.SetAttribute(path, "width", pixels(std::max(16.0f, box.width)));
            }
            if (height.empty() || height == "fill" || height == "fit") {
                document.SetAttribute(path, "height", pixels(std::max(16.0f, box.height)));
            }
        }
        rebuildPreview();
    };
    auto addBoard = [&]() {
        remember();
        solidifyBoards();
        const float boardWidth = std::max(16.0f, deviceWidth);
        const float boardHeight = std::max(16.0f, deviceHeight);
        float nextX = 0.0f;
        float nextY = 0.0f;
        const int existing = preview == nullptr ? 0 : static_cast<int>(preview->Children().size());
        for (int guard = 0; guard < 32; ++guard) {
            Rect candidate{nextX, nextY, boardWidth, boardHeight};
            bool blocked = false;
            for (int index = 0; index < existing; ++index) {
                const Rect occupied = boardRect(index);
                if (!rectsOverlap(candidate, occupied)) {
                    continue;
                }
                nextX = occupied.x + occupied.width + 80.0f;
                nextY = occupied.y;
                blocked = true;
                break;
            }
            if (!blocked) {
                break;
            }
        }
        std::vector<int> inserted;
        if (!document.InsertChild({}, "frame", inserted)) {
            historyOpen = false;
            return;
        }
        const int boardNumber = static_cast<int>(inserted.empty() ? 1 : inserted.back() + 1);
        document.SetAttribute(inserted, "name", "Board " + std::to_string(boardNumber));
        document.SetAttribute(inserted, "position", "absolute");
        document.SetAttribute(inserted, "x", pixels(nextX));
        document.SetAttribute(inserted, "y", pixels(nextY));
        document.SetAttribute(inserted, "width", pixels(boardWidth));
        document.SetAttribute(inserted, "height", pixels(boardHeight));
        document.SetAttribute(inserted, "background", "#FFFFFF");
        selectOnly(inserted);
        rebuildPreview();
        finishEdit();
        const float centerX = nextX + boardWidth * 0.5f;
        const float centerY = nextY + boardHeight * 0.5f;
        panX = (deviceWidth * 0.5f - centerX) * zoom;
        panY = (deviceHeight * 0.5f - centerY) * zoom;
        paintDirty = true;
    };
    auto resizeHandleAt = [&](Point point) {
        const Rect selectedRect = selectedScreen();
        if (selectedRect.width < 1.0f || activeTool != EditorTool::SELECT) {
            return ResizeHandle::NONE;
        }
        const float handle = 8.0f;
        const Rect corners[] = {
            {selectedRect.x - handle, selectedRect.y - handle, handle * 2.0f, handle * 2.0f},
            {selectedRect.x + selectedRect.width - handle, selectedRect.y - handle, handle * 2.0f, handle * 2.0f},
            {selectedRect.x - handle, selectedRect.y + selectedRect.height - handle, handle * 2.0f, handle * 2.0f},
            {selectedRect.x + selectedRect.width - handle, selectedRect.y + selectedRect.height - handle, handle * 2.0f, handle * 2.0f},
        };
        const ResizeHandle cornerKinds[] = {ResizeHandle::NORTH_WEST, ResizeHandle::NORTH_EAST, ResizeHandle::SOUTH_WEST, ResizeHandle::SOUTH_EAST};
        for (int index = 0; index < 4; ++index) {
            if (ContainsPoint(corners[index], point)) {
                return cornerKinds[index];
            }
        }
        const float band = 6.0f;
        const Rect edges[] = {
            {selectedRect.x + handle, selectedRect.y - band, std::max(0.0f, selectedRect.width - handle * 2.0f), band * 2.0f},
            {selectedRect.x + handle, selectedRect.y + selectedRect.height - band, std::max(0.0f, selectedRect.width - handle * 2.0f), band * 2.0f},
            {selectedRect.x - band, selectedRect.y + handle, band * 2.0f, std::max(0.0f, selectedRect.height - handle * 2.0f)},
            {selectedRect.x + selectedRect.width - band, selectedRect.y + handle, band * 2.0f, std::max(0.0f, selectedRect.height - handle * 2.0f)},
        };
        const ResizeHandle edgeKinds[] = {ResizeHandle::NORTH, ResizeHandle::SOUTH, ResizeHandle::WEST, ResizeHandle::EAST};
        for (int index = 0; index < 4; ++index) {
            if (edges[index].width > 1.0f && edges[index].height > 1.0f && ContainsPoint(edges[index], point)) {
                return edgeKinds[index];
            }
        }
        return ResizeHandle::NONE;
    };
    auto pathLocked = [&](const std::vector<int>& path) {
        for (std::size_t count = 1; count <= path.size(); ++count) {
            const std::vector<int> prefix(path.begin(), path.begin() + static_cast<std::ptrdiff_t>(count));
            if (std::find(lockedPaths.begin(), lockedPaths.end(), prefix) != lockedPaths.end()) {
                return true;
            }
        }
        return false;
    };
    auto boardLabel = [&](const std::vector<int>& path, const Element* element) {
        std::string label = document.Attribute(path, "name");
        if (label.empty() && element != nullptr) {
            label = element->Id();
        }
        if (label.empty()) {
            label = element != nullptr && element->Kind() == NodeKind::FRAME ? "Frame" : (element == nullptr ? "Frame" : NodeKindName(element->Kind()));
        }
        return label;
    };
    auto boardTitleRect = [&](int index) {
        const Rect screen = screenOfPath({index});
        float width = std::max(96.0f, std::min(std::max(0.0f, screen.width), 220.0f));
        const bool editing = editTarget == EditTarget::PROPERTY && propertyName == "name" && selection.size() == 1 && selection.front() == index;
        if (editing) {
            const Size measured = renderer.MeasureText(propertyDraft, 12.0f, 100000.0f);
            width = std::max(width, measured.width + 16.0f);
        }
        return Rect{screen.x, screen.y - 20.0f, width, 20.0f};
    };
    auto overBoardTitle = [&](Point point) {
        if (preview == nullptr) {
            return false;
        }
        for (int index = 0; index < static_cast<int>(preview->Children().size()); ++index) {
            if (screenOfPath({index}).width > 1.0f && ContainsPoint(boardTitleRect(index), point)) {
                return true;
            }
        }
        return false;
    };
    auto applyCursor = [&](Point point) {
        const bool space = (GetKeyState(VK_SPACE) & 0x8000) != 0;
        WindowCursor cursor = WindowCursor::ARROW;
        if (drag == DragKind::PAN || activeTool == EditorTool::HAND || space) {
            cursor = WindowCursor::HAND;
        } else if (drag == DragKind::MARQUEE || EditorToolComponent(activeTool) != nullptr || activeTool == EditorTool::ZOOM) {
            cursor = WindowCursor::CROSS;
        } else if (drag == DragKind::RESIZE) {
            cursor = CursorForHandle(resizeHandle);
        } else if (drag == DragKind::MOVE) {
            cursor = WindowCursor::SIZE_ALL;
        } else if (activeTool == EditorTool::SELECT) {
            const ResizeHandle handle = resizeHandleAt(point);
            if (handle != ResizeHandle::NONE) {
                cursor = CursorForHandle(handle);
            } else if (ContainsPoint(selectedScreen(), point)) {
                cursor = WindowCursor::SIZE_ALL;
            }
        }
        if (drag == DragKind::NONE && activeTool == EditorTool::SELECT && overBoardTitle(point)) {
            cursor = WindowCursor::IBEAM;
        }
        if (cursor == WindowCursor::ARROW) {
            if (ContainsPoint(xmlPanel, point) || (editTarget == EditTarget::PROPERTY && ContainsPoint(inspector, point))) {
                cursor = WindowCursor::IBEAM;
            } else if (ContainsPoint(Rect{0.0f, 0.0f, LEFT_WIDTH, clientHeight}, point) || ContainsPoint(toolbar, point)) {
                cursor = WindowCursor::HAND;
            }
        }
        window.SetCursorShape(cursor);
    };

    window.SetMouseDown([&](Point point) {
        layoutChrome(clientWidth, clientHeight);
        if (fileMenuOpen) {
            for (const HitRow& row : fileMenuRows) {
                if (ContainsPoint(row.rect, point)) {
                    applyFileAction(row.depth);
                    paintDirty = true;
                    return;
                }
            }
            closeFileMenu();
            paintDirty = true;
            return;
        }
        if (fileRenaming) {
            bool onDraft = false;
            for (const FileEntry& entry : fileEntries) {
                if (entry.path.generic_string() == selectedFile && ContainsPoint(entry.rect, point)) {
                    onDraft = true;
                    break;
                }
            }
            if (!onDraft) {
                commitFileRename();
            }
        }
        finishEdit();
        editTarget = EditTarget::NONE;
        drag = DragKind::NONE;
        for (const HitRow& row : toolRows) {
            if (ContainsPoint(row.rect, point)) {
                activeTool = static_cast<EditorTool>(row.depth);
                paintDirty = true;
                return;
            }
        }
        for (const HitRow& row : navRows) {
            if (ContainsPoint(row.rect, point)) {
                leftNav = row.depth;
                paintDirty = true;
                return;
            }
        }
        if (leftNav == 0 && ContainsPoint(fullSidebar, point)) {
            if (ContainsPoint(fileNewRect, point)) {
                createExplorerFile();
                paintDirty = true;
                return;
            }
            if (ContainsPoint(folderNewRect, point)) {
                createExplorerFolder();
                paintDirty = true;
                return;
            }
            if (ContainsPoint(refreshRect, point)) {
                filesDirty = true;
                paintDirty = true;
                return;
            }
            for (const FileEntry& entry : fileEntries) {
                if (!ContainsPoint(entry.rect, point) || !ContainsPoint(fullSidebar, point)) {
                    continue;
                }
                const unsigned now = GetTickCount();
                const std::string key = entry.path.generic_string();
                const bool again = lastFileKey == key && now - lastFileTick < 400;
                lastFileTick = now;
                lastFileKey = key;
                selectedFile = key;
                if (entry.directory) {
                    const auto found = std::find(openFolders.begin(), openFolders.end(), key);
                    if (found == openFolders.end()) {
                        openFolders.push_back(key);
                    } else {
                        openFolders.erase(found);
                    }
                    filesDirty = true;
                } else if (again) {
                    const std::string extension = LowerCopy(entry.path.extension().string());
                    if (extension == ".xml" && document.LoadFile(entry.path)) {
                        selection.clear();
                        selections.clear();
                        rebuildPreview();
                    }
                }
                paintDirty = true;
                return;
            }
        }
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
                    remember();
                    selectOnly(inserted);
                    rebuildPreview();
                    finishEdit();
                }
                paintDirty = true;
                return;
            }
        }
        if (hierarchy.height > 1.0f && ContainsPoint(boardButton, point)) {
            addBoard();
            return;
        }
        if (hierarchy.height > 1.0f && ContainsPoint(filterRow, point)) {
            editTarget = EditTarget::FILTER;
            paintDirty = true;
            return;
        }
        if (hierarchy.height > 1.0f && ContainsPoint(pageRow, point)) {
            selectOnly({});
            paintDirty = true;
            return;
        }
        for (const HitRow& row : hierarchyRows) {
            if (ContainsPoint(row.rect, point) && ContainsPoint(hierarchy, point)) {
                if (row.branch && ContainsPoint(row.twist, point)) {
                    const auto found = std::find(collapsedPaths.begin(), collapsedPaths.end(), row.path);
                    if (found == collapsedPaths.end()) {
                        collapsedPaths.push_back(row.path);
                    } else {
                        collapsedPaths.erase(found);
                    }
                } else if (ContainsPoint(row.eye, point)) {
                    remember();
                    document.SetAttribute(row.path, "visible", row.hidden ? "true" : "false");
                    rebuildPreview();
                    finishEdit();
                } else if (ContainsPoint(row.lock, point)) {
                    const auto found = std::find(lockedPaths.begin(), lockedPaths.end(), row.path);
                    if (found == lockedPaths.end()) {
                        lockedPaths.push_back(row.path);
                    } else {
                        lockedPaths.erase(found);
                    }
                } else if ((GetKeyState(VK_SHIFT) & 0x8000) != 0) {
                    toggleSelect(row.path);
                } else {
                    const unsigned now = GetTickCount();
                    const bool rename = SamePath(lastClickPath, row.path) && now - lastClickTick < 400;
                    selectOnly(row.path);
                    lastClickTick = now;
                    lastClickPath = row.path;
                    if (rename) {
                        remember();
                        editTarget = EditTarget::PROPERTY;
                        propertyName = "name";
                        propertyDraft = document.Attribute(row.path, "name");
                        propertyOriginal = propertyDraft;
                        replaceProperty = true;
                    }
                }
                paintDirty = true;
                return;
            }
        }
        const Rect propertyView{inspector.x, inspector.y + 56.0f, inspector.width, std::max(0.0f, inspector.height - 56.0f)};
        for (const HitRow& row : propertyRows) {
            if (ContainsPoint(row.rect, point) && ContainsPoint(propertyView, point)) {
                remember();
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
        const bool hand = activeTool == EditorTool::HAND || (GetKeyState(VK_SPACE) & 0x8000) != 0;
        if (ContainsPoint(canvas, point) && hand) {
            drag = DragKind::PAN;
            dragStart = point;
            paintDirty = true;
            return;
        }
        if (ContainsPoint(canvas, point) && activeTool == EditorTool::ZOOM) {
            const bool zoomOut = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            zoomAt(point, zoomOut ? 0.8f : 1.25f);
            paintDirty = true;
            return;
        }
        if (const char* componentName = EditorToolComponent(activeTool); componentName != nullptr && ContainsPoint(canvas, point)) {
            const Point local = toArtboard(point);
            std::vector<int> inserted;
            if (document.InsertChild(parentForInsert(), componentName, inserted)) {
                remember();
                document.SetAttribute(inserted, "position", "absolute");
                document.SetAttribute(inserted, "x", std::to_string(static_cast<int>(Snap(local.x))));
                document.SetAttribute(inserted, "y", std::to_string(static_cast<int>(Snap(local.y))));
                selectOnly(inserted);
                activeTool = EditorTool::SELECT;
                rebuildPreview();
                finishEdit();
            }
            paintDirty = true;
            return;
        }
        const Rect horizontalRuler{canvas.x + RULER_SIZE, canvas.y, std::max(0.0f, canvas.width - RULER_SIZE), RULER_SIZE};
        const Rect verticalRuler{canvas.x, canvas.y + RULER_SIZE, RULER_SIZE, std::max(0.0f, canvas.height - RULER_SIZE)};
        if (showRulers && (ContainsPoint(horizontalRuler, point) || ContainsPoint(verticalRuler, point))) {
            const Rect board = artboardScreen();
            guideDraft = true;
            draftGuide.vertical = ContainsPoint(verticalRuler, point);
            draftGuide.position = draftGuide.vertical ? (point.x - board.x) / zoom : (point.y - board.y) / zoom;
            drag = DragKind::GUIDE;
            paintDirty = true;
            return;
        }
        resizeHandle = resizeHandleAt(point);
        if (resizeHandle != ResizeHandle::NONE && !pathLocked(selection)) {
            captureDragNodes();
            resizeGroup = dragNodes.size() > 1;
            if (resizeGroup || selectionLocal(resizeOriginX, resizeOriginY, resizeOriginWidth, resizeOriginHeight)) {
                remember();
                drag = DragKind::RESIZE;
                applyCursor(point);
                paintDirty = true;
                return;
            }
        }
        if (ContainsPoint(workspace(), point) && preview != nullptr && activeTool == EditorTool::SELECT) {
            pressPath.clear();
            int titleIndex = 0;
            for (const std::unique_ptr<Element>& child : preview->Children()) {
                (void)child;
                const Rect screen = screenOfPath({titleIndex});
                if (screen.width > 1.0f && ContainsPoint(boardTitleRect(titleIndex), point)) {
                    const unsigned now = GetTickCount();
                    const bool rename = lastTitleIndex == titleIndex && now - lastTitleTick < 400;
                    lastTitleTick = now;
                    lastTitleIndex = titleIndex;
                    selectOnly({titleIndex});
                    if (rename && !pathLocked(selection)) {
                        remember();
                        editTarget = EditTarget::PROPERTY;
                        propertyName = "name";
                        propertyDraft = boardLabel(selection, ElementAt(preview.get(), selection));
                        propertyOriginal = document.Attribute(selection, "name");
                        replaceProperty = true;
                    } else if (!pathLocked(selection)) {
                        remember();
                        captureDragNodes();
                        drag = DragKind::MOVE;
                        dragStart = point;
                        snapXActive = false;
                        snapYActive = false;
                    }
                    applyCursor(point);
                    paintDirty = true;
                    return;
                }
                ++titleIndex;
            }
            const Point local = toArtboard(point);
            Element* hit = preview->HitTest(local);
            std::vector<int> path;
            if (hit != nullptr && FindElementPath(*preview, hit, path)) {
                const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
                const bool keepBoard = !shift && selection.size() == 1 && !path.empty() && path.front() == selection.front() && path.size() > selection.size();
                if (shift) {
                    toggleSelect(path);
                } else if (keepBoard) {
                    pressPath = path;
                } else if (!pathSelected(path)) {
                    selectOnly(path);
                } else {
                    selection = path;
                }
                dragStart = point;
                if (pathLocked(selection.empty() ? path : selection)) {
                    applyCursor(point);
                    paintDirty = true;
                    return;
                }
                if ((GetKeyState(VK_MENU) & 0x8000) != 0 && (GetKeyState(VK_CONTROL) & 0x8000) != 0 && !selection.empty()) {
                    remember();
                    drag = DragKind::REORDER;
                    reorderAnchor = local.y;
                } else if ((GetKeyState(VK_MENU) & 0x8000) != 0 && !selection.empty()) {
                    remember();
                    std::vector<int> parent = path;
                    if (!parent.empty()) {
                        parent.pop_back();
                    }
                    std::vector<int> inserted;
                    if (document.PasteXml(parent, document.NodeXml(path), inserted)) {
                        selectOnly(inserted);
                        rebuildPreview();
                        captureDragNodes();
                        drag = DragKind::MOVE;
                        snapXActive = false;
                        snapYActive = false;
                    }
                } else if (!selections.empty()) {
                    remember();
                    captureDragNodes();
                    drag = DragKind::MOVE;
                    snapXActive = false;
                    snapYActive = false;
                }
            } else if (activeTool == EditorTool::SELECT) {
                if ((GetKeyState(VK_SHIFT) & 0x8000) == 0) {
                    selections.clear();
                    selection.clear();
                }
                drag = DragKind::MARQUEE;
                marqueeAnchor = point;
                marqueePoint = point;
            }
            applyCursor(point);
            paintDirty = true;
            return;
        }
        if (ContainsPoint(canvas, point) && activeTool == EditorTool::SELECT) {
            if ((GetKeyState(VK_SHIFT) & 0x8000) == 0) {
                selections.clear();
                selection.clear();
            }
            drag = DragKind::MARQUEE;
            marqueeAnchor = point;
            marqueePoint = point;
            applyCursor(point);
            paintDirty = true;
            return;
        }
        if (ContainsPoint(canvas, point)) {
            drag = DragKind::PAN;
            dragStart = point;
        }
        applyCursor(point);
        paintDirty = true;
    });

    window.SetRightDown([&](Point point) {
        layoutChrome(clientWidth, clientHeight);
        if (fileRenaming) {
            commitFileRename();
        }
        if (leftNav != 0 || !ContainsPoint(fullSidebar, point)) {
            closeFileMenu();
            paintDirty = true;
            return;
        }
        fileMenuOpen = true;
        fileMenuPoint = point;
        fileMenuTarget.clear();
        fileMenuDirectory = false;
        const float listTop = fullSidebar.y + 72.0f;
        for (const FileEntry& entry : fileEntries) {
            if (entry.rect.height < 1.0f || entry.rect.y + entry.rect.height < listTop || entry.rect.y > fullSidebar.y + fullSidebar.height) {
                continue;
            }
            if (ContainsPoint(entry.rect, point)) {
                fileMenuTarget = entry.path.generic_string();
                fileMenuDirectory = entry.directory;
                selectedFile = fileMenuTarget;
                break;
            }
        }
        layoutChrome(clientWidth, clientHeight);
        paintDirty = true;
    });

    window.SetMouseMove([&](Point point) {
        applyCursor(point);
        int nextNav = -1;
        if (drag == DragKind::NONE) {
            for (const HitRow& row : navRows) {
                if (ContainsPoint(row.rect, point)) {
                    nextNav = row.depth;
                    break;
                }
            }
        }
        if (nextNav != navHover) {
            navHover = nextNav;
            paintDirty = true;
        }
        int nextFile = -1;
        int nextAction = 0;
        if (drag == DragKind::NONE && leftNav == 0) {
            if (ContainsPoint(fileNewRect, point)) {
                nextAction = 1;
            } else if (ContainsPoint(folderNewRect, point)) {
                nextAction = 2;
            } else if (ContainsPoint(refreshRect, point)) {
                nextAction = 3;
            } else {
                for (int index = 0; index < static_cast<int>(fileEntries.size()); ++index) {
                    if (ContainsPoint(fileEntries[static_cast<std::size_t>(index)].rect, point)) {
                        nextFile = index;
                        break;
                    }
                }
            }
        }
        if (nextFile != fileHover || nextAction != fileActionHover) {
            fileHover = nextFile;
            fileActionHover = nextAction;
            paintDirty = true;
        }
        int nextMenu = -1;
        if (fileMenuOpen) {
            for (int index = 0; index < static_cast<int>(fileMenuRows.size()); ++index) {
                if (ContainsPoint(fileMenuRows[static_cast<std::size_t>(index)].rect, point)) {
                    nextMenu = index;
                    break;
                }
            }
        }
        if (nextMenu != fileMenuHover) {
            fileMenuHover = nextMenu;
            paintDirty = true;
        }
        if (drag == DragKind::NONE) {
            std::vector<int> nextHover;
            if (activeTool == EditorTool::SELECT && ContainsPoint(workspace(), point) && preview != nullptr) {
                Element* hit = preview->HitTest(toArtboard(point));
                if (hit != nullptr) {
                    FindElementPath(*preview, hit, nextHover);
                }
            }
            if (nextHover != hoverPath) {
                hoverPath = std::move(nextHover);
                paintDirty = true;
            }
            return;
        }
        if (drag == DragKind::MARQUEE) {
            marqueePoint = point;
            paintDirty = true;
            return;
        }
        if (drag == DragKind::PAN) {
            panX += point.x - dragStart.x;
            panY += point.y - dragStart.y;
            dragStart = point;
            paintDirty = true;
            return;
        }
        if (drag == DragKind::GUIDE) {
            const Rect board = artboardScreen();
            draftGuide.position = draftGuide.vertical ? (point.x - board.x) / zoom : (point.y - board.y) / zoom;
            paintDirty = true;
            return;
        }
        if (drag == DragKind::MOVE && !dragNodes.empty()) {
            const float travel = std::hypot(point.x - dragStart.x, point.y - dragStart.y);
            if (travel < 3.0f) {
                return;
            }
            const float dx = (point.x - dragStart.x) / zoom;
            const float dy = (point.y - dragStart.y) / zoom;
            const EditorDragNode& primary = dragNodes.front();
            const float rawX = primary.localX + dx;
            const float rawY = primary.localY + dy;
            const float nextX = snapAxis(rawX, true);
            const float nextY = snapAxis(rawY, false);
            snapXActive = std::fabs(nextX - rawX) > 0.01f;
            snapYActive = std::fabs(nextY - rawY) > 0.01f;
            snapX = nextX;
            snapY = nextY;
            const float applyX = nextX - primary.localX;
            const float applyY = nextY - primary.localY;
            for (const EditorDragNode& node : dragNodes) {
                document.SetAttribute(node.path, "position", "absolute");
                document.SetAttribute(node.path, "x", pixels(node.localX + applyX));
                document.SetAttribute(node.path, "y", pixels(node.localY + applyY));
                document.SetAttribute(node.path, "width", pixels(std::max(16.0f, node.width)));
                document.SetAttribute(node.path, "height", pixels(std::max(16.0f, node.height)));
            }
            rebuildPreview();
            paintDirty = true;
            return;
        }
        if (drag == DragKind::RESIZE && resizeGroup && dragUnion.width > 1.0f && dragUnion.height > 1.0f) {
            const Point local = toArtboard(point);
            float left = dragUnion.x;
            float top = dragUnion.y;
            float right = dragUnion.x + dragUnion.width;
            float bottom = dragUnion.y + dragUnion.height;
            const bool east = resizeHandle == ResizeHandle::EAST || resizeHandle == ResizeHandle::NORTH_EAST || resizeHandle == ResizeHandle::SOUTH_EAST;
            const bool west = resizeHandle == ResizeHandle::WEST || resizeHandle == ResizeHandle::NORTH_WEST || resizeHandle == ResizeHandle::SOUTH_WEST;
            const bool south = resizeHandle == ResizeHandle::SOUTH || resizeHandle == ResizeHandle::SOUTH_EAST || resizeHandle == ResizeHandle::SOUTH_WEST;
            const bool north = resizeHandle == ResizeHandle::NORTH || resizeHandle == ResizeHandle::NORTH_EAST || resizeHandle == ResizeHandle::NORTH_WEST;
            if (east) {
                right = std::max(left + 16.0f, Snap(local.x));
            }
            if (west) {
                left = std::min(right - 16.0f, Snap(local.x));
            }
            if (south) {
                bottom = std::max(top + 16.0f, Snap(local.y));
            }
            if (north) {
                top = std::min(bottom - 16.0f, Snap(local.y));
            }
            const float scaleX = (right - left) / dragUnion.width;
            const float scaleY = (bottom - top) / dragUnion.height;
            for (const EditorDragNode& node : dragNodes) {
                const float nextBoardX = left + (node.boardX - dragUnion.x) * scaleX;
                const float nextBoardY = top + (node.boardY - dragUnion.y) * scaleY;
                const float parentX = node.boardX - node.localX;
                const float parentY = node.boardY - node.localY;
                document.SetAttribute(node.path, "position", "absolute");
                document.SetAttribute(node.path, "x", pixels(nextBoardX - parentX));
                document.SetAttribute(node.path, "y", pixels(nextBoardY - parentY));
                document.SetAttribute(node.path, "width", pixels(std::max(16.0f, node.width * scaleX)));
                document.SetAttribute(node.path, "height", pixels(std::max(16.0f, node.height * scaleY)));
            }
            rebuildPreview();
            paintDirty = true;
            return;
        }
        if (drag == DragKind::RESIZE) {
            Element* parent = parentOfSelection();
            if (parent == nullptr) {
                return;
            }
            const Point local = toArtboard(point);
            const Point origin = ContentOrigin(parent);
            const float pointerX = local.x - origin.x;
            const float pointerY = local.y - origin.y;
            float x = resizeOriginX;
            float y = resizeOriginY;
            float width = resizeOriginWidth;
            float height = resizeOriginHeight;
            const bool east = resizeHandle == ResizeHandle::EAST || resizeHandle == ResizeHandle::NORTH_EAST || resizeHandle == ResizeHandle::SOUTH_EAST;
            const bool west = resizeHandle == ResizeHandle::WEST || resizeHandle == ResizeHandle::NORTH_WEST || resizeHandle == ResizeHandle::SOUTH_WEST;
            const bool south = resizeHandle == ResizeHandle::SOUTH || resizeHandle == ResizeHandle::SOUTH_EAST || resizeHandle == ResizeHandle::SOUTH_WEST;
            const bool north = resizeHandle == ResizeHandle::NORTH || resizeHandle == ResizeHandle::NORTH_EAST || resizeHandle == ResizeHandle::NORTH_WEST;
            if (east) {
                width = std::max(16.0f, snapAxis(pointerX, true) - x);
            }
            if (west) {
                const float right = resizeOriginX + resizeOriginWidth;
                x = std::min(right - 16.0f, snapAxis(pointerX, true));
                width = right - x;
            }
            if (south) {
                height = std::max(16.0f, snapAxis(pointerY, false) - y);
            }
            if (north) {
                const float bottom = resizeOriginY + resizeOriginHeight;
                y = std::min(bottom - 16.0f, snapAxis(pointerY, false));
                height = bottom - y;
            }
            if ((GetKeyState(VK_SHIFT) & 0x8000) != 0 && resizeOriginWidth > 1.0f && resizeOriginHeight > 1.0f) {
                const float aspect = resizeOriginWidth / resizeOriginHeight;
                if (east || west) {
                    const float nextHeight = std::max(16.0f, width / aspect);
                    if (north) {
                        y -= nextHeight - height;
                    }
                    height = nextHeight;
                } else if (north || south) {
                    const float nextWidth = std::max(16.0f, height * aspect);
                    if (west) {
                        x -= nextWidth - width;
                    }
                    width = nextWidth;
                }
            }
            placeSelection(x, y, width, height);
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
    window.SetMouseUp([&](Point point) {
        if (drag == DragKind::GUIDE && guideDraft && ContainsPoint(canvas, point)) {
            guides.push_back(draftGuide);
        }
        if (drag == DragKind::MARQUEE && preview != nullptr) {
            const float left = std::min(marqueeAnchor.x, marqueePoint.x);
            const float top = std::min(marqueeAnchor.y, marqueePoint.y);
            const Rect marquee{left, top, std::fabs(marqueePoint.x - marqueeAnchor.x), std::fabs(marqueePoint.y - marqueeAnchor.y)};
            if (marquee.width > 3.0f && marquee.height > 3.0f) {
                const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
                if (!shift) {
                    selections.clear();
                }
                const Rect board = artboardScreen();
                auto walk = [&](auto&& self, Element& node, std::vector<int>& path) -> void {
                    if (!path.empty()) {
                        const Rect bounds = node.Bounds();
                        const Rect screen{board.x + bounds.x * zoom, board.y + bounds.y * zoom, bounds.width * zoom, bounds.height * zoom};
                        if (RectsOverlap(screen, marquee)) {
                            if (!pathSelected(path)) {
                                selections.push_back(path);
                            }
                            return;
                        }
                    }
                    int childIndex = 0;
                    for (const std::unique_ptr<Element>& child : node.Children()) {
                        path.push_back(childIndex);
                        self(self, *child, path);
                        path.pop_back();
                        ++childIndex;
                    }
                };
                std::vector<int> path;
                walk(walk, *preview, path);
                selection = selections.empty() ? std::vector<int>{} : selections.back();
            }
        }
        if (drag == DragKind::MOVE) {
            const float travel = std::hypot(point.x - dragStart.x, point.y - dragStart.y);
            if (travel < 3.0f && !pressPath.empty()) {
                selectOnly(pressPath);
            }
        }
        pressPath.clear();
        guideDraft = false;
        snapXActive = false;
        snapYActive = false;
        resizeGroup = false;
        finishEdit();
        drag = DragKind::NONE;
        resizeHandle = ResizeHandle::NONE;
        applyCursor(point);
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
        if (leftNav == 0 && ContainsPoint(fullSidebar, point)) {
            const float listHeight = std::max(0.0f, fullSidebar.height - 76.0f);
            const int visibleRows = std::max(1, static_cast<int>(listHeight / 28.0f));
            fileScroll = std::max(0, std::min(std::max(0, static_cast<int>(fileEntries.size()) - visibleRows), fileScroll - (delta > 0 ? 1 : -1)));
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
        zoom = std::clamp(zoom * factor, 0.1f, 16.0f);
        const Rect after = artboardScreen();
        panX += point.x - (after.x + localX * zoom);
        panY += point.y - (after.y + localY * zoom);
        paintDirty = true;
    });

    window.SetKeyDown([&](unsigned key) {
        if (fileRenaming) {
            if (key == VK_ESCAPE) {
                fileRenaming = false;
                paintDirty = true;
                return true;
            }
            if (key == VK_RETURN) {
                commitFileRename();
                paintDirty = true;
                return true;
            }
            if (key == VK_BACK && !renameDraft.empty()) {
                do {
                    renameDraft.pop_back();
                } while (!renameDraft.empty() && (static_cast<unsigned char>(renameDraft.back()) & 0xC0) == 0x80);
                paintDirty = true;
            }
            return true;
        }
        if (fileMenuOpen && key == VK_ESCAPE) {
            closeFileMenu();
            paintDirty = true;
            return true;
        }
        const bool control = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        if (control && (key == 'Z' || key == 'Y')) {
            if (key == 'Y' || (GetKeyState(VK_SHIFT) & 0x8000) != 0) {
                redo();
            } else {
                undo();
            }
            return true;
        }
        if (control && key == 'A') {
            selections.clear();
            if (preview != nullptr) {
                auto walk = [&](auto&& self, Element& node, std::vector<int>& path) -> void {
                    int childIndex = 0;
                    const bool hasChild = !node.Children().empty();
                    if (!path.empty() && !hasChild) {
                        selections.push_back(path);
                    }
                    for (const std::unique_ptr<Element>& child : node.Children()) {
                        path.push_back(childIndex);
                        self(self, *child, path);
                        path.pop_back();
                        ++childIndex;
                    }
                    if (!path.empty() && hasChild) {
                        bool childTaken = false;
                        for (const std::vector<int>& chosen : selections) {
                            if (chosen.size() > path.size() && std::equal(path.begin(), path.end(), chosen.begin())) {
                                childTaken = true;
                                break;
                            }
                        }
                        if (!childTaken) {
                            selections.push_back(path);
                        }
                    }
                };
                std::vector<int> path;
                walk(walk, *preview, path);
            }
            selection = selections.empty() ? std::vector<int>{} : selections.back();
            paintDirty = true;
            return true;
        }
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
                remember();
                selectOnly(inserted);
                rebuildPreview();
                finishEdit();
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
                remember();
                selectOnly(inserted);
                rebuildPreview();
                finishEdit();
                paintDirty = true;
            }
            return true;
        }
        if (editTarget == EditTarget::NONE && !control) {
            if (key == 'V') {
                activeTool = EditorTool::SELECT;
                paintDirty = true;
                return true;
            }
            if (key == 'F') {
                activeTool = EditorTool::FRAME;
                paintDirty = true;
                return true;
            }
            if (key == 'T') {
                activeTool = EditorTool::TEXT;
                paintDirty = true;
                return true;
            }
            if (key == 'I') {
                activeTool = EditorTool::IMAGE;
                paintDirty = true;
                return true;
            }
            if (key == 'L') {
                activeTool = EditorTool::LINE;
                paintDirty = true;
                return true;
            }
            if (key == 'H') {
                activeTool = EditorTool::HAND;
                paintDirty = true;
                return true;
            }
            if (key == 'Z') {
                activeTool = EditorTool::ZOOM;
                paintDirty = true;
                return true;
            }
            if (key == '1' || key == VK_NUMPAD1) {
                zoom = 1.0f;
                paintDirty = true;
                return true;
            }
            if (key == '0' || key == VK_NUMPAD0) {
                fitted = false;
                layoutChrome(clientWidth, clientHeight);
                paintDirty = true;
                return true;
            }
            if (key == 'G') {
                showGrid = !showGrid;
                paintDirty = true;
                return true;
            }
            if (key == 'R') {
                showRulers = !showRulers;
                paintDirty = true;
                return true;
            }
            if (key == 'B') {
                addBoard();
                return true;
            }
            if (!selection.empty() && !pathLocked(selection)) {
                const bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
                const float step = (GetKeyState(VK_SHIFT) & 0x8000) != 0 ? 10.0f : 1.0f;
                float x = 0.0f;
                float y = 0.0f;
                float width = 0.0f;
                float height = 0.0f;
                if (selectionLocal(x, y, width, height)) {
                    Element* parent = parentOfSelection();
                    const float parentWidth = parent == nullptr ? width : parent->Bounds().width;
                    const float parentHeight = parent == nullptr ? height : parent->Bounds().height;
                    remember();
                    if (alt && key == VK_LEFT) {
                        placeSelection(0.0f, y, width, height);
                    } else if (alt && key == VK_RIGHT) {
                        placeSelection(parentWidth - width, y, width, height);
                    } else if (alt && key == VK_UP) {
                        placeSelection(x, 0.0f, width, height);
                    } else if (alt && key == VK_DOWN) {
                        placeSelection(x, parentHeight - height, width, height);
                    } else if (key == VK_LEFT) {
                        placeSelection(x - step, y, width, height);
                    } else if (key == VK_RIGHT) {
                        placeSelection(x + step, y, width, height);
                    } else if (key == VK_UP) {
                        placeSelection(x, y - step, width, height);
                    } else if (key == VK_DOWN) {
                        placeSelection(x, y + step, width, height);
                    } else if (key == 'C') {
                        placeSelection((parentWidth - width) * 0.5f, (parentHeight - height) * 0.5f, width, height);
                    } else {
                        width = -1.0f;
                    }
                    if (width >= 0.0f) {
                        finishEdit();
                        paintDirty = true;
                        return true;
                    }
                    historyOpen = false;
                }
            }
        }
        if (key == VK_ESCAPE) {
            if (editTarget == EditTarget::PROPERTY) {
                propertyDraft = propertyOriginal;
                commitProperty();
            }
            historyOpen = false;
            editTarget = EditTarget::NONE;
            completions.clear();
            paintDirty = true;
            return true;
        }
        if (editTarget == EditTarget::FILTER) {
            if (key == VK_BACK && !layerFilter.empty()) {
                layerFilter.pop_back();
                paintDirty = true;
            }
            return true;
        }
        if (editTarget == EditTarget::PROPERTY) {
            if (key == VK_BACK && !propertyDraft.empty()) {
                propertyDraft.pop_back();
                replaceProperty = false;
                commitProperty();
                paintDirty = true;
            } else if (key == VK_RETURN) {
                finishEdit();
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
            remember();
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
            remember();
            std::vector<std::vector<int>> paths = selections;
            if (paths.empty() && !selection.empty()) {
                paths.push_back(selection);
            }
            std::sort(paths.begin(), paths.end(), [](const std::vector<int>& left, const std::vector<int>& right) {
                if (left.size() != right.size()) {
                    return left.size() > right.size();
                }
                return left > right;
            });
            bool removed = false;
            for (const std::vector<int>& path : paths) {
                if (pathLocked(path)) {
                    continue;
                }
                removed = document.DeleteNode(path) || removed;
            }
            selection.clear();
            selections.clear();
            if (removed) {
                rebuildPreview();
                finishEdit();
                paintDirty = true;
            } else {
                historyOpen = false;
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
        if (fileRenaming) {
            if (character >= 32 && renameDraft.size() < 180) {
                renameDraft += utf8;
                paintDirty = true;
            }
            return;
        }
        if (editTarget == EditTarget::FILTER) {
            layerFilter += utf8;
            paintDirty = true;
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
            remember();
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
        DrawLabel(renderer, "RoboUIDesigner", Rect{16.0f, 0.0f, 146.0f, TOOLBAR_HEIGHT}, COLOR_INK, 14.0f, TextAlign::START);
        for (std::size_t index = 0; index < deviceRows.size(); ++index) {
            const bool active = static_cast<int>(index) == deviceIndex;
            if (active) {
                renderer.FillRectangle(deviceRows[index].rect, COLOR_SELECTED, 8.0f);
            }
            DrawLabel(renderer, deviceRows[index].name, deviceRows[index].rect, active ? COLOR_ACCENT : COLOR_MUTED, 12.0f, TextAlign::CENTER);
        }
        DrawLabel(renderer, (std::to_string(static_cast<int>(zoom * 100.0f)) + "%").c_str(), Rect{clientWidth - 80.0f, 0.0f, 64.0f, TOOLBAR_HEIGHT},
                  COLOR_ACCENT, 13.0f, TextAlign::CENTER);

        renderer.FillRectangle(iconSidebar, COLOR_PANEL, 0.0f);
        renderer.FillRectangle(fullSidebar, COLOR_PANEL, 0.0f);
        renderer.FillRectangle(inspector, COLOR_PANEL, 0.0f);
        renderer.FillRectangle(Rect{ICON_SIDEBAR - 1.0f, TOOLBAR_HEIGHT, 1.0f, iconSidebar.height}, COLOR_LINE, 0.0f);
        renderer.FillRectangle(Rect{LEFT_WIDTH - 1.0f, TOOLBAR_HEIGHT, 1.0f, iconSidebar.height}, COLOR_LINE, 0.0f);
        renderer.FillRectangle(Rect{clientWidth - RIGHT_WIDTH, TOOLBAR_HEIGHT, 1.0f, iconSidebar.height}, COLOR_LINE, 0.0f);
        for (const HitRow& row : toolRows) {
            const bool active = static_cast<int>(activeTool) == row.depth;
            if (active) {
                renderer.FillRectangle(row.rect, COLOR_SELECTED, 8.0f);
            }
            DrawLabel(renderer, row.name, row.rect, active ? COLOR_ACCENT : COLOR_MUTED, 13.0f, TextAlign::CENTER);
        }
        for (const HitRow& row : navRows) {
            const bool active = leftNav == row.depth;
            const bool hovered = navHover == row.depth;
            const Rect iconBox{row.rect.x + (row.rect.width - 36.0f) * 0.5f, row.rect.y + 2.0f, 36.0f, 36.0f};
            if (active) {
                renderer.FillRectangle(iconBox, hovered ? COLOR_NAV_HOVER : COLOR_NAV, 10.0f);
            } else if (hovered) {
                renderer.FillRectangle(iconBox, COLOR_HOVER, 10.0f);
                renderer.StrokeRectangle(iconBox, COLOR_LINE, 1.0f, 10.0f);
            }
            const Color iconColor = active || hovered ? COLOR_INK : COLOR_MUTED;
            DrawNavIcon(renderer, iconBox.x + 9.0f, iconBox.y + 9.0f, row.depth, iconColor);
            DrawLabel(renderer, row.name, Rect{row.rect.x + 2.0f, row.rect.y + 40.0f, row.rect.width - 4.0f, 16.0f}, active || hovered ? COLOR_INK : COLOR_MUTED, 11.0f,
                      TextAlign::CENTER);
        }
        if (leftNav == 0 && fullSidebar.width > 1.0f) {
            DrawLabel(renderer, "Explorer", Rect{fullSidebar.x + 14.0f, fullSidebar.y + 8.0f, fullSidebar.width - 28.0f, 22.0f}, COLOR_MUTED, 12.0f, TextAlign::START);
            auto paintAction = [&](const Rect& rect, int kind, int action, Color color) {
                if (fileActionHover == action) {
                    renderer.FillRectangle(rect, COLOR_HOVER, 8.0f);
                }
                DrawExplorerIcon(renderer, rect.x + 6.0f, rect.y + 6.0f, kind, color);
            };
            paintAction(fileNewRect, 2, 1, fileActionHover == 1 ? COLOR_INK : COLOR_MUTED);
            paintAction(folderNewRect, 3, 2, fileActionHover == 2 ? COLOR_INK : COLOR_MUTED);
            paintAction(refreshRect, 4, 3, fileActionHover == 3 ? COLOR_INK : COLOR_MUTED);
            const Rect fileList{fullSidebar.x, fullSidebar.y + 72.0f, fullSidebar.width, std::max(0.0f, fullSidebar.height - 72.0f)};
            renderer.PushClip(fileList);
            for (int index = 0; index < static_cast<int>(fileEntries.size()); ++index) {
                const FileEntry& entry = fileEntries[static_cast<std::size_t>(index)];
                if (entry.rect.y + entry.rect.height < fileList.y || entry.rect.y > fileList.y + fileList.height) {
                    continue;
                }
                const bool selected = selectedFile == entry.path.generic_string();
                const bool hovered = fileHover == index;
                if (selected) {
                    renderer.FillRectangle(entry.rect, COLOR_SELECTED, 6.0f);
                } else if (hovered) {
                    renderer.FillRectangle(entry.rect, COLOR_HOVER, 6.0f);
                }
                const float indent = 10.0f + static_cast<float>(entry.depth) * 14.0f;
                if (entry.directory) {
                    if (entry.expanded) {
                        DrawIconLine(renderer, entry.twist.x, entry.twist.y + 2.0f, entry.twist.x + 4.0f, entry.twist.y + 6.0f, COLOR_MUTED);
                        DrawIconLine(renderer, entry.twist.x + 4.0f, entry.twist.y + 6.0f, entry.twist.x + 8.0f, entry.twist.y + 2.0f, COLOR_MUTED);
                    } else {
                        DrawIconLine(renderer, entry.twist.x, entry.twist.y, entry.twist.x + 4.0f, entry.twist.y + 4.0f, COLOR_MUTED);
                        DrawIconLine(renderer, entry.twist.x + 4.0f, entry.twist.y + 4.0f, entry.twist.x, entry.twist.y + 8.0f, COLOR_MUTED);
                    }
                }
                const Color iconColor = entry.directory ? COLOR_ACCENT : (selected || hovered ? COLOR_INK : COLOR_MUTED);
                DrawExplorerIcon(renderer, fullSidebar.x + indent + 14.0f, entry.rect.y + 5.0f, entry.directory ? 0 : 1, iconColor);
                Rect label{fullSidebar.x + indent + 34.0f, entry.rect.y, std::max(0.0f, entry.rect.width - indent - 28.0f), entry.rect.height};
                if (fileRenaming && selected) {
                    renderer.FillRectangle(label, COLOR_FIELD, 4.0f);
                    renderer.StrokeRectangle(label, COLOR_SELECT, 1.0f, 4.0f);
                    Rect textRect = label;
                    textRect.x += 6.0f;
                    textRect.width = std::max(0.0f, textRect.width - 10.0f);
                    DrawLabel(renderer, renameDraft, textRect, COLOR_INK, 12.0f, TextAlign::START);
                    const Size measured = renderer.MeasureText(renameDraft, 12.0f, 100000.0f);
                    renderer.FillRectangle(Rect{textRect.x + measured.width, label.y + 5.0f, 1.0f, label.height - 10.0f}, COLOR_INK, 0.0f);
                } else {
                    DrawLabel(renderer, entry.name, label, selected || hovered ? COLOR_INK : COLOR_MUTED, 12.0f, TextAlign::START);
                }
            }
            renderer.PopClip();
        }
        renderer.FillRectangle(canvas, COLOR_CANVAS, 0.0f);
        renderer.PushClip(canvas);
        const Rect board = artboardScreen();
        renderer.SetTransform(board.x, board.y, zoom);
        auto drawGrid = [&](const Rect& area) {
            if (!showGrid || area.width < 1.0f || area.height < 1.0f) {
                return;
            }
            const bool fine = zoom >= 8.0f && area.width <= 180.0f && area.height <= 180.0f;
            const float step = fine ? 1.0f : 16.0f;
            for (float x = area.x + step; x < area.x + area.width; x += step) {
                renderer.FillRectangle(Rect{x, area.y, 1.0f / zoom, area.height}, COLOR_GRID, 0.0f);
            }
            for (float y = area.y + step; y < area.y + area.height; y += step) {
                renderer.FillRectangle(Rect{area.x, y, area.width, 1.0f / zoom}, COLOR_GRID, 0.0f);
            }
        };
        if (preview != nullptr) {
            for (const std::unique_ptr<Element>& child : preview->Children()) {
                const Rect area = child->Bounds();
                if (area.width < 1.0f || area.height < 1.0f) {
                    continue;
                }
                renderer.FillRectangle(area, COLOR_BOARD, 0.0f);
                drawGrid(area);
                child->Paint(renderer);
            }
        }
        renderer.SetTransform(0.0f, 0.0f, 1.0f);
        if (preview != nullptr) {
            int childIndex = 0;
            for (const std::unique_ptr<Element>& child : preview->Children()) {
                const std::vector<int> childPath{childIndex};
                const Rect screen = screenOfPath(childPath);
                ++childIndex;
                if (screen.width < 1.0f) {
                    continue;
                }
                const bool selectedBoard = pathSelected(childPath) || SamePath(childPath, selection);
                DrawBorder(renderer, screen, selectedBoard ? COLOR_SELECT : COLOR_TICK);
                const bool editing = editTarget == EditTarget::PROPERTY && propertyName == "name" && SamePath(childPath, selection);
                const Rect title = boardTitleRect(childIndex - 1);
                if (editing) {
                    renderer.FillRectangle(title, COLOR_BOARD, 2.0f);
                    renderer.StrokeRectangle(title, COLOR_SELECT, 1.0f, 2.0f);
                    Rect textRect = title;
                    textRect.x += 4.0f;
                    textRect.width = std::max(0.0f, textRect.width - 8.0f);
                    DrawLabel(renderer, propertyDraft, textRect, COLOR_INK, 12.0f, TextAlign::START);
                    const Size measured = renderer.MeasureText(propertyDraft, 12.0f, 100000.0f);
                    renderer.FillRectangle(Rect{textRect.x + measured.width, title.y + 3.0f, 1.0f, title.height - 6.0f}, COLOR_INK, 0.0f);
                } else {
                    DrawLabel(renderer, boardLabel(childPath, child.get()), title, selectedBoard ? COLOR_SELECT : COLOR_TICK, 12.0f, TextAlign::START);
                }
            }
        }
        auto paintGuide = [&](const EditorGuide& guide) {
            if (guide.vertical) {
                const float x = board.x + guide.position * zoom;
                renderer.FillRectangle(Rect{x, canvas.y, 1.0f, canvas.height}, COLOR_ACCENT, 0.0f);
            } else {
                const float y = board.y + guide.position * zoom;
                renderer.FillRectangle(Rect{canvas.x, y, canvas.width, 1.0f}, COLOR_ACCENT, 0.0f);
            }
        };
        for (const EditorGuide& guide : guides) {
            paintGuide(guide);
        }
        if (guideDraft) {
            paintGuide(draftGuide);
        }
        if (snapXActive || snapYActive) {
            const Point origin = ContentOrigin(parentOfSelection());
            if (snapXActive) {
                const float x = board.x + (origin.x + snapX) * zoom;
                renderer.FillRectangle(Rect{x, canvas.y, 1.0f, canvas.height}, COLOR_SELECT, 0.0f);
            }
            if (snapYActive) {
                const float y = board.y + (origin.y + snapY) * zoom;
                renderer.FillRectangle(Rect{canvas.x, y, canvas.width, 1.0f}, COLOR_SELECT, 0.0f);
            }
        }
        const std::string toolLabel = std::string(EditorToolName(activeTool)) + (selections.size() > 1 ? "  " + std::to_string(selections.size()) : "");
        const float rulerInset = showRulers ? RULER_SIZE : 0.0f;
        DrawLabel(renderer, toolLabel, Rect{canvas.x + rulerInset + 8.0f, canvas.y + canvas.height - 22.0f, 140.0f, 18.0f}, COLOR_TICK, 12.0f, TextAlign::START);
        const Rect selectedRect = selectedScreen();
        if (!hoverPath.empty() && !pathSelected(hoverPath) && !SamePath(hoverPath, selection) && activeTool == EditorTool::SELECT) {
            const Rect hoverRect = screenOfPath(hoverPath);
            if (hoverRect.width > 1.0f && hoverRect.height > 1.0f) {
                DrawBorder(renderer, hoverRect, COLOR_ACCENT);
            }
        }
        if (selections.size() > 1 && activeTool == EditorTool::SELECT) {
            for (const std::vector<int>& path : selections) {
                const Rect item = screenOfPath(path);
                if (item.width > 1.0f && item.height > 1.0f) {
                    DrawBorder(renderer, item, COLOR_SELECT);
                }
            }
        }
        if (selectedRect.width > 1.0f && selectedRect.height > 1.0f && activeTool == EditorTool::SELECT) {
            DrawBorder(renderer, selectedRect, COLOR_SELECT);
            const float handle = 4.0f;
            const Rect handles[] = {
                {selectedRect.x - handle, selectedRect.y - handle, handle * 2.0f, handle * 2.0f},
                {selectedRect.x + selectedRect.width - handle, selectedRect.y - handle, handle * 2.0f, handle * 2.0f},
                {selectedRect.x - handle, selectedRect.y + selectedRect.height - handle, handle * 2.0f, handle * 2.0f},
                {selectedRect.x + selectedRect.width - handle, selectedRect.y + selectedRect.height - handle, handle * 2.0f, handle * 2.0f},
                {selectedRect.x + selectedRect.width * 0.5f - handle, selectedRect.y - handle, handle * 2.0f, handle * 2.0f},
                {selectedRect.x + selectedRect.width * 0.5f - handle, selectedRect.y + selectedRect.height - handle, handle * 2.0f, handle * 2.0f},
                {selectedRect.x - handle, selectedRect.y + selectedRect.height * 0.5f - handle, handle * 2.0f, handle * 2.0f},
                {selectedRect.x + selectedRect.width - handle, selectedRect.y + selectedRect.height * 0.5f - handle, handle * 2.0f, handle * 2.0f},
            };
            for (const Rect& handleRect : handles) {
                renderer.FillRectangle(handleRect, COLOR_BOARD, handle);
                renderer.StrokeRectangle(handleRect, COLOR_SELECT, 1.0f, handle);
            }
        }
        if (drag == DragKind::MARQUEE) {
            const float left = std::min(marqueeAnchor.x, marqueePoint.x);
            const float top = std::min(marqueeAnchor.y, marqueePoint.y);
            const Rect marquee{left, top, std::fabs(marqueePoint.x - marqueeAnchor.x), std::fabs(marqueePoint.y - marqueeAnchor.y)};
            renderer.FillRectangle(marquee, COLOR_SELECT_WASH, 0.0f);
            DrawBorder(renderer, marquee, COLOR_SELECT);
        }
        if (showRulers) {
        renderer.FillRectangle(Rect{canvas.x, canvas.y, canvas.width, RULER_SIZE}, COLOR_PANEL, 0.0f);
        renderer.FillRectangle(Rect{canvas.x, canvas.y, RULER_SIZE, canvas.height}, COLOR_PANEL, 0.0f);
        if (selectedRect.width > 1.0f && selectedRect.height > 1.0f) {
            const float bandX = std::max(canvas.x + RULER_SIZE, selectedRect.x);
            const float bandRight = std::min(canvas.x + canvas.width, selectedRect.x + selectedRect.width);
            const float bandY = std::max(canvas.y + RULER_SIZE, selectedRect.y);
            const float bandBottom = std::min(canvas.y + canvas.height, selectedRect.y + selectedRect.height);
            if (bandRight > bandX) {
                renderer.FillRectangle(Rect{bandX, canvas.y, bandRight - bandX, RULER_SIZE}, COLOR_SELECT_WASH, 0.0f);
            }
            if (bandBottom > bandY) {
                renderer.FillRectangle(Rect{canvas.x, bandY, RULER_SIZE, bandBottom - bandY}, COLOR_SELECT_WASH, 0.0f);
            }
        }
        const float tick = zoom < 0.25f ? 400.0f : zoom < 0.5f ? 200.0f : zoom < 1.0f ? 100.0f : zoom <= 2.0f ? 50.0f : zoom < 4.0f ? 25.0f : 10.0f;
        const float viewLeft = (canvas.x + RULER_SIZE - board.x) / zoom;
        const float viewRight = (canvas.x + canvas.width - board.x) / zoom;
        const float viewTop = (canvas.y + RULER_SIZE - board.y) / zoom;
        const float viewBottom = (canvas.y + canvas.height - board.y) / zoom;
        for (float value = std::ceil(viewLeft / tick) * tick; value <= viewRight; value += tick) {
            const float x = board.x + value * zoom;
            renderer.FillRectangle(Rect{x, canvas.y + RULER_SIZE - 8.0f, 1.0f, 8.0f}, COLOR_MUTED, 0.0f);
            DrawLabel(renderer, std::to_string(static_cast<int>(std::lround(value))), Rect{x + 3.0f, canvas.y, 42.0f, 14.0f}, COLOR_MUTED, 10.0f, TextAlign::START);
        }
        for (float value = std::ceil(viewTop / tick) * tick; value <= viewBottom; value += tick) {
            const float y = board.y + value * zoom;
            renderer.FillRectangle(Rect{canvas.x + RULER_SIZE - 8.0f, y, 8.0f, 1.0f}, COLOR_MUTED, 0.0f);
            DrawLabel(renderer, std::to_string(static_cast<int>(std::lround(value))), Rect{canvas.x + 1.0f, y + 2.0f, RULER_SIZE - 2.0f, 12.0f}, COLOR_MUTED, 9.0f, TextAlign::START);
        }
        renderer.FillRectangle(Rect{canvas.x, canvas.y, RULER_SIZE, RULER_SIZE}, COLOR_PANEL, 0.0f);
        renderer.FillRectangle(Rect{canvas.x, canvas.y + RULER_SIZE - 1.0f, canvas.width, 1.0f}, COLOR_LINE, 0.0f);
        renderer.FillRectangle(Rect{canvas.x + RULER_SIZE - 1.0f, canvas.y, 1.0f, canvas.height}, COLOR_LINE, 0.0f);
        }
        renderer.PopClip();

        DrawLabel(renderer, "Design", Rect{inspector.x + 12.0f, inspector.y + 8.0f, inspector.width - 24.0f, 24.0f}, COLOR_MUTED, 12.0f, TextAlign::START);
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
            renderer.FillRectangle(row.rect, active ? COLOR_SELECTED : COLOR_FIELD, 6.0f);
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
        if (fileMenuOpen && !fileMenuRows.empty()) {
            const Rect first = fileMenuRows.front().rect;
            const Rect last = fileMenuRows.back().rect;
            const Rect popup{first.x - 4.0f, first.y - 4.0f, first.width + 8.0f, last.y + last.height - first.y + 8.0f};
            renderer.FillRectangle(popup, COLOR_FIELD, 8.0f);
            renderer.StrokeRectangle(popup, COLOR_LINE, 1.0f, 8.0f);
            for (int index = 0; index < static_cast<int>(fileMenuRows.size()); ++index) {
                const HitRow& row = fileMenuRows[static_cast<std::size_t>(index)];
                if (fileMenuHover == index) {
                    renderer.FillRectangle(row.rect, COLOR_HOVER, 6.0f);
                }
                const Color labelColor = row.depth == 4 ? COLOR_STATUS : COLOR_INK;
                Rect text = row.rect;
                text.x += 10.0f;
                text.width = std::max(0.0f, text.width - 16.0f);
                DrawLabel(renderer, row.name, text, labelColor, 13.0f, TextAlign::START);
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
