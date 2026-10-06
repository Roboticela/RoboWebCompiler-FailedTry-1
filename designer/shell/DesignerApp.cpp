#include "designer/assets/CountryNames.h"
#include "designer/assets/IconPack.h"
#include "designer/shell/DesignerApp.h"

#include "compiler/semantic/ComponentCatalog.h"
#include "compiler/semantic/Foundations.h"
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
#include "xml/serializer/XmlSerializer.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_map>

#include <commctrl.h>
#include <commoncontrols.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <wrl/client.h>

namespace Runtime {
namespace {

constexpr float TOOLBAR_HEIGHT = 36.0f;
constexpr float TAB_HEIGHT = 34.0f;
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
    LIBRARY,
    ASSET,
    VARIABLE,
    VARIABLE_VALUE,
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
    bool pending = false;
    Rect rect{};
    Rect twist{};
};

struct WorkTab {
    std::string path;
    bool designer = false;
    std::string editorText;
    bool binary = false;
    bool dirty = false;
    int scroll = 0;
    std::size_t caret = 0;
    std::vector<int> selection;
    std::vector<std::vector<int>> selections;
    std::vector<std::string> undoStack;
    std::vector<std::string> redoStack;
    float zoom = 1.0f;
    float panX = 0.0f;
    float panY = 0.0f;
    bool fitted = false;
    float deviceWidth = 800.0f;
    float deviceHeight = 600.0f;
    int deviceIndex = 0;
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

bool PathStartsWith(const std::vector<int>& path, const std::vector<int>& prefix) {
    if (prefix.empty() || path.size() < prefix.size()) {
        return false;
    }
    return std::equal(prefix.begin(), prefix.end(), path.begin());
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
        if (attribute.name == "id" || attribute.name == "name" || attribute.name == "text") {
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

const XmlNode* PrimaryWindow(const DesignDocument& document) {
    const XmlNode* root = document.Root();
    if (root == nullptr) {
        return nullptr;
    }
    if (root->name == "screens") {
        for (const XmlNode& child : root->children) {
            if (child.name == "window") {
                return &child;
            }
        }
    }
    return root;
}

float PixelAttribute(const XmlNode* node, const char* name, float fallback) {
    if (node == nullptr) {
        return fallback;
    }
    for (const XmlAttribute& attribute : node->attributes) {
        if (attribute.name != name) {
            continue;
        }
        const std::optional<Length> length = ParseLength(attribute.value);
        if (length.has_value() && length->unit == LengthUnit::PIXELS && length->value >= 1.0f) {
            return length->value;
        }
    }
    return fallback;
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

void FillTriangle(Renderer& renderer, float ax, float ay, float bx, float by, float cx, float cy, Color color) {
    const float xs[] = {ax, bx, cx};
    const float ys[] = {ay, by, cy};
    const int top = static_cast<int>(std::floor(std::min(ay, std::min(by, cy))));
    const int bottom = static_cast<int>(std::ceil(std::max(ay, std::max(by, cy))));
    for (int scan = top; scan <= bottom; ++scan) {
        const float y = static_cast<float>(scan) + 0.5f;
        float left = 100000.0f;
        float right = -100000.0f;
        for (int edge = 0; edge < 3; ++edge) {
            const int next = (edge + 1) % 3;
            const float yStart = ys[edge];
            const float yEnd = ys[next];
            if (yStart == yEnd || (y < yStart && y < yEnd) || (y > yStart && y > yEnd)) {
                continue;
            }
            const float t = (y - yStart) / (yEnd - yStart);
            const float x = xs[edge] + (xs[next] - xs[edge]) * t;
            left = std::min(left, x);
            right = std::max(right, x);
        }
        if (right - left >= 0.5f) {
            renderer.FillRectangle(Rect{left, static_cast<float>(scan), right - left, 1.0f}, color, 0.0f);
        }
    }
}

void DrawStroke(Renderer& renderer, float x1, float y1, float x2, float y2, Color color, float thickness) {
    const float dx = x2 - x1;
    const float dy = y2 - y1;
    const float length = std::sqrt(dx * dx + dy * dy);
    const int count = std::max(1, static_cast<int>(length / 0.7f));
    for (int index = 0; index <= count; ++index) {
        const float t = static_cast<float>(index) / static_cast<float>(count);
        const float px = x1 + dx * t - thickness * 0.5f;
        const float py = y1 + dy * t - thickness * 0.5f;
        renderer.FillRectangle(Rect{px, py, thickness, thickness}, color, thickness * 0.5f);
    }
}

void DrawMenuCheck(Renderer& renderer, float x, float y, Color color) {
    DrawStroke(renderer, x + 2.0f, y + 7.0f, x + 6.0f, y + 11.5f, color, 2.0f);
    DrawStroke(renderer, x + 5.5f, y + 11.5f, x + 13.0f, y + 3.0f, color, 2.0f);
}

void DrawPageIcon(Renderer& renderer, float x, float y, Color color) {
    renderer.StrokeRectangle(Rect{x + 3.0f, y + 1.0f, 10.0f, 14.0f}, color, 1.25f, 1.5f);
    renderer.FillRectangle(Rect{x + 5.5f, y + 5.0f, 5.0f, 1.25f}, color, 0.5f);
    renderer.FillRectangle(Rect{x + 5.5f, y + 8.0f, 5.0f, 1.25f}, color, 0.5f);
}

void DrawFolderIcon(Renderer& renderer, float x, float y, Color color) {
    renderer.StrokeRectangle(Rect{x + 1.0f, y + 5.0f, 14.0f, 9.0f}, color, 1.25f, 2.0f);
    renderer.StrokeRectangle(Rect{x + 1.0f, y + 2.5f, 6.0f, 3.5f}, color, 1.25f, 1.0f);
}

void DrawMenuIcon(Renderer& renderer, float x, float y, int action, Color color) {
    switch (action) {
    case 0:
    case 13:
        DrawPageIcon(renderer, x, y, color);
        renderer.StrokeRectangle(Rect{x + 8.0f, y + 7.0f, 6.0f, 5.0f}, color, 1.15f, 1.0f);
        break;
    case 7:
        DrawStroke(renderer, x + 5.0f, y + 3.0f, x + 2.0f, y + 8.0f, color, 1.4f);
        DrawStroke(renderer, x + 2.0f, y + 8.0f, x + 5.0f, y + 13.0f, color, 1.4f);
        DrawStroke(renderer, x + 10.0f, y + 3.0f, x + 13.0f, y + 8.0f, color, 1.4f);
        DrawStroke(renderer, x + 13.0f, y + 8.0f, x + 10.0f, y + 13.0f, color, 1.4f);
        break;
    case 1:
    case 10:
        DrawPageIcon(renderer, x, y, color);
        renderer.FillRectangle(Rect{x + 11.0f, y + 10.0f, 5.0f, 1.4f}, color, 0.5f);
        renderer.FillRectangle(Rect{x + 12.8f, y + 8.2f, 1.4f, 5.0f}, color, 0.5f);
        break;
    case 2:
    case 11:
    case 14:
        DrawFolderIcon(renderer, x, y, color);
        break;
    case 12:
    case 18:
        renderer.StrokeRectangle(Rect{x + 3.0f, y + 2.0f, 11.0f, 9.0f}, color, 1.25f, 1.5f);
        renderer.FillRectangle(Rect{x + 1.0f, y + 12.0f, 12.0f, 1.25f}, color, 0.5f);
        break;
    case 15:
        renderer.StrokeRectangle(Rect{x + 2.0f, y + 3.0f, 12.0f, 11.0f}, color, 1.25f, 1.5f);
        renderer.FillRectangle(Rect{x + 5.0f, y + 3.0f, 6.0f, 4.0f}, color, 1.0f);
        renderer.FillRectangle(Rect{x + 6.5f, y + 10.0f, 3.0f, 1.25f}, color, 0.5f);
        break;
    case 16:
        renderer.StrokeRectangle(Rect{x + 2.0f, y + 4.0f, 12.0f, 10.0f}, color, 1.25f, 1.5f);
        renderer.FillRectangle(Rect{x + 11.0f, y + 1.0f, 4.0f, 1.3f}, color, 0.5f);
        renderer.FillRectangle(Rect{x + 12.3f, y + 1.0f, 1.3f, 4.0f}, color, 0.5f);
        break;
    case 54:
        FillTriangle(renderer, x + 5.0f, y + 3.0f, x + 5.0f, y + 13.0f, x + 13.0f, y + 8.0f, color);
        break;
    case 17:
        DrawFolderIcon(renderer, x, y, color);
        DrawStroke(renderer, x + 8.0f, y + 7.0f, x + 13.0f, y + 12.0f, color, 1.4f);
        break;
    case 19:
        renderer.StrokeRectangle(Rect{x + 3.0f, y + 3.0f, 10.0f, 10.0f}, color, 1.25f, 5.0f);
        renderer.FillRectangle(Rect{x + 7.3f, y + 2.0f, 1.4f, 6.0f}, color, 0.5f);
        break;
    case 20:
        DrawStroke(renderer, x + 12.0f, y + 5.0f, x + 5.0f, y + 5.0f, color, 1.5f);
        DrawStroke(renderer, x + 5.0f, y + 5.0f, x + 8.0f, y + 2.2f, color, 1.5f);
        DrawStroke(renderer, x + 5.0f, y + 5.0f, x + 8.0f, y + 7.8f, color, 1.5f);
        DrawStroke(renderer, x + 6.0f, y + 8.0f, x + 12.0f, y + 12.0f, color, 1.5f);
        break;
    case 21:
        DrawStroke(renderer, x + 4.0f, y + 5.0f, x + 11.0f, y + 5.0f, color, 1.5f);
        DrawStroke(renderer, x + 11.0f, y + 5.0f, x + 8.0f, y + 2.2f, color, 1.5f);
        DrawStroke(renderer, x + 11.0f, y + 5.0f, x + 8.0f, y + 7.8f, color, 1.5f);
        DrawStroke(renderer, x + 10.0f, y + 8.0f, x + 4.0f, y + 12.0f, color, 1.5f);
        break;
    case 22:
        DrawStroke(renderer, x + 3.0f, y + 3.0f, x + 13.0f, y + 13.0f, color, 1.4f);
        DrawStroke(renderer, x + 13.0f, y + 3.0f, x + 3.0f, y + 13.0f, color, 1.4f);
        renderer.FillRectangle(Rect{x + 2.0f, y + 2.0f, 3.2f, 3.2f}, color, 1.6f);
        renderer.FillRectangle(Rect{x + 10.8f, y + 2.0f, 3.2f, 3.2f}, color, 1.6f);
        break;
    case 23:
    case 53:
        renderer.StrokeRectangle(Rect{x + 4.0f, y + 4.0f, 9.0f, 10.0f}, color, 1.25f, 1.5f);
        renderer.StrokeRectangle(Rect{x + 2.0f, y + 2.0f, 9.0f, 10.0f}, color, 1.25f, 1.5f);
        break;
    case 5:
    case 24:
        renderer.StrokeRectangle(Rect{x + 4.0f, y + 3.0f, 8.0f, 11.0f}, color, 1.25f, 1.5f);
        renderer.FillRectangle(Rect{x + 3.0f, y + 1.5f, 10.0f, 2.5f}, color, 1.0f);
        break;
    case 4:
    case 25:
        renderer.StrokeRectangle(Rect{x + 4.0f, y + 4.0f, 8.0f, 10.0f}, color, 1.25f, 1.0f);
        renderer.FillRectangle(Rect{x + 3.0f, y + 2.5f, 10.0f, 1.4f}, color, 0.5f);
        renderer.FillRectangle(Rect{x + 6.5f, y + 1.0f, 3.0f, 1.5f}, color, 0.5f);
        break;
    case 26:
        renderer.StrokeRectangle(Rect{x + 2.0f, y + 2.0f, 12.0f, 12.0f}, color, 1.25f, 2.0f);
        renderer.FillRectangle(Rect{x + 5.0f, y + 5.0f, 6.0f, 6.0f}, color, 1.0f);
        break;
    case 27:
        renderer.StrokeRectangle(Rect{x + 2.0f, y + 2.0f, 12.0f, 12.0f}, color, 1.25f, 2.0f);
        DrawStroke(renderer, x + 4.0f, y + 12.0f, x + 12.0f, y + 4.0f, color, 1.4f);
        break;
    case 28:
    case 29:
    case 48:
        renderer.StrokeRectangle(Rect{x + 2.0f, y + 2.0f, 8.0f, 8.0f}, color, 1.25f, 4.0f);
        DrawStroke(renderer, x + 9.0f, y + 9.0f, x + 13.5f, y + 13.5f, color, 1.6f);
        if (action == 28) {
            renderer.FillRectangle(Rect{x + 4.5f, y + 5.3f, 3.0f, 1.3f}, color, 0.5f);
            renderer.FillRectangle(Rect{x + 5.35f, y + 4.4f, 1.3f, 3.0f}, color, 0.5f);
        } else if (action == 29) {
            renderer.FillRectangle(Rect{x + 4.5f, y + 5.3f, 3.0f, 1.3f}, color, 0.5f);
        }
        break;
    case 31:
        renderer.StrokeRectangle(Rect{x + 2.0f, y + 2.0f, 12.0f, 12.0f}, color, 1.25f, 2.0f);
        renderer.FillRectangle(Rect{x + 7.2f, y + 4.0f, 1.6f, 8.0f}, color, 0.5f);
        break;
    case 30:
        renderer.StrokeRectangle(Rect{x + 2.0f, y + 2.0f, 4.0f, 4.0f}, color, 1.25f, 1.0f);
        renderer.StrokeRectangle(Rect{x + 10.0f, y + 2.0f, 4.0f, 4.0f}, color, 1.25f, 1.0f);
        renderer.StrokeRectangle(Rect{x + 2.0f, y + 10.0f, 4.0f, 4.0f}, color, 1.25f, 1.0f);
        renderer.StrokeRectangle(Rect{x + 10.0f, y + 10.0f, 4.0f, 4.0f}, color, 1.25f, 1.0f);
        break;
    case 32:
        renderer.StrokeRectangle(Rect{x + 2.0f, y + 2.0f, 5.0f, 5.0f}, color, 1.2f, 1.0f);
        renderer.StrokeRectangle(Rect{x + 9.0f, y + 2.0f, 5.0f, 5.0f}, color, 1.2f, 1.0f);
        renderer.StrokeRectangle(Rect{x + 2.0f, y + 9.0f, 5.0f, 5.0f}, color, 1.2f, 1.0f);
        renderer.StrokeRectangle(Rect{x + 9.0f, y + 9.0f, 5.0f, 5.0f}, color, 1.2f, 1.0f);
        break;
    case 33:
        renderer.FillRectangle(Rect{x + 2.0f, y + 3.0f, 12.0f, 1.3f}, color, 0.5f);
        renderer.FillRectangle(Rect{x + 3.0f, y + 3.0f, 1.3f, 11.0f}, color, 0.5f);
        renderer.FillRectangle(Rect{x + 6.0f, y + 1.5f, 1.0f, 3.0f}, color, 0.4f);
        renderer.FillRectangle(Rect{x + 10.0f, y + 1.5f, 1.0f, 3.0f}, color, 0.4f);
        break;
    case 34:
        renderer.StrokeRectangle(Rect{x + 1.0f, y + 2.0f, 14.0f, 9.0f}, color, 1.25f, 1.5f);
        renderer.FillRectangle(Rect{x + 6.0f, y + 12.0f, 4.0f, 1.3f}, color, 0.5f);
        break;
    case 35:
        renderer.StrokeRectangle(Rect{x + 3.0f, y + 1.0f, 10.0f, 14.0f}, color, 1.25f, 2.0f);
        break;
    case 36:
        renderer.StrokeRectangle(Rect{x + 5.0f, y + 1.0f, 6.0f, 14.0f}, color, 1.25f, 2.0f);
        renderer.FillRectangle(Rect{x + 7.2f, y + 12.5f, 1.6f, 1.6f}, color, 0.8f);
        break;
    case 37:
        DrawFolderIcon(renderer, x, y, color);
        break;
    case 38:
        renderer.StrokeRectangle(Rect{x + 4.0f, y + 1.0f, 10.0f, 12.0f}, color, 1.2f, 1.0f);
        renderer.StrokeRectangle(Rect{x + 2.0f, y + 3.0f, 10.0f, 12.0f}, color, 1.2f, 1.0f);
        break;
    case 39:
        renderer.FillRectangle(Rect{x + 2.0f, y + 2.0f, 5.0f, 5.0f}, color, 1.0f);
        renderer.FillRectangle(Rect{x + 9.0f, y + 2.0f, 5.0f, 5.0f}, color, 1.0f);
        renderer.FillRectangle(Rect{x + 2.0f, y + 9.0f, 5.0f, 5.0f}, color, 1.0f);
        renderer.FillRectangle(Rect{x + 9.0f, y + 9.0f, 5.0f, 5.0f}, color, 1.0f);
        break;
    case 40:
    case 45:
    case 72:
        renderer.StrokeRectangle(Rect{x + 1.0f, y + 3.0f, 14.0f, 11.0f}, color, 1.25f, 1.5f);
        renderer.FillRectangle(Rect{x + 4.0f, y + 6.0f, 2.5f, 2.5f}, color, 1.2f);
        DrawStroke(renderer, x + 3.0f, y + 12.0f, x + 8.0f, y + 8.0f, color, 1.3f);
        DrawStroke(renderer, x + 8.0f, y + 8.0f, x + 13.0f, y + 12.0f, color, 1.3f);
        break;
    case 41:
        renderer.StrokeRectangle(Rect{x + 3.0f, y + 2.0f, 10.0f, 12.0f}, color, 1.25f, 2.0f);
        renderer.FillRectangle(Rect{x + 6.0f, y + 6.0f, 4.0f, 1.3f}, color, 0.5f);
        renderer.FillRectangle(Rect{x + 6.0f, y + 9.0f, 4.0f, 1.3f}, color, 0.5f);
        break;
    case 42:
        FillTriangle(renderer, x + 3.0f, y + 2.0f, x + 3.0f, y + 13.0f, x + 8.0f, y + 9.0f, color);
        DrawStroke(renderer, x + 7.5f, y + 9.5f, x + 12.0f, y + 14.0f, color, 1.6f);
        break;
    case 43:
    case 52:
    case 70:
        renderer.StrokeRectangle(Rect{x + 2.0f, y + 2.0f, 12.0f, 12.0f}, color, 1.4f, 2.5f);
        break;
    case 44:
    case 71:
    case 83:
    case 84:
    case 85: {
        const float weight = action == 85 ? 2.2f : action == 84 ? 1.7f : 1.25f;
        renderer.FillRectangle(Rect{x + 3.0f, y + 2.0f, 10.0f, weight}, color, 0.5f);
        renderer.FillRectangle(Rect{x + 8.0f - weight * 0.5f, y + 2.0f, weight, 12.0f}, color, 0.5f);
        break;
    }
    case 46:
    case 75:
        DrawStroke(renderer, x + 3.0f, y + 13.0f, x + 13.0f, y + 3.0f, color, 1.6f);
        break;
    case 47:
        renderer.StrokeRectangle(Rect{x + 4.0f, y + 6.0f, 8.0f, 7.0f}, color, 1.25f, 3.0f);
        renderer.FillRectangle(Rect{x + 6.0f, y + 2.0f, 2.0f, 5.0f}, color, 1.0f);
        renderer.FillRectangle(Rect{x + 9.0f, y + 3.0f, 2.0f, 4.0f}, color, 1.0f);
        break;
    case 73:
        renderer.StrokeRectangle(Rect{x + 2.0f, y + 2.0f, 12.0f, 12.0f}, color, 1.4f, 6.0f);
        break;
    case 74:
        renderer.FillRectangle(Rect{x + 2.0f, y + 12.0f, 2.4f, 2.4f}, color, 1.2f);
        renderer.FillRectangle(Rect{x + 7.0f, y + 2.0f, 2.4f, 2.4f}, color, 1.2f);
        renderer.FillRectangle(Rect{x + 12.0f, y + 8.0f, 2.4f, 2.4f}, color, 1.2f);
        DrawStroke(renderer, x + 3.0f, y + 12.0f, x + 8.0f, y + 3.0f, color, 1.3f);
        DrawStroke(renderer, x + 8.0f, y + 3.0f, x + 13.0f, y + 8.0f, color, 1.3f);
        break;
    case 76:
        renderer.StrokeRectangle(Rect{x + 1.0f, y + 4.0f, 9.0f, 9.0f}, color, 1.2f, 1.5f);
        renderer.StrokeRectangle(Rect{x + 6.0f, y + 2.0f, 9.0f, 9.0f}, color, 1.2f, 1.5f);
        break;
    case 77:
        renderer.StrokeRectangle(Rect{x + 2.0f, y + 2.0f, 12.0f, 12.0f}, color, 1.25f, 2.0f);
        renderer.FillRectangle(Rect{x + 11.0f, y + 4.0f, 1.6f, 5.0f}, color, 0.8f);
        break;
    case 50:
        renderer.StrokeRectangle(Rect{x + 1.0f, y + 5.0f, 9.0f, 9.0f}, color, 1.2f, 1.5f);
        renderer.FillRectangle(Rect{x + 6.0f, y + 2.0f, 8.0f, 8.0f}, color, 1.5f);
        break;
    case 51:
        renderer.FillRectangle(Rect{x + 1.0f, y + 5.0f, 8.0f, 8.0f}, color, 1.5f);
        renderer.StrokeRectangle(Rect{x + 6.0f, y + 2.0f, 9.0f, 9.0f}, color, 1.2f, 1.5f);
        break;
    case 80:
        renderer.FillRectangle(Rect{x + 2.0f, y + 3.0f, 12.0f, 1.4f}, color, 0.5f);
        renderer.FillRectangle(Rect{x + 2.0f, y + 7.0f, 8.0f, 1.4f}, color, 0.5f);
        renderer.FillRectangle(Rect{x + 2.0f, y + 11.0f, 10.0f, 1.4f}, color, 0.5f);
        break;
    case 81:
        renderer.FillRectangle(Rect{x + 2.0f, y + 3.0f, 12.0f, 1.4f}, color, 0.5f);
        renderer.FillRectangle(Rect{x + 4.0f, y + 7.0f, 8.0f, 1.4f}, color, 0.5f);
        renderer.FillRectangle(Rect{x + 3.0f, y + 11.0f, 10.0f, 1.4f}, color, 0.5f);
        break;
    case 82:
        renderer.FillRectangle(Rect{x + 2.0f, y + 3.0f, 12.0f, 1.4f}, color, 0.5f);
        renderer.FillRectangle(Rect{x + 6.0f, y + 7.0f, 8.0f, 1.4f}, color, 0.5f);
        renderer.FillRectangle(Rect{x + 4.0f, y + 11.0f, 10.0f, 1.4f}, color, 0.5f);
        break;
    case 90:
        renderer.FillRectangle(Rect{x + 3.0f, y + 7.3f, 10.0f, 1.6f}, color, 0.8f);
        break;
    case 91:
        renderer.StrokeRectangle(Rect{x + 2.0f, y + 2.0f, 12.0f, 12.0f}, color, 1.25f, 2.0f);
        DrawStroke(renderer, x + 8.0f, y + 5.0f, x + 11.0f, y + 5.0f, color, 1.3f);
        DrawStroke(renderer, x + 11.0f, y + 5.0f, x + 11.0f, y + 8.0f, color, 1.3f);
        break;
    case 3:
        DrawStroke(renderer, x + 11.0f, y + 2.0f, x + 4.0f, y + 13.0f, color, 1.5f);
        renderer.FillRectangle(Rect{x + 2.5f, y + 12.0f, 4.0f, 2.0f}, color, 0.5f);
        break;
    case 6:
    case 61:
        renderer.StrokeRectangle(Rect{x + 1.0f, y + 5.0f, 14.0f, 8.0f}, color, 1.2f, 1.5f);
        renderer.FillRectangle(Rect{x + 3.5f, y + 7.5f, 2.0f, 2.0f}, color, 0.6f);
        renderer.FillRectangle(Rect{x + 7.0f, y + 7.5f, 2.0f, 2.0f}, color, 0.6f);
        renderer.FillRectangle(Rect{x + 10.5f, y + 7.5f, 2.0f, 2.0f}, color, 0.6f);
        break;
    case 60:
        renderer.StrokeRectangle(Rect{x + 2.0f, y + 2.0f, 12.0f, 12.0f}, color, 1.25f, 6.0f);
        renderer.FillRectangle(Rect{x + 7.2f, y + 4.0f, 1.6f, 1.6f}, color, 0.8f);
        renderer.FillRectangle(Rect{x + 7.2f, y + 7.0f, 1.6f, 5.0f}, color, 0.6f);
        break;
    default:
        break;
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

constexpr Color ICON_FOLDER{0xDC / 255.0f, 0xB6 / 255.0f, 0x7A / 255.0f, 1.0f};
constexpr Color ICON_YELLOW{0xCB / 255.0f, 0xCB / 255.0f, 0x41 / 255.0f, 1.0f};
constexpr Color ICON_BLUE{0x51 / 255.0f, 0x9A / 255.0f, 0xBA / 255.0f, 1.0f};
constexpr Color ICON_ORANGE{0xE3 / 255.0f, 0x79 / 255.0f, 0x33 / 255.0f, 1.0f};
constexpr Color ICON_GREEN{0x8D / 255.0f, 0xC1 / 255.0f, 0x49 / 255.0f, 1.0f};
constexpr Color ICON_PURPLE{0xA0 / 255.0f, 0x74 / 255.0f, 0xC4 / 255.0f, 1.0f};
constexpr Color ICON_PINK{0x9A / 255.0f, 0x4D / 255.0f, 0x76 / 255.0f, 1.0f};
constexpr Color ICON_GRAY{0xD4 / 255.0f, 0xD4 / 255.0f, 0xD4 / 255.0f, 1.0f};
constexpr Color ICON_SLATE{0x6D / 255.0f, 0x80 / 255.0f, 0x86 / 255.0f, 1.0f};
constexpr Color ICON_RED{0xE5 / 255.0f, 0x39 / 255.0f, 0x35 / 255.0f, 1.0f};
constexpr Color ICON_GIT{0xF0 / 255.0f, 0x51 / 255.0f, 0x33 / 255.0f, 1.0f};
constexpr Color ICON_CSS{0x42 / 255.0f, 0xA5 / 255.0f, 0xF5 / 255.0f, 1.0f};
constexpr Color ICON_PYTHON{0x35 / 255.0f, 0x72 / 255.0f, 0xA5 / 255.0f, 1.0f};
constexpr Color ICON_INK{0.10f, 0.10f, 0.10f, 1.0f};
constexpr Color ICON_PAPER{1.0f, 1.0f, 1.0f, 1.0f};

struct ExplorerIcon {
    int kind = 1;
    Color color = ICON_GRAY;
    const char* label = nullptr;
    bool darkInk = false;
};

Color ShadeColor(Color color, float scale) {
    return Color{color.r * scale, color.g * scale, color.b * scale, color.a};
}

Color LiftColor(Color color, float amount) {
    return Color{std::min(1.0f, color.r + amount), std::min(1.0f, color.g + amount), std::min(1.0f, color.b + amount), color.a};
}

void DrawFolderGlyph(Renderer& renderer, float x, float y, bool open, Color color) {
    if (open) {
        renderer.FillRectangle(Rect{x + 1.0f, y + 2.0f, 14.0f, 6.5f}, ShadeColor(color, 0.78f), 1.4f);
        renderer.FillRectangle(Rect{x + 1.0f, y + 6.2f, 14.0f, 8.0f}, color, 1.6f);
        return;
    }
    renderer.FillRectangle(Rect{x + 1.0f, y + 3.0f, 7.0f, 3.6f}, color, 1.2f);
    renderer.FillRectangle(Rect{x + 1.0f, y + 5.2f, 14.0f, 9.0f}, color, 1.6f);
}

void DrawPageGlyph(Renderer& renderer, float x, float y, Color color) {
    renderer.FillRectangle(Rect{x + 3.0f, y + 1.0f, 10.0f, 14.0f}, color, 1.4f);
    FillTriangle(renderer, x + 8.4f, y + 1.0f, x + 13.0f, y + 1.0f, x + 13.0f, y + 5.4f, LiftColor(color, 0.22f));
}

void DrawBadgeGlyph(Renderer& renderer, float x, float y, Color color, const char* label, bool darkInk) {
    renderer.FillRectangle(Rect{x + 1.0f, y + 1.0f, 14.0f, 14.0f}, color, 3.0f);
    TextStyle style;
    const std::size_t length = label == nullptr ? 0 : std::strlen(label);
    style.fontSize = length >= 3 ? 6.0f : (length == 2 ? 7.5f : 9.0f);
    style.fontWeight = 700;
    style.color = darkInk ? ICON_INK : ICON_PAPER;
    style.horizontal = TextAlign::CENTER;
    style.vertical = TextAlign::CENTER;
    renderer.DrawString(label == nullptr ? "" : label, Rect{x + 1.0f, y + 1.0f, 14.0f, 14.0f}, style);
}

ExplorerIcon BadgeIcon(const char* label, Color color, bool darkInk) {
    return ExplorerIcon{20, color, label, darkInk};
}

ExplorerIcon ShapeIcon(int kind, Color color) {
    return ExplorerIcon{kind, color, nullptr, false};
}

ExplorerIcon DescribeExplorerIcon(const std::filesystem::path& path) {
    const std::string name = LowerCopy(path.filename().string());
    if (name == ".gitignore" || name == ".gitattributes" || name == ".gitmodules") {
        return ShapeIcon(17, ICON_GIT);
    }
    if (name == "cmakelists.txt" || name == "cmakecache.txt" || name == "makefile") {
        return BadgeIcon("Mk", ICON_SLATE, false);
    }
    std::string extension = LowerCopy(path.extension().string());
    if (!extension.empty() && extension.front() == '.') {
        extension.erase(extension.begin());
    }
    if (extension == "js" || extension == "jsx" || extension == "mjs" || extension == "cjs") {
        return BadgeIcon("JS", ICON_YELLOW, true);
    }
    if (extension == "ts" || extension == "tsx" || extension == "mts" || extension == "cts") {
        return BadgeIcon("TS", ICON_BLUE, false);
    }
    if (extension == "json" || extension == "jsonc") {
        return BadgeIcon("{}", ICON_YELLOW, true);
    }
    if (extension == "html" || extension == "htm") {
        return BadgeIcon("<>", ICON_ORANGE, false);
    }
    if (extension == "xml") {
        return BadgeIcon("</>", ICON_GREEN, true);
    }
    if (extension == "css" || extension == "scss" || extension == "less") {
        return BadgeIcon("#", ICON_CSS, false);
    }
    if (extension == "cpp" || extension == "cc" || extension == "cxx" || extension == "hpp" || extension == "hxx") {
        return BadgeIcon("C++", ICON_PINK, false);
    }
    if (extension == "c") {
        return BadgeIcon("C", ICON_BLUE, false);
    }
    if (extension == "h" || extension == "hh" || extension == "inl") {
        return BadgeIcon("H", ICON_PURPLE, false);
    }
    if (extension == "py") {
        return BadgeIcon("Py", ICON_PYTHON, false);
    }
    if (extension == "java") {
        return BadgeIcon("Ja", ICON_ORANGE, false);
    }
    if (extension == "cs") {
        return BadgeIcon("C#", ICON_PURPLE, false);
    }
    if (extension == "go") {
        return BadgeIcon("Go", ICON_BLUE, false);
    }
    if (extension == "rs") {
        return BadgeIcon("Rs", ICON_ORANGE, true);
    }
    if (extension == "php") {
        return BadgeIcon("P", ICON_PURPLE, false);
    }
    if (extension == "rb") {
        return BadgeIcon("Rb", ICON_RED, false);
    }
    if (extension == "sql") {
        return BadgeIcon("SQL", ICON_ORANGE, false);
    }
    if (extension == "vue") {
        return BadgeIcon("V", ICON_GREEN, true);
    }
    if (extension == "md" || extension == "markdown") {
        return BadgeIcon("M", ICON_BLUE, false);
    }
    if (extension == "yaml" || extension == "yml") {
        return BadgeIcon("Y", ICON_PURPLE, false);
    }
    if (extension == "bat" || extension == "cmd" || extension == "ps1" || extension == "sh") {
        return ShapeIcon(10, ICON_GREEN);
    }
    if (extension == "png" || extension == "jpg" || extension == "jpeg" || extension == "gif" || extension == "webp" || extension == "ico" || extension == "bmp" ||
        extension == "avif" || extension == "svg") {
        return ShapeIcon(9, ICON_PURPLE);
    }
    if (extension == "zip" || extension == "7z" || extension == "rar" || extension == "tar" || extension == "gz") {
        return ShapeIcon(11, ICON_SLATE);
    }
    if (extension == "exe" || extension == "dll" || extension == "lib" || extension == "pdb") {
        return ShapeIcon(12, ICON_GREEN);
    }
    if (extension == "pdf") {
        return BadgeIcon("PDF", ICON_RED, false);
    }
    if (extension == "sln" || extension == "vcxproj" || extension == "filters") {
        return BadgeIcon("VS", ICON_PURPLE, false);
    }
    if (extension == "cmake") {
        return BadgeIcon("Mk", ICON_SLATE, false);
    }
    if (extension == "txt" || extension == "log" || extension == "csv" || extension == "rst" || extension == "ini" || extension == "cfg" || extension == "conf" ||
        extension == "toml") {
        return ShapeIcon(8, extension == "toml" || extension == "ini" || extension == "cfg" || extension == "conf" ? ICON_SLATE : ICON_GRAY);
    }
    return ShapeIcon(1, ICON_GRAY);
}

void DrawLibraryGlyph(Renderer& renderer, const Rect& well, const std::string& name) {
    const float x = well.x + 8.0f;
    const float y = well.y + 8.0f;
    const float size = std::max(8.0f, well.width - 16.0f);
    const Color ink = COLOR_ACCENT;
    const Color quiet = COLOR_MUTED;
    if (name == "frame") {
        renderer.StrokeRectangle(Rect{x, y, size, size}, ink, 1.5f, 4.0f);
        renderer.FillRectangle(Rect{x + 4.0f, y + 4.0f, size - 8.0f, 3.0f}, quiet, 1.0f);
    } else if (name == "text") {
        renderer.FillRectangle(Rect{x, y + 2.0f, size, 2.0f}, ink, 1.0f);
        renderer.FillRectangle(Rect{x, y + 7.0f, size * 0.72f, 2.0f}, quiet, 1.0f);
        renderer.FillRectangle(Rect{x, y + 12.0f, size * 0.88f, 2.0f}, quiet, 1.0f);
    } else if (name == "image") {
        renderer.StrokeRectangle(Rect{x, y, size, size}, ink, 1.5f, 3.0f);
        renderer.FillRectangle(Rect{x + 3.0f, y + 3.0f, 4.0f, 4.0f}, ink, 2.0f);
        renderer.FillRectangle(Rect{x + 2.0f, y + size - 6.0f, size - 4.0f, 3.0f}, quiet, 1.0f);
    } else if (name == "vector") {
        renderer.StrokeRectangle(Rect{x + 2.0f, y + 2.0f, size - 4.0f, size - 4.0f}, ink, 1.5f, (size - 4.0f) * 0.5f);
    } else if (name == "path") {
        renderer.FillRectangle(Rect{x, y + size - 4.0f, 3.0f, 3.0f}, ink, 1.5f);
        renderer.FillRectangle(Rect{x + size * 0.45f, y + 2.0f, 3.0f, 3.0f}, ink, 1.5f);
        renderer.FillRectangle(Rect{x + size - 3.0f, y + size * 0.45f, 3.0f, 3.0f}, ink, 1.5f);
        renderer.FillRectangle(Rect{x + 2.0f, y + size * 0.42f, size * 0.42f, 1.5f}, quiet, 0.0f);
    } else if (name == "line") {
        for (int step = 0; step < 8; ++step) {
            const float t = static_cast<float>(step) / 7.0f;
            renderer.FillRectangle(Rect{x + t * (size - 2.0f), y + (1.0f - t) * (size - 2.0f), 2.0f, 2.0f}, ink, 1.0f);
        }
    } else if (name == "group") {
        renderer.StrokeRectangle(Rect{x, y + 4.0f, size - 6.0f, size - 6.0f}, quiet, 1.5f, 3.0f);
        renderer.StrokeRectangle(Rect{x + 6.0f, y, size - 6.0f, size - 6.0f}, ink, 1.5f, 3.0f);
    } else if (name == "scroll" || name == "scrollbar") {
        renderer.StrokeRectangle(Rect{x, y, size, size}, ink, 1.5f, 3.0f);
        renderer.FillRectangle(Rect{x + size - 5.0f, y + 3.0f, 2.0f, size * 0.45f}, quiet, 1.0f);
    } else if (name == "component") {
        renderer.StrokeRectangle(Rect{x + size * 0.18f, y, size * 0.64f, size * 0.64f}, ink, 1.5f, 2.0f);
        renderer.StrokeRectangle(Rect{x, y + size * 0.36f, size * 0.64f, size * 0.64f}, quiet, 1.5f, 2.0f);
    } else if (name == "instance") {
        renderer.StrokeRectangle(Rect{x, y, size, size}, ink, 1.5f, 3.0f);
        renderer.FillRectangle(Rect{x + size * 0.28f, y + size * 0.28f, size * 0.44f, size * 0.44f}, ink, 2.0f);
    } else if (name == "slot") {
        renderer.FillRectangle(Rect{x, y, size * 0.28f, 1.5f}, ink, 0.0f);
        renderer.FillRectangle(Rect{x + size * 0.72f, y, size * 0.28f, 1.5f}, ink, 0.0f);
        renderer.FillRectangle(Rect{x, y + size - 1.5f, size * 0.28f, 1.5f}, ink, 0.0f);
        renderer.FillRectangle(Rect{x + size * 0.72f, y + size - 1.5f, size * 0.28f, 1.5f}, ink, 0.0f);
        renderer.FillRectangle(Rect{x, y, 1.5f, size * 0.28f}, ink, 0.0f);
        renderer.FillRectangle(Rect{x + size - 1.5f, y, 1.5f, size * 0.28f}, ink, 0.0f);
        renderer.FillRectangle(Rect{x, y + size * 0.72f, 1.5f, size * 0.28f}, ink, 0.0f);
        renderer.FillRectangle(Rect{x + size - 1.5f, y + size * 0.72f, 1.5f, size * 0.28f}, ink, 0.0f);
    } else if (name == "window" || name == "board") {
        renderer.StrokeRectangle(Rect{x, y, size, size * 0.72f}, ink, 1.5f, 3.0f);
        renderer.FillRectangle(Rect{x + 3.0f, y + 3.0f, size - 6.0f, 2.0f}, name == "board" ? quiet : ink, 1.0f);
    } else if (name == "shadow") {
        renderer.StrokeRectangle(Rect{x + 4.0f, y + 4.0f, size - 4.0f, size - 4.0f}, quiet, 1.5f, 3.0f);
        renderer.StrokeRectangle(Rect{x, y, size - 4.0f, size - 4.0f}, ink, 1.5f, 3.0f);
    }
}

void DrawExplorerIcon(Renderer& renderer, float x, float y, int kind, Color color, const char* label = nullptr, bool darkInk = false) {
    if (kind == 0 || kind == 3 || kind == 16) {
        DrawFolderGlyph(renderer, x, y, kind == 16, color);
        if (kind == 3) {
            const float luma = color.r * 0.3f + color.g * 0.59f + color.b * 0.11f;
            const Color plus = luma > 0.65f ? ICON_INK : ICON_PAPER;
            renderer.FillRectangle(Rect{x + 10.0f, y + 0.5f, 5.0f, 1.6f}, plus, 0.0f);
            renderer.FillRectangle(Rect{x + 11.7f, y - 0.2f, 1.6f, 5.0f}, plus, 0.0f);
        }
    } else if (kind == 1 || kind == 2 || kind == 8) {
        DrawPageGlyph(renderer, x, y, color);
        if (kind == 8) {
            const Color line = darkInk ? ICON_INK : ShadeColor(color, 0.55f);
            renderer.FillRectangle(Rect{x + 5.0f, y + 6.2f, 6.0f, 1.1f}, line, 0.0f);
            renderer.FillRectangle(Rect{x + 5.0f, y + 8.6f, 4.4f, 1.1f}, line, 0.0f);
            renderer.FillRectangle(Rect{x + 5.0f, y + 11.0f, 5.2f, 1.1f}, line, 0.0f);
        }
        if (kind == 2) {
            const float luma = color.r * 0.3f + color.g * 0.59f + color.b * 0.11f;
            const Color plus = luma > 0.65f ? ICON_INK : ICON_PAPER;
            renderer.FillRectangle(Rect{x + 10.0f, y + 10.0f, 5.0f, 1.6f}, plus, 0.0f);
            renderer.FillRectangle(Rect{x + 11.7f, y + 8.3f, 1.6f, 5.0f}, plus, 0.0f);
        }
    } else if (kind == 4) {
        const float centerX = x + 8.0f;
        const float centerY = y + 8.0f;
        const float radius = 5.0f;
        const float start = -2.15f;
        const float sweep = 5.15f;
        const int segments = 24;
        for (int index = 0; index <= segments; ++index) {
            const float angle = start + sweep * (static_cast<float>(index) / static_cast<float>(segments));
            renderer.FillRectangle(Rect{centerX + std::cos(angle) * radius - 1.15f, centerY + std::sin(angle) * radius - 1.15f, 2.3f, 2.3f}, color, 1.15f);
        }
        const float end = start + sweep;
        const float headX = centerX + std::cos(end) * radius;
        const float headY = centerY + std::sin(end) * radius;
        const float tangentX = -std::sin(end);
        const float tangentY = std::cos(end);
        const float normalX = std::cos(end);
        const float normalY = std::sin(end);
        FillTriangle(renderer, headX + tangentX * 4.2f, headY + tangentY * 4.2f, headX - tangentX * 1.4f + normalX * 2.8f, headY - tangentY * 1.4f + normalY * 2.8f,
                     headX - tangentX * 1.4f - normalX * 2.8f, headY - tangentY * 1.4f - normalY * 2.8f, color);
    } else if (kind == 9) {
        renderer.FillRectangle(Rect{x + 1.0f, y + 3.0f, 14.0f, 11.0f}, color, 2.0f);
        renderer.FillRectangle(Rect{x + 3.2f, y + 5.0f, 2.8f, 2.8f}, ICON_PAPER, 1.4f);
        FillTriangle(renderer, x + 2.2f, y + 13.2f, x + 7.4f, y + 8.4f, x + 10.2f, y + 13.2f, ICON_PAPER);
        FillTriangle(renderer, x + 7.2f, y + 13.2f, x + 11.0f, y + 9.2f, x + 14.4f, y + 13.2f, ShadeColor(ICON_PAPER, 0.82f));
    } else if (kind == 10) {
        renderer.FillRectangle(Rect{x + 1.0f, y + 3.0f, 14.0f, 11.0f}, color, 2.0f);
        const Color ink = ShadeColor(color, 0.28f);
        FillTriangle(renderer, x + 3.4f, y + 6.4f, x + 3.4f, y + 11.2f, x + 6.8f, y + 8.8f, ink);
        renderer.FillRectangle(Rect{x + 8.0f, y + 10.0f, 4.4f, 1.5f}, ink, 0.0f);
    } else if (kind == 11) {
        renderer.FillRectangle(Rect{x + 2.0f, y + 3.0f, 12.0f, 11.0f}, color, 1.6f);
        renderer.FillRectangle(Rect{x + 2.0f, y + 6.4f, 12.0f, 2.2f}, ShadeColor(color, 0.72f), 0.0f);
        renderer.FillRectangle(Rect{x + 6.4f, y + 5.5f, 3.2f, 4.0f}, LiftColor(color, 0.18f), 0.8f);
    } else if (kind == 12) {
        renderer.FillRectangle(Rect{x + 2.0f, y + 2.0f, 12.0f, 12.0f}, color, 2.0f);
        renderer.FillRectangle(Rect{x + 2.0f, y + 2.0f, 12.0f, 3.2f}, ShadeColor(color, 0.72f), 1.0f);
        renderer.FillRectangle(Rect{x + 4.6f, y + 7.6f, 6.8f, 1.4f}, ICON_PAPER, 0.0f);
    } else if (kind == 17) {
        renderer.FillRectangle(Rect{x + 2.5f, y + 1.5f, 4.0f, 4.0f}, color, 2.0f);
        renderer.FillRectangle(Rect{x + 2.5f, y + 10.5f, 4.0f, 4.0f}, color, 2.0f);
        renderer.FillRectangle(Rect{x + 9.5f, y + 6.0f, 4.0f, 4.0f}, color, 2.0f);
        renderer.FillRectangle(Rect{x + 4.2f, y + 5.2f, 1.6f, 6.2f}, color, 0.0f);
        renderer.FillRectangle(Rect{x + 4.2f, y + 7.2f, 6.2f, 1.6f}, color, 0.0f);
    } else if (kind == 20) {
        DrawBadgeGlyph(renderer, x, y, color, label, darkInk);
    } else {
        DrawPageGlyph(renderer, x, y, color);
    }
}

struct ShellIcon {
    std::string key;
    std::vector<unsigned char> pixels;
    int width = 0;
    int height = 0;
};

HICON LoadShellIconHandle(const std::wstring& probe, bool directory, bool open) {
    const DWORD attributes = directory ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
    SHFILEINFOW info{};
    DWORD flags = SHGFI_SYSICONINDEX | SHGFI_USEFILEATTRIBUTES;
    if (open) {
        flags |= SHGFI_OPENICON;
    }
    if (SHGetFileInfoW(probe.c_str(), attributes, &info, sizeof(info), flags) == 0) {
        return nullptr;
    }
    IImageList* images = nullptr;
    if (SUCCEEDED(SHGetImageList(SHIL_EXTRALARGE, IID_IImageList, reinterpret_cast<void**>(&images))) && images != nullptr) {
        HICON icon = nullptr;
        images->GetIcon(info.iIcon, ILD_TRANSPARENT, &icon);
        images->Release();
        if (icon != nullptr) {
            return icon;
        }
    }
    SHFILEINFOW fallback{};
    DWORD iconFlags = SHGFI_ICON | SHGFI_LARGEICON | SHGFI_USEFILEATTRIBUTES;
    if (open) {
        iconFlags |= SHGFI_OPENICON;
    }
    if (SHGetFileInfoW(probe.c_str(), attributes, &fallback, sizeof(fallback), iconFlags) == 0) {
        return nullptr;
    }
    return fallback.hIcon;
}

ShellIcon RasterizeIcon(HICON icon, const std::string& key) {
    ShellIcon result;
    result.key = key;
    if (icon == nullptr) {
        return result;
    }
    ICONINFO info{};
    if (!GetIconInfo(icon, &info)) {
        DestroyIcon(icon);
        return result;
    }
    BITMAP bitmap{};
    GetObject(info.hbmColor != nullptr ? info.hbmColor : info.hbmMask, sizeof(bitmap), &bitmap);
    const int width = bitmap.bmWidth;
    const int height = info.hbmColor != nullptr ? bitmap.bmHeight : bitmap.bmHeight / 2;
    if (info.hbmColor != nullptr) {
        DeleteObject(info.hbmColor);
    }
    if (info.hbmMask != nullptr) {
        DeleteObject(info.hbmMask);
    }
    if (width < 1 || height < 1) {
        DestroyIcon(icon);
        return result;
    }
    HDC device = CreateCompatibleDC(nullptr);
    BITMAPINFO header{};
    header.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    header.bmiHeader.biWidth = width;
    header.bmiHeader.biHeight = -height;
    header.bmiHeader.biPlanes = 1;
    header.bmiHeader.biBitCount = 32;
    header.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP dib = CreateDIBSection(device, &header, DIB_RGB_COLORS, &bits, nullptr, 0);
    HGDIOBJ previous = SelectObject(device, dib);
    DrawIconEx(device, 0, 0, icon, width, height, 0, nullptr, DI_NORMAL);
    result.width = width;
    result.height = height;
    result.pixels.resize(static_cast<std::size_t>(width * height * 4));
    if (bits != nullptr) {
        memcpy(result.pixels.data(), bits, result.pixels.size());
    }
    SelectObject(device, previous);
    DeleteObject(dib);
    DeleteDC(device);
    DestroyIcon(icon);
    bool hasAlpha = false;
    for (int index = 3; index < width * height * 4; index += 4) {
        if (result.pixels[static_cast<std::size_t>(index)] != 0) {
            hasAlpha = true;
            break;
        }
    }
    for (int index = 0; index < width * height; ++index) {
        unsigned char* pixel = result.pixels.data() + static_cast<std::size_t>(index) * 4;
        if (!hasAlpha) {
            pixel[3] = (pixel[0] != 0 || pixel[1] != 0 || pixel[2] != 0) ? 255 : 0;
        }
        const float alpha = static_cast<float>(pixel[3]) / 255.0f;
        pixel[0] = static_cast<unsigned char>(pixel[0] * alpha);
        pixel[1] = static_cast<unsigned char>(pixel[1] * alpha);
        pixel[2] = static_cast<unsigned char>(pixel[2] * alpha);
    }
    return result;
}

const ShellIcon* ShellIconFor(bool directory, bool open, const std::filesystem::path& path) {
    static bool comReady = false;
    if (!comReady) {
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        comReady = true;
    }
    static std::unordered_map<std::string, ShellIcon> cache;
    std::string key = directory ? (open ? "dir-open" : "dir") : "file";
    if (!directory) {
        std::string extension = LowerCopy(path.extension().string());
        if (extension.empty()) {
            extension = LowerCopy(path.filename().string());
        }
        key += extension.empty() ? ":plain" : extension;
    }
    const auto found = cache.find(key);
    if (found != cache.end()) {
        return &found->second;
    }
    const std::wstring probe = directory ? L"C:\\folder" : L"C:\\item" + ToWide(path.extension().empty() ? path.filename().string() : path.extension().string());
    ShellIcon icon = RasterizeIcon(LoadShellIconHandle(probe, directory, open), key);
    const auto inserted = cache.emplace(key, std::move(icon));
    return &inserted.first->second;
}

std::filesystem::path PickProjectFolder(HWND owner) {
    Microsoft::WRL::ComPtr<IFileOpenDialog> dialog;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(dialog.GetAddressOf())))) {
        return {};
    }
    DWORD options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
    dialog->SetTitle(L"Open Project");
    if (FAILED(dialog->Show(owner))) {
        return {};
    }
    Microsoft::WRL::ComPtr<IShellItem> item;
    if (FAILED(dialog->GetResult(item.GetAddressOf()))) {
        return {};
    }
    PWSTR text = nullptr;
    if (FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &text)) || text == nullptr) {
        return {};
    }
    const std::wstring wide(text);
    CoTaskMemFree(text);
    return std::filesystem::path(wide);
}

std::filesystem::path PickXmlFile(HWND owner, bool save, const std::wstring& suggestedName) {
    const COMDLG_FILTERSPEC filters[] = {{L"XML files", L"*.xml"}, {L"All files", L"*.*"}};
    Microsoft::WRL::ComPtr<IShellItem> item;
    if (save) {
        Microsoft::WRL::ComPtr<IFileSaveDialog> dialog;
        if (FAILED(CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(dialog.GetAddressOf())))) {
            return {};
        }
        dialog->SetFileTypes(2, filters);
        dialog->SetDefaultExtension(L"xml");
        dialog->SetTitle(L"Save As");
        if (!suggestedName.empty()) {
            dialog->SetFileName(suggestedName.c_str());
        }
        if (FAILED(dialog->Show(owner)) || FAILED(dialog->GetResult(item.GetAddressOf()))) {
            return {};
        }
    } else {
        Microsoft::WRL::ComPtr<IFileOpenDialog> dialog;
        if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(dialog.GetAddressOf())))) {
            return {};
        }
        DWORD options = 0;
        dialog->GetOptions(&options);
        dialog->SetOptions(options | FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST);
        dialog->SetFileTypes(2, filters);
        dialog->SetTitle(L"Open File");
        if (FAILED(dialog->Show(owner)) || FAILED(dialog->GetResult(item.GetAddressOf()))) {
            return {};
        }
    }
    PWSTR text = nullptr;
    if (item == nullptr || FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &text)) || text == nullptr) {
        return {};
    }
    const std::wstring wide(text);
    CoTaskMemFree(text);
    return std::filesystem::path(wide);
}

void LaunchRuntime(HWND owner, const std::filesystem::path& xmlPath, const char* device) {
    wchar_t modulePath[MAX_PATH] = {};
    if (GetModuleFileNameW(nullptr, modulePath, MAX_PATH) == 0) {
        return;
    }
    const std::filesystem::path runner = std::filesystem::path(modulePath).parent_path() / L"myui.exe";
    if (!std::filesystem::exists(runner)) {
        MessageBoxW(owner, L"myui.exe was not found next to the designer. Build the myui target, then run again.", L"RoboUIDesigner",
                    MB_OK | MB_ICONWARNING);
        return;
    }
    std::wstring command = L"\"";
    command += runner.wstring();
    command += L"\" run \"";
    command += xmlPath.wstring();
    command += L"\" ";
    command += ToWide(device);
    std::vector<wchar_t> buffer(command.begin(), command.end());
    buffer.push_back(L'\0');
    const std::wstring directory = xmlPath.parent_path().wstring();
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(nullptr, buffer.data(), nullptr, nullptr, FALSE, 0, nullptr, directory.empty() ? nullptr : directory.c_str(), &startup, &process)) {
        MessageBoxW(owner, L"The app could not be started.", L"RoboUIDesigner", MB_OK | MB_ICONWARNING);
        return;
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
}

void LaunchDesignerWindow() {
    wchar_t modulePath[MAX_PATH] = {};
    if (GetModuleFileNameW(nullptr, modulePath, MAX_PATH) == 0) {
        return;
    }
    std::wstring command = L"\"";
    command += modulePath;
    command += L"\"";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &process)) {
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
    }
}

void DrawShellIcon(Renderer& renderer, float x, float y, bool directory, bool open, const std::filesystem::path& path) {
    const ShellIcon* icon = ShellIconFor(directory, open, path);
    if (icon != nullptr && icon->width > 0 && !icon->pixels.empty()) {
        renderer.DrawCachedImage(icon->key, Rect{x, y, 16.0f, 16.0f}, icon->pixels.data(), icon->width, icon->height);
        return;
    }
    DrawExplorerIcon(renderer, x, y, directory ? (open ? 16 : 0) : 1, directory ? ICON_FOLDER : ICON_GRAY);
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

    float deviceWidth = PixelAttribute(PrimaryWindow(document), "width", static_cast<float>(DEFAULT_WINDOW_WIDTH));
    float deviceHeight = PixelAttribute(PrimaryWindow(document), "height", static_cast<float>(DEFAULT_WINDOW_HEIGHT));
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
    std::string libraryQuery;
    Rect librarySearchRect{};
    int libraryHover = -1;
    IconPack iconPack;
    bool iconPackLoaded = false;
    std::vector<int> assetFilter;
    std::string assetFilterQuery;
    bool assetFilterReady = false;
    int assetScroll = 0;
    std::string assetQuery;
    Rect assetSearchRect{};
    int assetHover = -1;
    int assetSelected = -1;
    Rect assetPlace{};
    Rect assetPackIcons{};
    Rect assetPackFlags{};
    std::vector<HitRow> assetRows;
    IconPack countryPack;
    bool countryPackLoaded = false;
    int assetPack = 0;
    struct VariableHit {
        Rect rect{};
        std::string group;
        std::string name;
        std::string value;
        std::string note;
        bool header = false;
        bool builtin = false;
    };
    std::vector<VariableHit> variableRows;
    std::string variableQuery;
    std::string variableDraft;
    std::string variableSelected;
    int variableScroll = 0;
    Rect variableSearchRect{};
    Rect variableAddRect{};
    Rect variableValueRect{};
    Rect variableRemoveRect{};
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
    unsigned lastCanvasTick = 0;
    std::vector<int> lastCanvasPath;
    std::vector<int> editScope;
    Rect scopeDoneRect{};
    Rect scopeMainRect{};
    Rect scopeApplyRect{};
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
        const CompileResult compiled = CompileUi(SourceFile{document.FilePath(), document.PreviewText()});
        compileDiagnostics = compiled.diagnostics;
        if (!compiled.Ok()) {
            return;
        }
        preview = Instantiate(*compiled.root, {});
        if (preview != nullptr && (preview->Kind() == NodeKind::WINDOW || preview->Kind() == NodeKind::BOARD)) {
            auto host = std::make_unique<Frame>();
            host->AddChild(std::move(preview));
            preview = std::move(host);
        }
    };
    rebuildPreview();
    if (document.Root() != nullptr && document.Root()->name == "component") {
        for (int index = 0; index < static_cast<int>(document.Root()->children.size()); ++index) {
            const std::string& childName = document.Root()->children[static_cast<std::size_t>(index)].name;
            if (childName == "property" || childName == "event") {
                continue;
            }
            selection = {index};
            selections = {selection};
            break;
        }
    }

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
    std::filesystem::path projectRoot = options.folder;
    std::vector<std::string> openFolders;
    if (!projectRoot.empty()) {
        openFolders.push_back(projectRoot.generic_string());
    }
    Rect openProjectRect{};
    bool openProjectHover = false;
    std::vector<HitRow> menuTitles;
    Rect runButton{};
    bool runHover = false;
    int openMenu = -1;
    int menuTitleHover = -1;
    bool appMenuOpen = false;
    int appMenuHover = -1;
    std::vector<HitRow> appMenuRows;
    std::vector<FileEntry> fileEntries;
    std::string selectedFile;
    std::vector<std::string> selectedFiles;
    std::string fileAnchor;
    bool explorerActive = false;
    bool fileCreatePending = false;
    bool fileCreateFolder = false;
    std::string fileCreateParent;
    int fileHover = -1;
    int fileActionHover = 0;
    int fileScroll = 0;
    bool fileMenuOpen = false;
    Point fileMenuPoint{};
    std::string fileMenuTarget;
    bool fileMenuDirectory = false;
    int fileMenuHover = -1;
    std::vector<HitRow> fileMenuRows;
    std::vector<WorkTab> workTabs;
    int activeTab = -1;
    Rect tabBar{};
    std::vector<HitRow> tabRows;
    int tabHover = -1;
    int tabCloseHover = -1;
    bool editorFocused = false;
    {
        WorkTab opened;
        opened.path = document.FilePath();
        opened.designer = true;
        opened.editorText = document.Text();
        opened.selection = selection;
        opened.selections = selections;
        opened.deviceWidth = deviceWidth;
        opened.deviceHeight = deviceHeight;
        opened.deviceIndex = deviceIndex;
        workTabs.push_back(std::move(opened));
        activeTab = 0;
    }
    bool fileRenaming = false;
    bool renameReplace = false;
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
        if (projectRoot.empty()) {
            filesDirty = false;
            return;
        }
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
        if (fileCreatePending) {
            const std::filesystem::path parent(fileCreateParent);
            int parentIndex = -1;
            int parentDepth = -1;
            if (parent != projectRoot) {
                for (int index = 0; index < static_cast<int>(fileEntries.size()); ++index) {
                    if (fileEntries[static_cast<std::size_t>(index)].path == parent) {
                        parentIndex = index;
                        parentDepth = fileEntries[static_cast<std::size_t>(index)].depth;
                        break;
                    }
                }
            }
            FileEntry ghost;
            ghost.name = renameDraft.empty() ? (fileCreateFolder ? "New folder" : "Untitled.xml") : renameDraft;
            ghost.path = parent / ghost.name;
            ghost.directory = fileCreateFolder;
            ghost.depth = parentDepth + 1;
            ghost.pending = true;
            const int insertAt = parentIndex + 1;
            if (insertAt >= 0 && insertAt <= static_cast<int>(fileEntries.size())) {
                fileEntries.insert(fileEntries.begin() + insertAt, ghost);
            } else {
                fileEntries.insert(fileEntries.begin(), ghost);
            }
            selectedFile = ghost.path.generic_string();
            selectedFiles = {selectedFile};
        }
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
    auto selectExplorerOnly = [&](const std::string& key) {
        selectedFiles.clear();
        if (!key.empty()) {
            selectedFiles.push_back(key);
        }
        selectedFile = key;
        fileAnchor = key;
    };
    auto explorerSelected = [&](const std::string& key) {
        return std::find(selectedFiles.begin(), selectedFiles.end(), key) != selectedFiles.end();
    };
    auto revealExplorer = [&](const std::string& key) {
        int index = -1;
        for (int row = 0; row < static_cast<int>(fileEntries.size()); ++row) {
            if (fileEntries[static_cast<std::size_t>(row)].path.generic_string() == key || fileEntries[static_cast<std::size_t>(row)].pending) {
                index = row;
                if (fileEntries[static_cast<std::size_t>(row)].path.generic_string() == key) {
                    break;
                }
            }
        }
        if (index < 0) {
            return;
        }
        const int visibleRows = std::max(1, static_cast<int>(std::max(0.0f, fullSidebar.height - 76.0f) / 28.0f));
        if (index < fileScroll) {
            fileScroll = index;
        } else if (index >= fileScroll + visibleRows) {
            fileScroll = index - visibleRows + 1;
        }
    };
    auto cancelExplorerCreate = [&]() {
        fileCreatePending = false;
        fileRenaming = false;
        renameReplace = false;
        renameDraft.clear();
        if (!selectedFiles.empty() && selectedFile == selectedFiles.front()) {
            selectExplorerOnly("");
        }
        filesDirty = true;
    };
    auto removeExplorerPaths = [&](const std::vector<std::string>& keys) {
        std::vector<std::string> ordered = keys;
        std::sort(ordered.begin(), ordered.end(), [](const std::string& left, const std::string& right) { return left.size() > right.size(); });
        for (const std::string& key : ordered) {
            const std::filesystem::path target(key);
            if (target == projectRoot || key.empty()) {
                continue;
            }
            std::error_code error;
            if (std::filesystem::is_directory(target, error)) {
                std::filesystem::remove_all(target, error);
            } else {
                std::filesystem::remove(target, error);
            }
            openFolders.erase(std::remove_if(openFolders.begin(), openFolders.end(),
                                             [&](const std::string& folder) {
                                                 return folder == key || (folder.size() > key.size() && folder.compare(0, key.size(), key) == 0 && folder[key.size()] == '/');
                                             }),
                              openFolders.end());
            selectedFiles.erase(std::remove_if(selectedFiles.begin(), selectedFiles.end(),
                                               [&](const std::string& item) {
                                                   return item == key || (item.size() > key.size() && item.compare(0, key.size(), key) == 0 && item[key.size()] == '/');
                                               }),
                                selectedFiles.end());
            if (selectedFile == key || (selectedFile.size() > key.size() && selectedFile.compare(0, key.size(), key) == 0 && selectedFile[key.size()] == '/')) {
                selectedFile = selectedFiles.empty() ? std::string{} : selectedFiles.back();
            }
            if (fileAnchor == key || (fileAnchor.size() > key.size() && fileAnchor.compare(0, key.size(), key) == 0 && fileAnchor[key.size()] == '/')) {
                fileAnchor = selectedFile;
            }
        }
        filesDirty = true;
    };
    auto closeFileMenu = [&]() {
        fileMenuOpen = false;
        fileMenuHover = -1;
        fileMenuRows.clear();
    };
    auto commitFileRename = [&]() {
        if (!fileRenaming) {
            return;
        }
        if (renameDraft.empty() || renameDraft == "." || renameDraft == ".." || renameDraft.find_first_of("\\/:*?\"<>|") != std::string::npos) {
            if (fileCreatePending) {
                cancelExplorerCreate();
            } else {
                fileRenaming = false;
                renameReplace = false;
            }
            return;
        }
        if (fileCreatePending) {
            const std::filesystem::path destination = std::filesystem::path(fileCreateParent) / renameDraft;
            std::error_code error;
            if (std::filesystem::exists(destination, error)) {
                return;
            }
            if (fileCreateFolder) {
                std::filesystem::create_directory(destination, error);
            } else {
                std::ofstream output(destination);
                if (!output) {
                    return;
                }
                output << "<window width=\"800\" height=\"600\" title=\"Untitled\">\n"
                       << "    <frame name=\"Board 1\" position=\"absolute\" x=\"0\" y=\"0\" width=\"800\" height=\"600\" background=\"#FFFFFF\"/>\n"
                       << "</window>\n";
            }
            if (error) {
                return;
            }
            fileCreatePending = false;
            fileRenaming = false;
            renameReplace = false;
            selectExplorerOnly(destination.generic_string());
            filesDirty = true;
            return;
        }
        const std::filesystem::path source(selectedFile);
        const std::filesystem::path destination = source.parent_path() / renameDraft;
        std::error_code error;
        if (destination != source) {
            if (std::filesystem::exists(destination, error)) {
                return;
            }
            std::filesystem::rename(source, destination, error);
            if (error) {
                return;
            }
            const std::string from = source.generic_string();
            const std::string to = destination.generic_string();
            for (std::string& folder : openFolders) {
                if (folder == from || (folder.size() > from.size() && folder.compare(0, from.size(), from) == 0 && folder[from.size()] == '/')) {
                    folder.replace(0, from.size(), to);
                }
            }
            for (std::string& item : selectedFiles) {
                if (item == from || (item.size() > from.size() && item.compare(0, from.size(), from) == 0 && item[from.size()] == '/')) {
                    item.replace(0, from.size(), to);
                }
            }
            if (fileAnchor == from || (fileAnchor.size() > from.size() && fileAnchor.compare(0, from.size(), from) == 0 && fileAnchor[from.size()] == '/')) {
                fileAnchor.replace(0, from.size(), to);
            }
            selectedFile = to;
            filesDirty = true;
        }
        fileRenaming = false;
        renameReplace = false;
    };
    auto beginExplorerCreate = [&](bool folder) {
        if (projectRoot.empty()) {
            return;
        }
        if (fileRenaming) {
            commitFileRename();
        }
        if (fileCreatePending) {
            cancelExplorerCreate();
        }
        const std::filesystem::path destination = fileDestination();
        const std::string folderKey = destination.generic_string();
        if (std::find(openFolders.begin(), openFolders.end(), folderKey) == openFolders.end()) {
            openFolders.push_back(folderKey);
        }
        const std::filesystem::path suggested = UniqueChild(destination, folder ? "New folder" : "Untitled", folder ? "" : ".xml");
        fileCreatePending = true;
        fileCreateFolder = folder;
        fileCreateParent = folderKey;
        renameDraft = suggested.filename().string();
        renameOriginal = renameDraft;
        renameReplace = true;
        fileRenaming = true;
        explorerActive = true;
        selectExplorerOnly(suggested.generic_string());
        filesDirty = true;
    };
    auto surfaceDesigner = [&]() {
        return activeTab >= 0 && activeTab < static_cast<int>(workTabs.size()) && workTabs[static_cast<std::size_t>(activeTab)].designer;
    };
    auto captureTab = [&]() {
        if (!surfaceDesigner()) {
            return;
        }
        WorkTab& tab = workTabs[static_cast<std::size_t>(activeTab)];
        tab.editorText = document.Text();
        tab.selection = selection;
        tab.selections = selections;
        tab.undoStack = undoStack;
        tab.redoStack = redoStack;
        tab.zoom = zoom;
        tab.panX = panX;
        tab.panY = panY;
        tab.fitted = fitted;
        tab.deviceWidth = deviceWidth;
        tab.deviceHeight = deviceHeight;
        tab.deviceIndex = deviceIndex;
        tab.path = document.FilePath();
    };
    auto showTab = [&](int index, bool capture) {
        if (capture) {
            captureTab();
        }
        if (index < 0 || index >= static_cast<int>(workTabs.size())) {
            activeTab = -1;
            editorFocused = false;
            return;
        }
        activeTab = index;
        WorkTab& tab = workTabs[static_cast<std::size_t>(index)];
        editorFocused = !tab.designer;
        editTarget = EditTarget::NONE;
        drag = DragKind::NONE;
        if (!tab.designer) {
            return;
        }
        document.LoadText(tab.editorText, tab.path);
        selection = tab.selection;
        selections = tab.selections;
        undoStack = tab.undoStack;
        redoStack = tab.redoStack;
        zoom = tab.zoom;
        panX = tab.panX;
        panY = tab.panY;
        fitted = tab.fitted;
        deviceWidth = tab.deviceWidth;
        deviceHeight = tab.deviceHeight;
        deviceIndex = tab.deviceIndex;
        historyOpen = false;
        rebuildPreview();
    };
    auto openWorkFile = [&](const std::filesystem::path& path, bool asDesigner) {
        if (path.empty()) {
            return;
        }
        const std::string key = path.generic_string();
        for (int index = 0; index < static_cast<int>(workTabs.size()); ++index) {
            const WorkTab& tab = workTabs[static_cast<std::size_t>(index)];
            if (tab.path == key && tab.designer == asDesigner) {
                showTab(index, true);
                return;
            }
        }
        if (asDesigner) {
            captureTab();
            if (!document.LoadFile(path)) {
                return;
            }
            selection.clear();
            selections.clear();
            undoStack.clear();
            redoStack.clear();
            zoom = 1.0f;
            panX = 0.0f;
            panY = 0.0f;
            fitted = false;
            historyOpen = false;
            deviceWidth = PixelAttribute(PrimaryWindow(document), "width", static_cast<float>(DEFAULT_WINDOW_WIDTH));
            deviceHeight = PixelAttribute(PrimaryWindow(document), "height", static_cast<float>(DEFAULT_WINDOW_HEIGHT));
            if (document.Root() != nullptr && document.Root()->name == "component") {
                for (int index = 0; index < static_cast<int>(document.Root()->children.size()); ++index) {
                    const std::string& childName = document.Root()->children[static_cast<std::size_t>(index)].name;
                    if (childName == "property" || childName == "event") {
                        continue;
                    }
                    selection = {index};
                    selections = {selection};
                    break;
                }
            }
            rebuildPreview();
            WorkTab tab;
            tab.path = document.FilePath();
            tab.designer = true;
            tab.editorText = document.Text();
            tab.selection = selection;
            tab.selections = selections;
            tab.zoom = zoom;
            tab.fitted = fitted;
            tab.deviceWidth = deviceWidth;
            tab.deviceHeight = deviceHeight;
            tab.deviceIndex = deviceIndex;
            workTabs.push_back(std::move(tab));
            activeTab = static_cast<int>(workTabs.size()) - 1;
            editorFocused = false;
            return;
        }
        captureTab();
        WorkTab tab;
        tab.path = key;
        tab.designer = false;
        std::ifstream input(path, std::ios::binary);
        if (input) {
            tab.editorText.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
            tab.binary = tab.editorText.find('\0') != std::string::npos;
            if (tab.binary) {
                tab.editorText.clear();
            }
        }
        workTabs.push_back(std::move(tab));
        activeTab = static_cast<int>(workTabs.size()) - 1;
        editorFocused = true;
        editTarget = EditTarget::NONE;
    };
    auto closeTab = [&](int index) {
        if (index < 0 || index >= static_cast<int>(workTabs.size())) {
            return;
        }
        if (index == activeTab) {
            workTabs.erase(workTabs.begin() + index);
            activeTab = -1;
            if (workTabs.empty()) {
                editorFocused = false;
                return;
            }
            const int next = std::min(index, static_cast<int>(workTabs.size()) - 1);
            showTab(next, false);
            return;
        }
        workTabs.erase(workTabs.begin() + index);
        if (activeTab > index) {
            --activeTab;
        }
    };
    auto saveActive = [&]() {
        if (activeTab < 0 || activeTab >= static_cast<int>(workTabs.size())) {
            return;
        }
        if (surfaceDesigner()) {
            if (document.Save()) {
                LogInfo("Saved " + document.FilePath());
                workTabs[static_cast<std::size_t>(activeTab)].editorText = document.Text();
                workTabs[static_cast<std::size_t>(activeTab)].dirty = false;
            }
            return;
        }
        WorkTab& tab = workTabs[static_cast<std::size_t>(activeTab)];
        if (tab.binary || tab.path.empty()) {
            return;
        }
        std::ofstream output(std::filesystem::path(tab.path), std::ios::binary);
        if (!output) {
            return;
        }
        output << tab.editorText;
        if (output) {
            tab.dirty = false;
            LogInfo("Saved " + tab.path);
        }
    };
    auto placeEditorCaret = [&](Point point) {
        if (activeTab < 0 || surfaceDesigner()) {
            return;
        }
        WorkTab& tab = workTabs[static_cast<std::size_t>(activeTab)];
        if (tab.binary) {
            return;
        }
        const int line = std::max(0, static_cast<int>((point.y - canvas.y - 8.0f) / 18.0f) + tab.scroll);
        int current = 0;
        std::size_t start = 0;
        for (std::size_t index = 0; index < tab.editorText.size(); ++index) {
            if (tab.editorText[index] != '\n') {
                continue;
            }
            if (current == line) {
                break;
            }
            ++current;
            start = index + 1;
        }
        if (current < line) {
            tab.caret = tab.editorText.size();
            return;
        }
        const int column = std::max(0, static_cast<int>((point.x - canvas.x - 56.0f) / 7.2f));
        std::size_t caret = start;
        int seen = 0;
        while (caret < tab.editorText.size() && tab.editorText[caret] != '\n' && seen < column) {
            if ((static_cast<unsigned char>(tab.editorText[caret]) & 0xC0) != 0x80) {
                ++seen;
            }
            ++caret;
        }
        tab.caret = caret;
    };
    auto applyFileAction = [&](int action) {
        const std::filesystem::path target = fileMenuTarget.empty() ? projectRoot : std::filesystem::path(fileMenuTarget);
        const bool targetSelected = explorerSelected(target.generic_string());
        closeFileMenu();
        if (action == 0) {
            openWorkFile(target, true);
        } else if (action == 7) {
            openWorkFile(target, false);
        } else if (action == 1 || action == 2) {
            selectExplorerOnly(target.generic_string());
            beginExplorerCreate(action == 2);
        } else if (action == 3) {
            selectExplorerOnly(target.generic_string());
            renameOriginal = target.filename().string();
            renameDraft = renameOriginal;
            renameReplace = true;
            fileRenaming = true;
            explorerActive = true;
        } else if (action == 4) {
            if (targetSelected && selectedFiles.size() > 1) {
                removeExplorerPaths(selectedFiles);
            } else if (target != projectRoot) {
                removeExplorerPaths({target.generic_string()});
            }
        } else if (action == 5) {
            if (targetSelected && selectedFiles.size() > 1) {
                std::string joined;
                for (const std::string& item : selectedFiles) {
                    if (!joined.empty()) {
                        joined += "\n";
                    }
                    joined += std::filesystem::path(item).string();
                }
                CopyText(joined);
            } else {
                CopyText(target.string());
            }
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
        library = Rect{fullSidebar.x, fullSidebar.y, fullSidebar.width, leftNav == 2 ? fullSidebar.height : 0.0f};
        xmlPanel = Rect{0.0f, height - XML_HEIGHT, width, XML_HEIGHT};
        inspector = Rect{width - RIGHT_WIDTH, TOOLBAR_HEIGHT, RIGHT_WIDTH, std::max(0.0f, height - TOOLBAR_HEIGHT - XML_HEIGHT)};
        tabBar = Rect{LEFT_WIDTH, TOOLBAR_HEIGHT, std::max(0.0f, width - LEFT_WIDTH - RIGHT_WIDTH), TAB_HEIGHT};
        canvas = Rect{LEFT_WIDTH, TOOLBAR_HEIGHT + TAB_HEIGHT, std::max(0.0f, width - LEFT_WIDTH - RIGHT_WIDTH),
                      std::max(0.0f, height - TOOLBAR_HEIGHT - TAB_HEIGHT - XML_HEIGHT)};
        tabRows.clear();
        if (!workTabs.empty() && tabBar.width > 8.0f) {
            const float available = std::max(0.0f, tabBar.width - 8.0f);
            const float tabWidth = std::min(188.0f, std::max(64.0f, available / static_cast<float>(workTabs.size())));
            float tabX = tabBar.x + 4.0f;
            for (int index = 0; index < static_cast<int>(workTabs.size()); ++index) {
                const WorkTab& tab = workTabs[static_cast<std::size_t>(index)];
                HitRow row;
                row.depth = index;
                row.branch = tab.designer;
                row.name = std::filesystem::path(tab.path).filename().string();
                if (row.name.empty()) {
                    row.name = "Untitled";
                }
                bool paired = false;
                for (const WorkTab& other : workTabs) {
                    if (other.designer == tab.designer) {
                        continue;
                    }
                    if (std::filesystem::path(other.path).filename() == std::filesystem::path(tab.path).filename()) {
                        paired = true;
                        break;
                    }
                }
                if (paired) {
                    row.name += tab.designer ? "  Design" : "  Edit";
                }
                row.rect = Rect{tabX, tabBar.y + 4.0f, std::max(0.0f, tabWidth - 4.0f), TAB_HEIGHT - 8.0f};
                row.twist = Rect{row.rect.x + row.rect.width - 18.0f, row.rect.y + (row.rect.height - 12.0f) * 0.5f, 12.0f, 12.0f};
                tabRows.push_back(std::move(row));
                tabX += tabWidth;
            }
        }
        libraryRows.clear();
        librarySearchRect = {};
        if (leftNav == 2 && fullSidebar.width > 1.0f) {
            librarySearchRect = Rect{fullSidebar.x + 12.0f, fullSidebar.y + 36.0f, std::max(0.0f, fullSidebar.width - 24.0f), 32.0f};
            struct LibraryItem {
                const char* category;
                const char* name;
            };
            const std::vector<LibraryItem> items = {{"Window", "window"}, {"Board", "board"}, {"Frame", "frame"}, {"Scrollbar", "scrollbar"}};
            const std::string query = LowerCopy(libraryQuery);
            const float contentWidth = std::max(0.0f, fullSidebar.width - 24.0f);
            const float gap = 8.0f;
            const float cardWidth = std::max(72.0f, (contentWidth - gap) * 0.5f);
            const float cardHeight = 92.0f;
            const float listTop = fullSidebar.y + 80.0f;
            const float viewHeight = std::max(0.0f, fullSidebar.height - 80.0f);
            std::vector<HitRow> staged;
            float cursor = 0.0f;
            std::string category;
            int column = 0;
            for (const LibraryItem& item : items) {
                if (!query.empty() && LowerCopy(item.name).find(query) == std::string::npos) {
                    continue;
                }
                if (category != item.category) {
                    if (column != 0) {
                        cursor += cardHeight + gap;
                        column = 0;
                    }
                    HitRow header;
                    header.name = item.category;
                    header.depth = -1;
                    header.rect = Rect{fullSidebar.x + 12.0f, cursor, contentWidth, 22.0f};
                    staged.push_back(std::move(header));
                    category = item.category;
                    cursor += 26.0f;
                }
                HitRow card;
                card.name = item.name;
                card.depth = 0;
                card.rect = Rect{fullSidebar.x + 12.0f + static_cast<float>(column) * (cardWidth + gap), cursor, cardWidth, cardHeight};
                staged.push_back(std::move(card));
                column = column == 0 ? 1 : 0;
                if (column == 0) {
                    cursor += cardHeight + gap;
                }
            }
            if (column != 0) {
                cursor += cardHeight + gap;
            }
            const int maxScroll = static_cast<int>(std::max(0.0f, cursor - viewHeight));
            libraryScroll = std::max(0, std::min(libraryScroll, maxScroll));
            for (HitRow& row : staged) {
                row.rect.y += listTop - static_cast<float>(libraryScroll);
                libraryRows.push_back(std::move(row));
            }
        }
        assetRows.clear();
        assetSearchRect = {};
        assetPackIcons = {};
        assetPackFlags = {};
        if (leftNav == 3 && fullSidebar.width > 1.0f) {
            if (!iconPackLoaded) {
                iconPackLoaded = true;
                iconPack.Load(std::filesystem::path(RUNTIME_SOURCE_DIR) / "assets" / "RoboIcons.pack");
            }
            if (!countryPackLoaded) {
                countryPackLoaded = true;
                countryPack.Load(std::filesystem::path(RUNTIME_SOURCE_DIR) / "assets" / "RoboCountry.pack");
            }
            const float packWidth = std::max(0.0f, (fullSidebar.width - 32.0f) * 0.5f);
            assetPackIcons = Rect{fullSidebar.x + 12.0f, fullSidebar.y + 34.0f, packWidth, 26.0f};
            assetPackFlags = Rect{assetPackIcons.x + packWidth + 8.0f, assetPackIcons.y, packWidth, 26.0f};
            assetSearchRect = Rect{fullSidebar.x + 12.0f, fullSidebar.y + 68.0f, std::max(0.0f, fullSidebar.width - 24.0f), 32.0f};
            IconPack& activePack = assetPack == 1 ? countryPack : iconPack;
            const std::string filterKey = (assetPack == 1 ? "flags:" : "icons:") + assetQuery;
            if (!assetFilterReady || assetFilterQuery != filterKey) {
                assetFilter.clear();
                const std::string query = LowerCopy(assetQuery);
                for (int index = 0; index < activePack.Count(); ++index) {
                    const std::string& name = activePack.Name(index);
                    const char* title = assetPack == 1 ? CountryTitle(name) : nullptr;
                    const bool named = title != nullptr && LowerCopy(title).find(query) != std::string::npos;
                    if (query.empty() || LowerCopy(name).find(query) != std::string::npos || named) {
                        assetFilter.push_back(index);
                    }
                }
                assetFilterQuery = filterKey;
                assetFilterReady = true;
                assetScroll = 0;
            }
            const float contentWidth = std::max(0.0f, fullSidebar.width - 24.0f);
            const float gap = 8.0f;
            const float cardWidth = std::max(72.0f, (contentWidth - gap) * 0.5f);
            const float cardHeight = 92.0f;
            const float listTop = fullSidebar.y + 112.0f;
            const float previewHeight = assetSelected >= 0 ? 96.0f : 0.0f;
            assetPlace = {};
            if (assetSelected >= 0) {
                const Rect previewBar{fullSidebar.x + 8.0f, fullSidebar.y + fullSidebar.height - 88.0f, std::max(0.0f, fullSidebar.width - 16.0f), 80.0f};
                assetPlace = Rect{previewBar.x + previewBar.width - 72.0f, previewBar.y + 28.0f, 56.0f, 26.0f};
            }
            const float viewHeight = std::max(0.0f, fullSidebar.height - 112.0f - previewHeight);
            const int columns = 2;
            const int rowCount = assetFilter.empty() ? 0 : (static_cast<int>(assetFilter.size()) + columns - 1) / columns;
            const int contentHeight = rowCount * static_cast<int>(cardHeight + gap);
            assetScroll = std::max(0, std::min(assetScroll, std::max(0, contentHeight - static_cast<int>(viewHeight))));
            const int firstRow = static_cast<int>(static_cast<float>(assetScroll) / (cardHeight + gap));
            const int lastRow = std::min(rowCount, firstRow + static_cast<int>(viewHeight / (cardHeight + gap)) + 2);
            for (int rowIndex = firstRow; rowIndex < lastRow; ++rowIndex) {
                for (int column = 0; column < columns; ++column) {
                    const int filterIndex = rowIndex * columns + column;
                    if (filterIndex >= static_cast<int>(assetFilter.size())) {
                        break;
                    }
                    HitRow card;
                    card.depth = assetFilter[static_cast<std::size_t>(filterIndex)];
                    card.name = activePack.Name(card.depth);
                    const float y = listTop + static_cast<float>(rowIndex) * (cardHeight + gap) - static_cast<float>(assetScroll);
                    card.rect = Rect{fullSidebar.x + 12.0f + static_cast<float>(column) * (cardWidth + gap), y, cardWidth, cardHeight};
                    assetRows.push_back(std::move(card));
                }
            }
        }
        variableRows.clear();
        variableSearchRect = {};
        variableAddRect = {};
        variableValueRect = {};
        variableRemoveRect = {};
        if (leftNav == 4 && fullSidebar.width > 1.0f) {
            struct StoredToken {
                std::string group;
                std::string name;
                std::string value;
                bool removed = false;
            };
            std::vector<StoredToken> stored;
            if (const XmlNode* root = document.Root()) {
                std::vector<const XmlNode*> tokenHosts;
                if (root->name == "screens") {
                    for (const XmlNode& child : root->children) {
                        tokenHosts.push_back(&child);
                    }
                } else {
                    tokenHosts.push_back(root);
                }
                for (const XmlNode* host : tokenHosts) {
                for (const XmlNode& child : host->children) {
                    if (child.name != "tokens") {
                        continue;
                    }
                    for (const XmlNode& token : child.children) {
                        StoredToken item;
                        item.group = token.name;
                        for (const XmlAttribute& attribute : token.attributes) {
                            if (attribute.name == "name") {
                                item.name = attribute.value;
                            } else if (attribute.name == "value") {
                                item.value = attribute.value;
                            } else if (attribute.name == "removed" && attribute.value == "true") {
                                item.removed = true;
                            }
                        }
                        if (!item.name.empty()) {
                            stored.push_back(std::move(item));
                        }
                    }
                }
                }
            }
            auto storedFor = [&](const std::string& group, const std::string& name) -> const StoredToken* {
                const StoredToken* found = nullptr;
                for (const StoredToken& item : stored) {
                    if (item.group == group && item.name == name) {
                        found = &item;
                    }
                }
                return found;
            };
            variableSearchRect = Rect{fullSidebar.x + 12.0f, fullSidebar.y + 36.0f, std::max(0.0f, fullSidebar.width - 24.0f), 32.0f};
            variableAddRect = Rect{fullSidebar.x + fullSidebar.width - 64.0f, fullSidebar.y + 8.0f, 52.0f, 22.0f};
            const std::string query = LowerCopy(variableQuery);
            int tokenCount = 0;
            const FoundationToken* tokens = FoundationTokens(tokenCount);
            std::vector<VariableHit> staged;
            FoundationGroup current = FoundationGroup::COLOR;
            bool opened = false;
            for (int index = 0; index < tokenCount; ++index) {
                const FoundationToken& token = tokens[index];
                const std::string group = FoundationGroupElement(token.group);
                const StoredToken* documentToken = storedFor(group, token.name);
                if (documentToken != nullptr && documentToken->removed) {
                    continue;
                }
                const std::string value = documentToken != nullptr && !documentToken->value.empty() ? documentToken->value : token.value;
                if (!query.empty() && LowerCopy(token.name).find(query) == std::string::npos && LowerCopy(value).find(query) == std::string::npos) {
                    continue;
                }
                if (!opened || current != token.group) {
                    VariableHit header;
                    header.header = true;
                    header.name = FoundationGroupLabel(token.group);
                    staged.push_back(std::move(header));
                    current = token.group;
                    opened = true;
                }
                VariableHit row;
                row.group = group;
                row.name = token.name;
                row.value = value;
                row.note = token.note;
                row.builtin = true;
                staged.push_back(std::move(row));
            }
            bool customHeader = false;
            for (const StoredToken& item : stored) {
                if (item.removed || item.value.empty()) {
                    continue;
                }
                bool known = false;
                for (int index = 0; index < tokenCount; ++index) {
                    if (item.name == tokens[index].name && item.group == FoundationGroupElement(tokens[index].group)) {
                        known = true;
                        break;
                    }
                }
                if (known) {
                    continue;
                }
                if (!query.empty() && LowerCopy(item.name).find(query) == std::string::npos && LowerCopy(item.value).find(query) == std::string::npos) {
                    continue;
                }
                if (!customHeader) {
                    VariableHit header;
                    header.header = true;
                    header.name = "Custom";
                    staged.push_back(std::move(header));
                    customHeader = true;
                }
                VariableHit row;
                row.group = item.group;
                row.name = item.name;
                row.value = item.value;
                row.builtin = false;
                staged.push_back(std::move(row));
            }
            const bool editingValue = !variableSelected.empty();
            const float footer = editingValue ? 86.0f : 0.0f;
            const float listTop = fullSidebar.y + 80.0f;
            const float viewHeight = std::max(0.0f, fullSidebar.height - 80.0f - footer);
            const float rowHeight = 32.0f;
            const int contentHeight = static_cast<int>(staged.size()) * static_cast<int>(rowHeight);
            variableScroll = std::max(0, std::min(variableScroll, std::max(0, contentHeight - static_cast<int>(viewHeight))));
            for (std::size_t index = 0; index < staged.size(); ++index) {
                VariableHit row = std::move(staged[index]);
                const float y = listTop + static_cast<float>(index) * rowHeight - static_cast<float>(variableScroll);
                row.rect = Rect{fullSidebar.x + 8.0f, y, std::max(0.0f, fullSidebar.width - 16.0f), rowHeight - 2.0f};
                if (editingValue && !row.header && row.name == variableSelected) {
                    const Rect bar{fullSidebar.x + 8.0f, fullSidebar.y + fullSidebar.height - 78.0f, std::max(0.0f, fullSidebar.width - 16.0f), 70.0f};
                    variableValueRect = Rect{bar.x + 8.0f, bar.y + 28.0f, std::max(0.0f, bar.width - 84.0f), 28.0f};
                    variableRemoveRect = Rect{bar.x + bar.width - 68.0f, bar.y + 28.0f, 60.0f, 28.0f};
                }
                variableRows.push_back(std::move(row));
            }
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
            if (root->name == "screens") {
                int childIndex = 0;
                for (const XmlNode& child : root->children) {
                    walk(walk, child, {childIndex}, 0);
                    ++childIndex;
                }
            } else {
                walk(walk, *root, {}, 0);
            }
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
        if (projectRoot.empty()) {
            refreshRect = {};
            folderNewRect = {};
            fileNewRect = {};
            const float buttonWidth = std::min(188.0f, std::max(140.0f, fullSidebar.width - 40.0f));
            const float top = std::max(fullSidebar.y + 28.0f, fullSidebar.y + (fullSidebar.height - 150.0f) * 0.42f);
            openProjectRect = Rect{fullSidebar.x + (fullSidebar.width - buttonWidth) * 0.5f, top + 86.0f, buttonWidth, 40.0f};
        } else {
            openProjectRect = {};
            refreshRect = Rect{fullSidebar.x + fullSidebar.width - 36.0f, actionY, 28.0f, 28.0f};
            folderNewRect = Rect{refreshRect.x - 32.0f, actionY, 28.0f, 28.0f};
            fileNewRect = Rect{folderNewRect.x - 32.0f, actionY, 28.0f, 28.0f};
        }
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
            if (fileCreatePending) {
                for (int index = 0; index < static_cast<int>(fileEntries.size()); ++index) {
                    if (!fileEntries[static_cast<std::size_t>(index)].pending) {
                        continue;
                    }
                    if (index < fileScroll) {
                        fileScroll = index;
                    } else if (index >= fileScroll + visibleRows) {
                        fileScroll = index - visibleRows + 1;
                    }
                    break;
                }
            }
            fileScroll = std::max(0, std::min(fileScroll, std::max(0, static_cast<int>(fileEntries.size()) - visibleRows)));
            for (int index = 0; index < static_cast<int>(fileEntries.size()); ++index) {
                FileEntry& entry = fileEntries[static_cast<std::size_t>(index)];
                const float y = listTop + static_cast<float>(index - fileScroll) * 28.0f;
                const float indent = 10.0f + static_cast<float>(entry.depth) * 14.0f;
                entry.rect = Rect{fullSidebar.x + 6.0f, y, std::max(0.0f, fullSidebar.width - 12.0f), 26.0f};
                entry.twist = Rect{fullSidebar.x + indent - 3.0f, y + 5.0f, 16.0f, 16.0f};
            }
        }
        toolRows.clear();
        deviceRows.clear();
        menuTitles.clear();
        const char* menuNames[] = {"RoboUI", "File", "Edit", "View", "Object", "Arrange", "Text", "Window", "Help"};
        float titleX = 4.0f;
        for (const char* title : menuNames) {
            HitRow row;
            row.name = title;
            const float titleWidth = 26.0f + static_cast<float>(std::strlen(title)) * 7.2f;
            row.rect = Rect{titleX, 0.0f, titleWidth, TOOLBAR_HEIGHT};
            menuTitles.push_back(std::move(row));
            titleX += titleWidth;
        }
        runButton = Rect{std::max(titleX + 8.0f, width - 92.0f), 6.0f, 80.0f, 24.0f};
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
                    addItem("Open as Designer", 0);
                    addItem("Open as Editor", 7);
                } else {
                    addItem("Open", 7);
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
            const float menuWidth = 228.0f;
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
        appMenuRows.clear();
        if (openMenu >= 0 && openMenu < static_cast<int>(menuTitles.size())) {
            struct MenuCommand {
                const char* label = "";
                const char* shortcut = "";
                int action = -1;
                bool checked = false;
                bool enabled = true;
            };
            std::vector<MenuCommand> commands;
            const auto addCommand = [&](const char* label, const char* shortcut, int action, bool checked = false, bool enabled = true) {
                commands.push_back(MenuCommand{label, shortcut, action, checked, enabled});
            };
            const auto addRule = [&]() { commands.push_back(MenuCommand{}); };
            const XmlNode* selectedNode = document.NodeAt(selection);
            const bool textNode = selectedNode != nullptr && selectedNode->name == "text";
            const std::string textAlign = document.Attribute(selection, "textAlign");
            const std::string fontWeight = document.Attribute(selection, "fontWeight");
            const bool folderOpen = !projectRoot.empty();
            switch (openMenu) {
            case 0:
                addCommand("About RoboUIDesigner", "", 60);
                addRule();
                addCommand("Quit RoboUIDesigner", "Alt+F4", 19);
                break;
            case 1:
                addCommand("New File", "Ctrl+N", 10, false, folderOpen);
                addCommand("New Folder", "", 11, false, folderOpen);
                addRule();
                addCommand("New Window", "Ctrl+Shift+N", 12);
                addRule();
                addCommand("Open File...", "Ctrl+O", 13);
                addCommand("Open Folder...", "", 14);
                addRule();
                addCommand("Save", "Ctrl+S", 15);
                addCommand("Save As...", "Ctrl+Shift+S", 16);
                addRule();
                addCommand("Run", "F5", 54);
                addRule();
                addCommand("Close Folder", "", 17, false, folderOpen);
                addCommand("Close Window", "", 18);
                break;
            case 2:
                addCommand("Undo", "Ctrl+Z", 20, false, !undoStack.empty());
                addCommand("Redo", "Ctrl+Shift+Z", 21, false, !redoStack.empty());
                addRule();
                addCommand("Cut", "Ctrl+X", 22, false, !selection.empty());
                addCommand("Copy", "Ctrl+C", 23, false, !selection.empty());
                addCommand("Paste", "Ctrl+V", 24);
                addCommand("Duplicate", "Ctrl+D", 53, false, !selection.empty());
                addCommand("Delete", "Del", 25, false, !selection.empty());
                addRule();
                addCommand("Select All", "Ctrl+A", 26);
                addCommand("Deselect", "", 27, false, !selection.empty());
                break;
            case 3:
                addCommand("Zoom In", "Ctrl++", 28);
                addCommand("Zoom Out", "Ctrl+-", 29);
                addCommand("Actual Size", "Ctrl+0", 31, std::fabs(zoom - 1.0f) < 0.01f);
                addCommand("Zoom to Fit", "Ctrl+1", 30);
                addRule();
                addCommand("Show Grid", "", 32, showGrid);
                addCommand("Show Rulers", "", 33, showRulers);
                addRule();
                addCommand("Desktop", "", 34, deviceIndex == 0);
                addCommand("Tablet", "", 35, deviceIndex == 1);
                addCommand("Mobile", "", 36, deviceIndex == 2);
                addRule();
                addCommand("Explorer", "", 37, leftNav == 0);
                addCommand("Pages", "", 38, leftNav == 1);
                addCommand("Library", "", 39, leftNav == 2);
                addCommand("Assets", "", 40, leftNav == 3);
                addCommand("Variables", "", 41, leftNav == 4);
                break;
            case 4:
                addCommand("Select", "V", 42, activeTool == EditorTool::SELECT);
                addCommand("Frame", "F", 43, activeTool == EditorTool::FRAME);
                addCommand("Text", "T", 44, activeTool == EditorTool::TEXT);
                addCommand("Image", "I", 45, activeTool == EditorTool::IMAGE);
                addCommand("Line", "L", 46, activeTool == EditorTool::LINE);
                addCommand("Hand", "H", 47, activeTool == EditorTool::HAND);
                addCommand("Zoom", "Z", 48, activeTool == EditorTool::ZOOM);
                break;
            case 5: {
                addCommand("Bring Forward", "", 50, false, !selection.empty());
                addCommand("Send Backward", "", 51, false, !selection.empty());
                addRule();
                const bool boardNode = selectedNode != nullptr && selectedNode->name == "board";
                bool hasWindow = false;
                if (const XmlNode* root = document.Root()) {
                    for (const XmlNode& child : root->children) {
                        if (child.name == "window") {
                            hasWindow = true;
                            break;
                        }
                    }
                }
                addCommand("Add Board", "", 52);
                addCommand("Add to Window", "", 55, false, boardNode && hasWindow);
                break;
            }
            case 6:
                addCommand("Align Left", "", 80, textAlign.empty() || textAlign == "left", textNode);
                addCommand("Align Center", "", 81, textAlign == "center", textNode);
                addCommand("Align Right", "", 82, textAlign == "right", textNode);
                addRule();
                addCommand("Regular", "", 83, fontWeight.empty() || fontWeight == "400", textNode);
                addCommand("Medium", "", 84, fontWeight == "500", textNode);
                addCommand("Semibold", "", 85, fontWeight == "600", textNode);
                break;
            case 7:
                addCommand("Minimize", "", 90);
                addCommand("Zoom Window", "", 91);
                break;
            case 8:
                addCommand("Keyboard Shortcuts", "", 61);
                addCommand("About RoboUIDesigner", "", 60);
                break;
            default:
                break;
            }
            const float menuWidth = 328.0f;
            float menuX = menuTitles[static_cast<std::size_t>(openMenu)].rect.x;
            if (menuX + menuWidth > width - 8.0f) {
                menuX = std::max(8.0f, width - menuWidth - 8.0f);
            }
            float menuY = TOOLBAR_HEIGHT;
            for (const MenuCommand& command : commands) {
                HitRow row;
                row.depth = command.action;
                row.branch = command.checked;
                row.hidden = !command.enabled;
                const bool separator = command.action < 0;
                const float rowHeight = separator ? 9.0f : 30.0f;
                row.rect = Rect{menuX + 6.0f, menuY + 6.0f, menuWidth - 12.0f, rowHeight};
                if (!separator) {
                    row.name = command.label;
                    if (command.shortcut[0] != '\0') {
                        row.name += '\t';
                        row.name += command.shortcut;
                    }
                }
                appMenuRows.push_back(std::move(row));
                menuY += rowHeight;
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
        if (propertyName == "width" || propertyName == "height") {
            document.SetAttribute(selection, "auto", "false");
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
        while (true) {
            const XmlNode* node = document.NodeAt(parent);
            if (node != nullptr && (node->name == "window" || node->name == "board")) {
                return parent;
            }
            const ComponentSpec* spec = node == nullptr ? nullptr : FindComponent(node->name);
            if (spec != nullptr && spec->acceptsChildren && node->name != "screens") {
                return parent;
            }
            if (parent.empty()) {
                break;
            }
            parent.pop_back();
        }
        if (document.Root() != nullptr && document.Root()->name == "screens" && !document.Root()->children.empty()) {
            return std::vector<int>{0};
        }
        return parent;
    };

    auto componentScopeOf = [&](const std::vector<int>& path) {
        std::vector<int> found;
        for (std::size_t count = 1; count <= path.size(); ++count) {
            const std::vector<int> prefix(path.begin(), path.begin() + static_cast<std::ptrdiff_t>(count));
            const XmlNode* node = document.NodeAt(prefix);
            if (node == nullptr) {
                continue;
            }
            if (node->name == "component" || node->name == "instance" || !document.Attribute(prefix, "component").empty()) {
                found = prefix;
            }
        }
        return found;
    };
    auto findComponentDefinition = [&](const std::string& name, std::vector<int>& path) {
        const XmlNode* root = document.Root();
        if (root == nullptr || name.empty()) {
            return false;
        }
        auto walk = [&](auto&& self, const XmlNode& node, const std::vector<int>& current) -> bool {
            if (node.name == "component" && document.Attribute(current, "name") == name) {
                path = current;
                return true;
            }
            for (int index = 0; index < static_cast<int>(node.children.size()); ++index) {
                std::vector<int> child = current;
                child.push_back(index);
                if (self(self, node.children[static_cast<std::size_t>(index)], child)) {
                    return true;
                }
            }
            return false;
        };
        return walk(walk, *root, {});
    };
    auto applyComponent = [&](const std::string& name) {
        std::vector<int> master;
        if (!findComponentDefinition(name, master)) {
            return;
        }
        const XmlNode* masterNode = document.NodeAt(master);
        if (masterNode == nullptr) {
            return;
        }
        const XmlNode* body = nullptr;
        for (const XmlNode& child : masterNode->children) {
            if (child.name != "property" && child.name != "event") {
                body = &child;
                break;
            }
        }
        if (body == nullptr) {
            return;
        }
        std::vector<std::string> pieces;
        for (const XmlNode& child : body->children) {
            pieces.push_back(SerializeXml(child));
        }
        std::vector<std::vector<int>> instances;
        auto walk = [&](auto&& self, const XmlNode& node, const std::vector<int>& current) -> void {
            if (PathStartsWith(current, master)) {
                return;
            }
            if (node.name != "component" && document.Attribute(current, "component") == name) {
                instances.push_back(current);
            }
            for (int index = 0; index < static_cast<int>(node.children.size()); ++index) {
                std::vector<int> child = current;
                child.push_back(index);
                self(self, node.children[static_cast<std::size_t>(index)], child);
            }
        };
        if (document.Root() != nullptr) {
            walk(walk, *document.Root(), {});
        }
        for (const std::vector<int>& instance : instances) {
            const XmlNode* node = document.NodeAt(instance);
            if (node == nullptr) {
                continue;
            }
            for (int index = static_cast<int>(node->children.size()) - 1; index >= 0; --index) {
                std::vector<int> child = instance;
                child.push_back(index);
                document.DeleteNode(child);
            }
            for (const std::string& piece : pieces) {
                std::vector<int> inserted;
                document.PasteXml(instance, piece, inserted);
            }
        }
        rebuildPreview();
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
    auto placeSelection = [&](float x, float y, float width, float height, bool lockSize) {
        if (selection.empty()) {
            return;
        }
        document.SetAttribute(selection, "position", "absolute");
        document.SetAttribute(selection, "x", pixels(x));
        document.SetAttribute(selection, "y", pixels(y));
        if (lockSize) {
            document.SetAttribute(selection, "auto", "false");
            document.SetAttribute(selection, "width", pixels(std::max(16.0f, width)));
            document.SetAttribute(selection, "height", pixels(std::max(16.0f, height)));
        }
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
    auto addCanvasNode = [&](const char* componentName, const char* label, const std::vector<int>& parentPath) {
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
        if (!document.InsertChild(parentPath, componentName, inserted)) {
            historyOpen = false;
            return;
        }
        const int boardNumber = static_cast<int>(inserted.empty() ? 1 : inserted.back() + 1);
        const std::string boardName = std::string(label) + std::to_string(boardNumber);
        document.SetAttribute(inserted, "name", boardName);
        document.SetAttribute(inserted, "position", "absolute");
        document.SetAttribute(inserted, "x", pixels(nextX));
        document.SetAttribute(inserted, "y", pixels(nextY));
        document.SetAttribute(inserted, "width", pixels(boardWidth));
        document.SetAttribute(inserted, "height", pixels(boardHeight));
        document.SetAttribute(inserted, "background", "#FFFFFF");
        document.SetAttribute(inserted, "auto", "false");
        if (std::string(componentName) == "window") {
            document.SetAttribute(inserted, "title", boardName);
        }
        selectOnly(inserted);
        rebuildPreview();
        finishEdit();
        const float centerX = nextX + boardWidth * 0.5f;
        const float centerY = nextY + boardHeight * 0.5f;
        panX = (deviceWidth * 0.5f - centerX) * zoom;
        panY = (deviceHeight * 0.5f - centerY) * zoom;
        paintDirty = true;
    };
    auto addBoard = [&]() { addCanvasNode("board", "Board ", {}); };
    auto addBoardToWindow = [&]() {
        const XmlNode* selected = document.NodeAt(selection);
        if (selected == nullptr || selected->name != "board" || document.Root() == nullptr) {
            return;
        }
        std::vector<int> windowPath;
        for (const std::vector<int>& path : selections) {
            const XmlNode* node = document.NodeAt(path);
            if (node != nullptr && node->name == "window") {
                windowPath = path;
                break;
            }
        }
        if (windowPath.empty()) {
            int index = 0;
            for (const XmlNode& child : document.Root()->children) {
                if (child.name == "window") {
                    windowPath = {index};
                    break;
                }
                ++index;
            }
        }
        if (windowPath.empty()) {
            return;
        }
        remember();
        if (!document.AddBoardContent(selection, windowPath)) {
            historyOpen = false;
            return;
        }
        rebuildPreview();
        finishEdit();
        paintDirty = true;
    };
    auto addWindow = [&]() { addCanvasNode("window", "Window ", {}); };
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
            if (std::find(lockedPaths.begin(), lockedPaths.end(), prefix) != lockedPaths.end() || document.Attribute(prefix, "locked") == "true") {
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

    auto closeAppMenu = [&]() {
        appMenuOpen = false;
        openMenu = -1;
        appMenuHover = -1;
        appMenuRows.clear();
    };
    auto insertElement = [&](const char* name) {
        remember();
        std::vector<int> inserted;
        if (document.InsertChild(parentForInsert(), name, inserted)) {
            selectOnly(inserted);
            rebuildPreview();
            finishEdit();
        }
    };
    auto moveSelection = [&](int direction) {
        if (selection.empty()) {
            return;
        }
        remember();
        if (!document.MoveNode(selection, direction)) {
            historyOpen = false;
            return;
        }
        selection.back() += direction < 0 ? -1 : 1;
        selections = {selection};
        rebuildPreview();
        finishEdit();
    };
    auto applyAppAction = [&](int action) {
        closeAppMenu();
        if (action == 10) {
            leftNav = 0;
            explorerActive = true;
            beginExplorerCreate(false);
        } else if (action == 11) {
            leftNav = 0;
            explorerActive = true;
            beginExplorerCreate(true);
        } else if (action == 12) {
            LaunchDesignerWindow();
        } else if (action == 13) {
            const std::filesystem::path chosen = PickXmlFile(window.Hwnd(), false, {});
            if (!chosen.empty()) {
                openWorkFile(chosen, true);
            }
        } else if (action == 14) {
            const std::filesystem::path chosen = PickProjectFolder(window.Hwnd());
            if (!chosen.empty()) {
                projectRoot = chosen;
                openFolders.clear();
                openFolders.push_back(projectRoot.generic_string());
                selectExplorerOnly("");
                fileCreatePending = false;
                fileRenaming = false;
                renameReplace = false;
                fileScroll = 0;
                filesDirty = true;
                leftNav = 0;
            }
        } else if (action == 15) {
            saveActive();
        } else if (action == 16) {
            const bool designing = surfaceDesigner();
            const std::string suggestedName = designing || activeTab < 0
                                                   ? std::filesystem::path(document.FilePath()).filename().string()
                                                   : std::filesystem::path(workTabs[static_cast<std::size_t>(activeTab)].path).filename().string();
            const std::filesystem::path chosen = PickXmlFile(window.Hwnd(), true, ToWide(suggestedName));
            if (!chosen.empty()) {
                std::ofstream output(chosen, std::ios::binary);
                if (output) {
                    if (designing || activeTab < 0) {
                        output << document.Text();
                    } else {
                        output << workTabs[static_cast<std::size_t>(activeTab)].editorText;
                    }
                    output.close();
                    if (designing && document.LoadFile(chosen)) {
                        rebuildPreview();
                    }
                    if (activeTab >= 0) {
                        WorkTab& tab = workTabs[static_cast<std::size_t>(activeTab)];
                        tab.path = chosen.generic_string();
                        tab.dirty = false;
                        if (designing) {
                            tab.editorText = document.Text();
                        }
                    }
                }
            }
        } else if (action == 17) {
            projectRoot.clear();
            openFolders.clear();
            selectExplorerOnly("");
            fileCreatePending = false;
            fileRenaming = false;
            renameReplace = false;
            fileScroll = 0;
            filesDirty = true;
        } else if (action == 18 || action == 19) {
            PostMessageW(window.Hwnd(), WM_CLOSE, 0, 0);
        } else if (action == 20) {
            undo();
        } else if (action == 21) {
            redo();
        } else if (action == 22) {
            if (!selection.empty()) {
                CopyText(document.NodeXml(selection));
                remember();
                document.DeleteNode(selection);
                selection.clear();
                selections.clear();
                rebuildPreview();
                finishEdit();
            }
        } else if (action == 23) {
            if (!selection.empty()) {
                CopyText(document.NodeXml(selection));
            }
        } else if (action == 24) {
            std::vector<int> inserted;
            if (document.PasteXml(parentForInsert(), PasteText(), inserted)) {
                remember();
                selectOnly(inserted);
                rebuildPreview();
                finishEdit();
            }
        } else if (action == 25) {
            if (!selection.empty()) {
                remember();
                document.DeleteNode(selection);
                selection.clear();
                selections.clear();
                rebuildPreview();
                finishEdit();
            }
        } else if (action == 26) {
            selections.clear();
            if (preview != nullptr) {
                for (int index = 0; index < static_cast<int>(preview->Children().size()); ++index) {
                    selections.push_back({index});
                }
            }
            selection = selections.empty() ? std::vector<int>{} : selections.back();
        } else if (action == 27) {
            selection.clear();
            selections.clear();
        } else if (action == 28 || action == 29) {
            layoutChrome(clientWidth, clientHeight);
            const Point center{canvas.x + canvas.width * 0.5f, canvas.y + canvas.height * 0.5f};
            zoomAt(center, action == 28 ? 1.25f : 0.8f);
            fitted = true;
        } else if (action == 30) {
            fitted = false;
            layoutChrome(clientWidth, clientHeight);
        } else if (action == 31) {
            zoom = 1.0f;
            panX = 0.0f;
            panY = 0.0f;
            fitted = true;
        } else if (action == 32) {
            showGrid = !showGrid;
        } else if (action == 33) {
            showRulers = !showRulers;
        } else if (action >= 34 && action <= 36) {
            deviceIndex = action - 34;
            deviceWidth = DEVICE_PRESETS[deviceIndex].width;
            deviceHeight = DEVICE_PRESETS[deviceIndex].height;
            fitted = false;
            layoutChrome(clientWidth, clientHeight);
        } else if (action >= 37 && action <= 41) {
            leftNav = action - 37;
        } else if (action == 42) {
            activeTool = EditorTool::SELECT;
        } else if (action == 43) {
            activeTool = EditorTool::FRAME;
        } else if (action == 44) {
            activeTool = EditorTool::TEXT;
        } else if (action == 45) {
            activeTool = EditorTool::IMAGE;
        } else if (action == 46) {
            activeTool = EditorTool::LINE;
        } else if (action == 47) {
            activeTool = EditorTool::HAND;
        } else if (action == 48) {
            activeTool = EditorTool::ZOOM;
        } else if (action == 50) {
            moveSelection(1);
        } else if (action == 51) {
            moveSelection(-1);
        } else if (action == 52) {
            addBoard();
        } else if (action == 55) {
            addBoardToWindow();
        } else if (action == 53) {
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
            }
        } else if (action == 54) {
            if (document.FilePath().empty() || !document.Save()) {
                MessageBoxW(window.Hwnd(), L"Save the document before running it.", L"RoboUIDesigner", MB_OK | MB_ICONWARNING);
            } else {
                const char* device = deviceIndex == 1 ? "tablet" : deviceIndex == 2 ? "mobile" : "desktop";
                LaunchRuntime(window.Hwnd(), document.FilePath(), device);
            }
        } else if (action == 60) {
            MessageBoxW(window.Hwnd(), L"RoboUIDesigner\nA visual editor for Runtime XML.", L"About RoboUIDesigner", MB_OK);
        } else if (action == 61) {
            MessageBoxW(window.Hwnd(),
                        L"Ctrl+N  New File\nCtrl+O  Open File\nCtrl+S  Save\nCtrl+Shift+S  Save As\nF5  Run\nCtrl+Z  Undo\nCtrl+Shift+Z  Redo\nCtrl+C  Copy\nCtrl+V  Paste\nCtrl+D  Duplicate\n\nV Select   F Frame   T Text\nI Image   L Line   H Hand   Z Zoom",
                        L"Keyboard Shortcuts", MB_OK);
        } else if (action >= 70 && action <= 77) {
            const char* names[] = {"frame", "text", "image", "vector", "path", "line", "group", "scroll"};
            insertElement(names[action - 70]);
        } else if (action >= 80 && action <= 82) {
            const char* aligns[] = {"left", "center", "right"};
            remember();
            document.SetAttribute(selection, "textAlign", aligns[action - 80]);
            rebuildPreview();
            finishEdit();
        } else if (action >= 83 && action <= 85) {
            const char* weights[] = {"400", "500", "600"};
            remember();
            document.SetAttribute(selection, "fontWeight", weights[action - 83]);
            rebuildPreview();
            finishEdit();
        } else if (action == 90) {
            ShowWindow(window.Hwnd(), SW_MINIMIZE);
        } else if (action == 91) {
            ShowWindow(window.Hwnd(), IsZoomed(window.Hwnd()) ? SW_RESTORE : SW_MAXIMIZE);
        }
    };

    auto menuTitleAt = [&](Point point) {
        for (int index = 0; index < static_cast<int>(menuTitles.size()); ++index) {
            if (ContainsPoint(menuTitles[static_cast<std::size_t>(index)].rect, point)) {
                return index;
            }
        }
        return -1;
    };
    window.SetMouseDown([&](Point point) {
        layoutChrome(clientWidth, clientHeight);
        if (ContainsPoint(runButton, point)) {
            closeAppMenu();
            closeFileMenu();
            applyAppAction(54);
            paintDirty = true;
            return;
        }
        const int chosenMenu = menuTitleAt(point);
        if (appMenuOpen) {
            for (const HitRow& row : appMenuRows) {
                if (!ContainsPoint(row.rect, point)) {
                    continue;
                }
                if (row.depth >= 0 && !row.hidden) {
                    applyAppAction(row.depth);
                    paintDirty = true;
                }
                return;
            }
            if (chosenMenu >= 0) {
                if (chosenMenu == openMenu) {
                    closeAppMenu();
                } else {
                    openMenu = chosenMenu;
                    appMenuOpen = true;
                    appMenuHover = -1;
                }
                paintDirty = true;
                return;
            }
            if (!appMenuRows.empty()) {
                const Rect popup{appMenuRows.front().rect.x - 6.0f, TOOLBAR_HEIGHT, appMenuRows.front().rect.width + 12.0f,
                                 appMenuRows.back().rect.y + appMenuRows.back().rect.height - TOOLBAR_HEIGHT + 8.0f};
                if (ContainsPoint(popup, point)) {
                    return;
                }
            }
            closeAppMenu();
            paintDirty = true;
            return;
        }
        if (chosenMenu >= 0) {
            closeFileMenu();
            openMenu = chosenMenu;
            appMenuOpen = true;
            appMenuHover = -1;
            paintDirty = true;
            return;
        }
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
        for (const HitRow& row : tabRows) {
            if (ContainsPoint(row.twist, point)) {
                closeTab(row.depth);
                paintDirty = true;
                return;
            }
            if (ContainsPoint(row.rect, point)) {
                showTab(row.depth, true);
                paintDirty = true;
                return;
            }
        }
        if (fileRenaming) {
            bool onDraft = false;
            for (const FileEntry& entry : fileEntries) {
                if ((entry.pending || entry.path.generic_string() == selectedFile) && ContainsPoint(entry.rect, point)) {
                    onDraft = true;
                    break;
                }
            }
            if (!onDraft) {
                commitFileRename();
            }
        }
        explorerActive = false;
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
            explorerActive = true;
            editorFocused = false;
            if (projectRoot.empty()) {
                if (ContainsPoint(openProjectRect, point)) {
                    const std::filesystem::path chosen = PickProjectFolder(window.Hwnd());
                    if (!chosen.empty()) {
                        projectRoot = chosen;
                        openFolders.clear();
                        openFolders.push_back(projectRoot.generic_string());
                        selectExplorerOnly("");
                        fileCreatePending = false;
                        fileRenaming = false;
                        renameReplace = false;
                        fileScroll = 0;
                        filesDirty = true;
                    }
                }
                paintDirty = true;
                return;
            }
            if (ContainsPoint(fileNewRect, point)) {
                beginExplorerCreate(false);
                paintDirty = true;
                return;
            }
            if (ContainsPoint(folderNewRect, point)) {
                beginExplorerCreate(true);
                paintDirty = true;
                return;
            }
            if (ContainsPoint(refreshRect, point)) {
                filesDirty = true;
                paintDirty = true;
                return;
            }
            const bool control = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            for (const FileEntry& entry : fileEntries) {
                if (!ContainsPoint(entry.rect, point) || entry.rect.y + entry.rect.height < fullSidebar.y + 72.0f || entry.rect.y > fullSidebar.y + fullSidebar.height) {
                    continue;
                }
                const unsigned now = GetTickCount();
                const std::string key = entry.path.generic_string();
                const bool again = !control && !shift && lastFileKey == key && now - lastFileTick < 400;
                lastFileTick = now;
                lastFileKey = key;
                if (entry.directory && !entry.pending && ContainsPoint(entry.twist, point)) {
                    const auto found = std::find(openFolders.begin(), openFolders.end(), key);
                    if (found == openFolders.end()) {
                        openFolders.push_back(key);
                    } else {
                        openFolders.erase(found);
                    }
                    filesDirty = true;
                    if (!control && !shift) {
                        selectExplorerOnly(key);
                    }
                    paintDirty = true;
                    return;
                }
                if (shift) {
                    int anchorIndex = -1;
                    int hitIndex = -1;
                    for (int index = 0; index < static_cast<int>(fileEntries.size()); ++index) {
                        const std::string rowKey = fileEntries[static_cast<std::size_t>(index)].path.generic_string();
                        if (rowKey == fileAnchor) {
                            anchorIndex = index;
                        }
                        if (rowKey == key) {
                            hitIndex = index;
                        }
                    }
                    if (anchorIndex < 0 || hitIndex < 0) {
                        selectExplorerOnly(key);
                    } else {
                        selectedFiles.clear();
                        const int from = std::min(anchorIndex, hitIndex);
                        const int to = std::max(anchorIndex, hitIndex);
                        for (int index = from; index <= to; ++index) {
                            if (!fileEntries[static_cast<std::size_t>(index)].pending) {
                                selectedFiles.push_back(fileEntries[static_cast<std::size_t>(index)].path.generic_string());
                            }
                        }
                        selectedFile = key;
                    }
                } else if (control) {
                    const auto found = std::find(selectedFiles.begin(), selectedFiles.end(), key);
                    if (found == selectedFiles.end()) {
                        selectedFiles.push_back(key);
                    } else {
                        selectedFiles.erase(found);
                    }
                    selectedFile = selectedFiles.empty() ? std::string{} : key;
                    fileAnchor = key;
                } else if (entry.directory && again && !entry.pending) {
                    selectExplorerOnly(key);
                    const auto found = std::find(openFolders.begin(), openFolders.end(), key);
                    if (found == openFolders.end()) {
                        openFolders.push_back(key);
                    } else {
                        openFolders.erase(found);
                    }
                    filesDirty = true;
                } else if (!entry.directory && again && !entry.pending) {
                    selectExplorerOnly(key);
                    const std::string extension = LowerCopy(entry.path.extension().string());
                    openWorkFile(entry.path, extension == ".xml");
                } else {
                    selectExplorerOnly(key);
                }
                paintDirty = true;
                return;
            }
            selectExplorerOnly("");
            paintDirty = true;
            return;
        }
        if (!surfaceDesigner()) {
            if (ContainsPoint(canvas, point)) {
                editorFocused = true;
                placeEditorCaret(point);
                paintDirty = true;
                return;
            }
            if (ContainsPoint(inspector, point) || ContainsPoint(xmlPanel, point) || ContainsPoint(hierarchy, point) ||
                (leftNav != 3 && ContainsPoint(fullSidebar, point))) {
                editorFocused = false;
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
        if (leftNav == 2 && ContainsPoint(librarySearchRect, point)) {
            editTarget = EditTarget::LIBRARY;
            paintDirty = true;
            return;
        }
        if (leftNav == 3 && ContainsPoint(assetPackIcons, point)) {
            assetPack = 0;
            assetFilterReady = false;
            assetSelected = -1;
            assetScroll = 0;
            paintDirty = true;
            return;
        }
        if (leftNav == 3 && ContainsPoint(assetPackFlags, point)) {
            assetPack = 1;
            assetFilterReady = false;
            assetSelected = -1;
            assetScroll = 0;
            paintDirty = true;
            return;
        }
        if (leftNav == 3 && ContainsPoint(assetSearchRect, point)) {
            editTarget = EditTarget::ASSET;
            paintDirty = true;
            return;
        }
        if (leftNav == 3 && assetPlace.width > 1.0f && ContainsPoint(assetPlace, point)) {
            IconPack& activePack = assetPack == 1 ? countryPack : iconPack;
            std::string xml = activePack.VectorXml(assetSelected);
            if (xml.empty() && assetPack == 1 && assetSelected >= 0) {
                const std::string iso = activePack.Name(assetSelected);
                const std::filesystem::path dest = std::filesystem::path(RUNTIME_SOURCE_DIR) / "assets" / "flags" / (iso + ".png");
                if (activePack.WriteFile(assetSelected, dest)) {
                    const char* title = CountryTitle(iso);
                    std::string label = title != nullptr ? title : iso;
                    std::string escaped;
                    for (char character : label) {
                        if (character == '&') {
                            escaped += "&amp;";
                        } else if (character == '<') {
                            escaped += "&lt;";
                        } else if (character == '"') {
                            escaped += "&quot;";
                        } else {
                            escaped += character;
                        }
                    }
                    std::string source = dest.string();
                    for (char& character : source) {
                        if (character == '\\') {
                            character = '/';
                        }
                    }
                    xml = "<image name=\"" + escaped + "\" source=\"" + source + "\" width=\"80\" height=\"60\" fit=\"contain\"/>";
                }
            }
            std::vector<int> inserted;
            if (!xml.empty()) {
                remember();
                if (!document.PasteXml(parentForInsert(), xml, inserted)) {
                    historyOpen = false;
                } else {
                    selectOnly(inserted);
                    rebuildPreview();
                    finishEdit();
                }
            }
            paintDirty = true;
            return;
        }
        if (leftNav == 3) {
            for (const HitRow& row : assetRows) {
                if (ContainsPoint(row.rect, point) && row.rect.y >= fullSidebar.y + 112.0f && row.rect.y < fullSidebar.y + fullSidebar.height - (assetSelected >= 0 ? 96.0f : 0.0f)) {
                    assetSelected = row.depth;
                    paintDirty = true;
                    return;
                }
            }
        }
        if (leftNav == 4 && ContainsPoint(variableSearchRect, point)) {
            editTarget = EditTarget::VARIABLE;
            paintDirty = true;
            return;
        }
        if (leftNav == 4 && variableAddRect.width > 1.0f && ContainsPoint(variableAddRect, point)) {
            std::string name = "custom";
            int suffix = 2;
            auto taken = [&](const std::string& candidate) {
                for (const VariableHit& row : variableRows) {
                    if (!row.header && row.name == candidate) {
                        return true;
                    }
                }
                return false;
            };
            while (taken(name)) {
                name = "custom-" + std::to_string(suffix);
                ++suffix;
            }
            remember();
            if (document.SetToken("color", name, "#0E7AFF")) {
                variableSelected = name;
                variableDraft = "#0E7AFF";
                editTarget = EditTarget::VARIABLE_VALUE;
                rebuildPreview();
                finishEdit();
            } else {
                historyOpen = false;
            }
            paintDirty = true;
            return;
        }
        if (leftNav == 4 && variableValueRect.width > 1.0f && ContainsPoint(variableValueRect, point)) {
            editTarget = EditTarget::VARIABLE_VALUE;
            for (const VariableHit& row : variableRows) {
                if (!row.header && row.name == variableSelected) {
                    variableDraft = row.value;
                }
            }
            paintDirty = true;
            return;
        }
        if (leftNav == 4 && variableRemoveRect.width > 1.0f && ContainsPoint(variableRemoveRect, point)) {
            for (const VariableHit& row : variableRows) {
                if (row.header || row.name != variableSelected) {
                    continue;
                }
                remember();
                if (!document.RemoveToken(row.group, row.name, row.builtin)) {
                    historyOpen = false;
                } else {
                    variableSelected.clear();
                    variableDraft.clear();
                    editTarget = EditTarget::NONE;
                    rebuildPreview();
                    finishEdit();
                }
                break;
            }
            paintDirty = true;
            return;
        }
        if (leftNav == 4) {
            const float listBottom = fullSidebar.y + fullSidebar.height - (variableSelected.empty() ? 0.0f : 86.0f);
            for (const VariableHit& row : variableRows) {
                if (row.header || row.rect.y < fullSidebar.y + 76.0f || row.rect.y > listBottom) {
                    continue;
                }
                if (ContainsPoint(row.rect, point)) {
                    variableSelected = row.name;
                    variableDraft = row.value;
                    editTarget = EditTarget::VARIABLE_VALUE;
                    paintDirty = true;
                    return;
                }
            }
        }
        for (const HitRow& row : libraryRows) {
            if (row.depth >= 0 && ContainsPoint(row.rect, point) && row.rect.y >= fullSidebar.y + 76.0f &&
                row.rect.y < fullSidebar.y + fullSidebar.height) {
                if (row.name == "window") {
                    addWindow();
                    paintDirty = true;
                    return;
                }
                if (row.name == "board") {
                    addBoard();
                    paintDirty = true;
                    return;
                }
                if (row.name == "scrollbar") {
                    remember();
                    std::vector<int> inserted;
                    const char* scrollbarXml =
                        "<frame name=\"Scrollbar\" component=\"Scrollbar\" role=\"scrollbar\" orientation=\"vertical\" position=\"absolute\" "
                        "width=\"14\" height=\"fill\" background=\"#E8E8ED\" radius=\"7\">"
                        "<frame name=\"Decrement\" role=\"decrement\" width=\"fill\" height=\"14\" background=\"#D2D2D7\" radius=\"4\"/>"
                        "<frame name=\"Thumb\" role=\"thumb\" width=\"fill\" height=\"48\" background=\"#86868B\" radius=\"7\"/>"
                        "<frame name=\"Increment\" role=\"increment\" width=\"fill\" height=\"14\" background=\"#D2D2D7\" radius=\"4\"/>"
                        "</frame>";
                    if (document.PasteXml(parentForInsert(), scrollbarXml, inserted)) {
                        selectOnly(inserted);
                        rebuildPreview();
                    }
                    paintDirty = true;
                    return;
                }
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
            if (!editScope.empty() && (ContainsPoint(scopeDoneRect, point) || ContainsPoint(scopeMainRect, point) || ContainsPoint(scopeApplyRect, point))) {
                if (ContainsPoint(scopeDoneRect, point)) {
                    editScope.clear();
                } else if (ContainsPoint(scopeApplyRect, point)) {
                    const XmlNode* scoped = document.NodeAt(editScope);
                    const std::string name = scoped != nullptr && scoped->name == "component" ? document.Attribute(editScope, "name")
                                                                                               : document.Attribute(editScope, "component");
                    remember();
                    applyComponent(name);
                } else {
                    const std::string name = document.Attribute(editScope, "component");
                    std::vector<int> master;
                    if (findComponentDefinition(name, master)) {
                        editScope = master;
                        selectOnly(master);
                    }
                }
                applyCursor(point);
                paintDirty = true;
                return;
            }
            const Point local = toArtboard(point);
            Element* hit = preview->HitTest(local);
            std::vector<int> path;
            if (hit != nullptr && FindElementPath(*preview, hit, path)) {
                const unsigned now = GetTickCount();
                const bool doubleClick = SamePath(lastCanvasPath, path) && now - lastCanvasTick < 400;
                lastCanvasTick = now;
                lastCanvasPath = path;
                const std::vector<int> entered = doubleClick ? componentScopeOf(path) : std::vector<int>{};
                if (!entered.empty()) {
                    editScope = entered;
                    selectOnly(path.size() > entered.size() ? path : entered);
                    applyCursor(point);
                    paintDirty = true;
                    return;
                }
                if (!editScope.empty() && !PathStartsWith(path, editScope)) {
                    editScope.clear();
                }
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
        if (leftNav != 0 || !ContainsPoint(fullSidebar, point) || projectRoot.empty()) {
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
                fileMenuDirectory = entry.directory || entry.pending;
                if (!entry.pending && !explorerSelected(fileMenuTarget)) {
                    selectExplorerOnly(fileMenuTarget);
                }
                explorerActive = true;
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
        int nextLibrary = -1;
        if (drag == DragKind::NONE && leftNav == 2) {
            for (int index = 0; index < static_cast<int>(libraryRows.size()); ++index) {
                const HitRow& row = libraryRows[static_cast<std::size_t>(index)];
                if (row.depth >= 0 && ContainsPoint(row.rect, point) && row.rect.y >= fullSidebar.y + 76.0f &&
                    row.rect.y < fullSidebar.y + fullSidebar.height) {
                    nextLibrary = index;
                    break;
                }
            }
        }
        if (nextLibrary != libraryHover) {
            libraryHover = nextLibrary;
            paintDirty = true;
        }
        int nextAsset = -1;
        if (drag == DragKind::NONE && leftNav == 3) {
            for (int index = 0; index < static_cast<int>(assetRows.size()); ++index) {
                const HitRow& row = assetRows[static_cast<std::size_t>(index)];
                if (ContainsPoint(row.rect, point) && row.rect.y >= fullSidebar.y + 76.0f &&
                    row.rect.y < fullSidebar.y + fullSidebar.height - (assetSelected >= 0 ? 96.0f : 0.0f)) {
                    nextAsset = index;
                    break;
                }
            }
        }
        if (nextAsset != assetHover) {
            assetHover = nextAsset;
            paintDirty = true;
        }
        int nextTitle = -1;
        if (drag == DragKind::NONE) {
            for (int index = 0; index < static_cast<int>(menuTitles.size()); ++index) {
                if (ContainsPoint(menuTitles[static_cast<std::size_t>(index)].rect, point)) {
                    nextTitle = index;
                    break;
                }
            }
        }
        if (nextTitle != menuTitleHover) {
            menuTitleHover = nextTitle;
            paintDirty = true;
        }
        const bool nextRun = drag == DragKind::NONE && ContainsPoint(runButton, point);
        if (nextRun != runHover) {
            runHover = nextRun;
            paintDirty = true;
        }
        if (appMenuOpen && nextTitle >= 0 && nextTitle != openMenu) {
            openMenu = nextTitle;
            appMenuHover = -1;
            paintDirty = true;
        }
        int nextAppMenu = -1;
        if (appMenuOpen) {
            for (int index = 0; index < static_cast<int>(appMenuRows.size()); ++index) {
                const HitRow& row = appMenuRows[static_cast<std::size_t>(index)];
                if (row.depth >= 0 && !row.hidden && ContainsPoint(row.rect, point)) {
                    nextAppMenu = index;
                    break;
                }
            }
        }
        if (nextAppMenu != appMenuHover) {
            appMenuHover = nextAppMenu;
            paintDirty = true;
        }
        int nextFile = -1;
        int nextAction = 0;
        const bool nextOpenProject = drag == DragKind::NONE && leftNav == 0 && projectRoot.empty() && ContainsPoint(openProjectRect, point);
        if (nextOpenProject != openProjectHover) {
            openProjectHover = nextOpenProject;
            paintDirty = true;
        }
        if (drag == DragKind::NONE && leftNav == 0 && !projectRoot.empty()) {
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
        int nextTab = -1;
        int nextTabClose = -1;
        if (drag == DragKind::NONE) {
            for (int index = 0; index < static_cast<int>(tabRows.size()); ++index) {
                const HitRow& row = tabRows[static_cast<std::size_t>(index)];
                if (ContainsPoint(row.twist, point)) {
                    nextTab = index;
                    nextTabClose = index;
                    break;
                }
                if (ContainsPoint(row.rect, point)) {
                    nextTab = index;
                    break;
                }
            }
        }
        if (nextTab != tabHover || nextTabClose != tabCloseHover) {
            tabHover = nextTab;
            tabCloseHover = nextTabClose;
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
                document.SetAttribute(node.path, "auto", "false");
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
            placeSelection(x, y, width, height, true);
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
        if (!surfaceDesigner() && activeTab >= 0 && ContainsPoint(canvas, point)) {
            WorkTab& tab = workTabs[static_cast<std::size_t>(activeTab)];
            tab.scroll = std::max(0, tab.scroll - (delta > 0 ? 3 : -3));
            paintDirty = true;
            return;
        }
        if (leftNav == 2 && ContainsPoint(fullSidebar, point)) {
            libraryScroll = std::max(0, libraryScroll - (delta > 0 ? 48 : -48));
            paintDirty = true;
            return;
        }
        if (leftNav == 3 && ContainsPoint(fullSidebar, point)) {
            assetScroll = std::max(0, assetScroll - (delta > 0 ? 96 : -96));
            paintDirty = true;
            return;
        }
        if (leftNav == 4 && ContainsPoint(fullSidebar, point)) {
            variableScroll = std::max(0, variableScroll - (delta > 0 ? 32 : -32));
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
        if ((GetKeyState(VK_CONTROL) & 0x8000) == 0 && preview != nullptr && ContainsPoint(artboardScreen(), point)) {
            const Point local = toArtboard(point);
            const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            Element* hit = preview->HitTest(local);
            for (Element* node = hit; node != nullptr; node = node->Parent()) {
                if (node->OnWheel(delta, shift)) {
                    paintDirty = true;
                    return;
                }
            }
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
                if (fileCreatePending) {
                    cancelExplorerCreate();
                } else {
                    fileRenaming = false;
                    renameReplace = false;
                }
                paintDirty = true;
                return true;
            }
            if (key == VK_RETURN) {
                commitFileRename();
                paintDirty = true;
                return true;
            }
            if (key == VK_BACK) {
                if (renameReplace) {
                    renameDraft.clear();
                    renameReplace = false;
                } else if (!renameDraft.empty()) {
                    do {
                        renameDraft.pop_back();
                    } while (!renameDraft.empty() && (static_cast<unsigned char>(renameDraft.back()) & 0xC0) == 0x80);
                }
                paintDirty = true;
            }
            return true;
        }
        if ((fileMenuOpen || appMenuOpen) && key == VK_ESCAPE) {
            closeFileMenu();
            closeAppMenu();
            paintDirty = true;
            return true;
        }
        if (key == VK_F5) {
            closeFileMenu();
            closeAppMenu();
            applyAppAction(54);
            paintDirty = true;
            return true;
        }
        const bool control = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        const bool editorTab = activeTab >= 0 && activeTab < static_cast<int>(workTabs.size()) && !workTabs[static_cast<std::size_t>(activeTab)].designer;
        if (editorFocused && editorTab && !workTabs[static_cast<std::size_t>(activeTab)].binary && !(control && key == VK_TAB)) {
            WorkTab& tab = workTabs[static_cast<std::size_t>(activeTab)];
            if (control && key == 'S' && (GetKeyState(VK_SHIFT) & 0x8000) == 0) {
                saveActive();
                paintDirty = true;
                return true;
            }
            if (control && key == 'W') {
                closeTab(activeTab);
                paintDirty = true;
                return true;
            }
            if (control && (key == 'Z' || key == 'Y')) {
                return true;
            }
            if (!control) {
                if (tab.caret > tab.editorText.size()) {
                    tab.caret = tab.editorText.size();
                }
                if (key == VK_BACK && tab.caret > 0) {
                    std::size_t index = tab.caret;
                    do {
                        --index;
                    } while (index > 0 && (static_cast<unsigned char>(tab.editorText[index]) & 0xC0) == 0x80);
                    tab.editorText.erase(index, tab.caret - index);
                    tab.caret = index;
                    tab.dirty = true;
                } else if (key == VK_DELETE && tab.caret < tab.editorText.size()) {
                    std::size_t index = tab.caret + 1;
                    while (index < tab.editorText.size() && (static_cast<unsigned char>(tab.editorText[index]) & 0xC0) == 0x80) {
                        ++index;
                    }
                    tab.editorText.erase(tab.caret, index - tab.caret);
                    tab.dirty = true;
                } else if (key == VK_RETURN) {
                    tab.editorText.insert(tab.caret, "\n");
                    ++tab.caret;
                    tab.dirty = true;
                } else if (key == VK_LEFT && tab.caret > 0) {
                    --tab.caret;
                    while (tab.caret > 0 && (static_cast<unsigned char>(tab.editorText[tab.caret]) & 0xC0) == 0x80) {
                        --tab.caret;
                    }
                } else if (key == VK_RIGHT && tab.caret < tab.editorText.size()) {
                    ++tab.caret;
                    while (tab.caret < tab.editorText.size() && (static_cast<unsigned char>(tab.editorText[tab.caret]) & 0xC0) == 0x80) {
                        ++tab.caret;
                    }
                } else if (key == VK_UP || key == VK_DOWN) {
                    int line = 0;
                    std::size_t lineStart = 0;
                    for (std::size_t index = 0; index < tab.caret && index < tab.editorText.size(); ++index) {
                        if (tab.editorText[index] == '\n') {
                            ++line;
                            lineStart = index + 1;
                        }
                    }
                    const int column = static_cast<int>(tab.caret - lineStart);
                    const int target = key == VK_UP ? line - 1 : line + 1;
                    if (target >= 0) {
                        int current = 0;
                        std::size_t start = 0;
                        std::size_t index = 0;
                        for (; index < tab.editorText.size(); ++index) {
                            if (tab.editorText[index] != '\n') {
                                continue;
                            }
                            if (current == target) {
                                break;
                            }
                            ++current;
                            start = index + 1;
                        }
                        if (current == target) {
                            const std::size_t lineEnd = index;
                            tab.caret = std::min(start + static_cast<std::size_t>(std::max(0, column)), lineEnd);
                        }
                    }
                } else if (key == VK_ESCAPE) {
                    editorFocused = false;
                }
                paintDirty = true;
                return true;
            }
        }
        if (control && key == VK_TAB && !workTabs.empty()) {
            const int count = static_cast<int>(workTabs.size());
            const int step = (GetKeyState(VK_SHIFT) & 0x8000) != 0 ? -1 : 1;
            const int next = ((activeTab < 0 ? 0 : activeTab) + step + count) % count;
            showTab(next, true);
            paintDirty = true;
            return true;
        }
        if (control && key == 'W' && activeTab >= 0) {
            closeTab(activeTab);
            paintDirty = true;
            return true;
        }
        if (control && (key == 'Z' || key == 'Y')) {
            if (key == 'Y' || (GetKeyState(VK_SHIFT) & 0x8000) != 0) {
                redo();
            } else {
                undo();
            }
            return true;
        }
        if (explorerActive && leftNav == 0 && control && key == 'A') {
            selectedFiles.clear();
            for (const FileEntry& entry : fileEntries) {
                if (!entry.pending) {
                    selectedFiles.push_back(entry.path.generic_string());
                }
            }
            selectedFile = selectedFiles.empty() ? std::string{} : selectedFiles.back();
            fileAnchor = selectedFiles.empty() ? std::string{} : selectedFiles.front();
            paintDirty = true;
            return true;
        }
        if (explorerActive && leftNav == 0 && control && key == 'C' && !selectedFiles.empty()) {
            std::string joined;
            for (const std::string& item : selectedFiles) {
                if (!joined.empty()) {
                    joined += "\n";
                }
                joined += std::filesystem::path(item).string();
            }
            CopyText(joined);
            return true;
        }
        if (explorerActive && leftNav == 0 && !control && editTarget == EditTarget::NONE) {
            auto explorerIndex = [&]() {
                for (int index = 0; index < static_cast<int>(fileEntries.size()); ++index) {
                    if (fileEntries[static_cast<std::size_t>(index)].path.generic_string() == selectedFile) {
                        return index;
                    }
                }
                return -1;
            };
            if (key == VK_DELETE) {
                if (!selectedFiles.empty()) {
                    removeExplorerPaths(selectedFiles);
                    paintDirty = true;
                }
                return true;
            }
            if (key == VK_F2 && !selectedFile.empty()) {
                renameOriginal = std::filesystem::path(selectedFile).filename().string();
                renameDraft = renameOriginal;
                renameReplace = true;
                fileRenaming = true;
                paintDirty = true;
                return true;
            }
            if (key == VK_UP || key == VK_DOWN || key == VK_HOME || key == VK_END) {
                layoutChrome(clientWidth, clientHeight);
                int index = explorerIndex();
                if (key == VK_HOME) {
                    index = 0;
                } else if (key == VK_END) {
                    index = static_cast<int>(fileEntries.size()) - 1;
                } else if (index < 0) {
                    index = 0;
                } else {
                    index += key == VK_DOWN ? 1 : -1;
                }
                index = std::max(0, std::min(index, static_cast<int>(fileEntries.size()) - 1));
                if (!fileEntries.empty()) {
                    selectExplorerOnly(fileEntries[static_cast<std::size_t>(index)].path.generic_string());
                    revealExplorer(selectedFile);
                }
                paintDirty = true;
                return true;
            }
            if ((key == VK_LEFT || key == VK_RIGHT) && !selectedFile.empty()) {
                layoutChrome(clientWidth, clientHeight);
                const int index = explorerIndex();
                if (index >= 0) {
                    FileEntry& entry = fileEntries[static_cast<std::size_t>(index)];
                    const std::string keyPath = entry.path.generic_string();
                    if (entry.directory && key == VK_RIGHT && !entry.expanded) {
                        openFolders.push_back(keyPath);
                        filesDirty = true;
                    } else if (entry.directory && key == VK_LEFT && entry.expanded) {
                        const auto found = std::find(openFolders.begin(), openFolders.end(), keyPath);
                        if (found != openFolders.end()) {
                            openFolders.erase(found);
                        }
                        filesDirty = true;
                    } else if (key == VK_LEFT) {
                        for (int parent = index - 1; parent >= 0; --parent) {
                            if (fileEntries[static_cast<std::size_t>(parent)].depth < entry.depth) {
                                selectExplorerOnly(fileEntries[static_cast<std::size_t>(parent)].path.generic_string());
                                revealExplorer(selectedFile);
                                break;
                            }
                        }
                    } else if (key == VK_RIGHT && index + 1 < static_cast<int>(fileEntries.size()) &&
                               fileEntries[static_cast<std::size_t>(index + 1)].depth > entry.depth) {
                        selectExplorerOnly(fileEntries[static_cast<std::size_t>(index + 1)].path.generic_string());
                        revealExplorer(selectedFile);
                    }
                }
                paintDirty = true;
                return true;
            }
            if (key == VK_RETURN && !selectedFile.empty()) {
                layoutChrome(clientWidth, clientHeight);
                const int index = explorerIndex();
                if (index >= 0) {
                    FileEntry& entry = fileEntries[static_cast<std::size_t>(index)];
                    if (entry.directory) {
                        const std::string keyPath = entry.path.generic_string();
                        const auto found = std::find(openFolders.begin(), openFolders.end(), keyPath);
                        if (found == openFolders.end()) {
                            openFolders.push_back(keyPath);
                        } else {
                            openFolders.erase(found);
                        }
                        filesDirty = true;
                    } else {
                        const std::string extension = LowerCopy(entry.path.extension().string());
                        if (extension == ".xml" && document.LoadFile(entry.path)) {
                            selection.clear();
                            selections.clear();
                            rebuildPreview();
                        }
                    }
                }
                paintDirty = true;
                return true;
            }
        }
        if (control && key == 'A' && !explorerActive) {
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
        if (control && editTarget == EditTarget::NONE && !fileRenaming) {
            const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            if (shift && key == 'N') {
                applyAppAction(12);
                paintDirty = true;
                return true;
            }
            if (shift && key == 'S') {
                applyAppAction(16);
                paintDirty = true;
                return true;
            }
            if (!shift && key == 'N') {
                applyAppAction(10);
                paintDirty = true;
                return true;
            }
            if (!shift && key == 'O') {
                applyAppAction(13);
                paintDirty = true;
                return true;
            }
        }
        if (control && key == 'S' && (GetKeyState(VK_SHIFT) & 0x8000) == 0) {
            saveActive();
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
        if (editTarget == EditTarget::NONE && !control && !explorerActive) {
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
                        placeSelection(0.0f, y, width, height, false);
                    } else if (alt && key == VK_RIGHT) {
                        placeSelection(parentWidth - width, y, width, height, false);
                    } else if (alt && key == VK_UP) {
                        placeSelection(x, 0.0f, width, height, false);
                    } else if (alt && key == VK_DOWN) {
                        placeSelection(x, parentHeight - height, width, height, false);
                    } else if (key == VK_LEFT) {
                        placeSelection(x - step, y, width, height, false);
                    } else if (key == VK_RIGHT) {
                        placeSelection(x + step, y, width, height, false);
                    } else if (key == VK_UP) {
                        placeSelection(x, y - step, width, height, false);
                    } else if (key == VK_DOWN) {
                        placeSelection(x, y + step, width, height, false);
                    } else if (key == 'C') {
                        placeSelection((parentWidth - width) * 0.5f, (parentHeight - height) * 0.5f, width, height, false);
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
            if (!editScope.empty() && editTarget == EditTarget::NONE) {
                editScope.clear();
                paintDirty = true;
                return true;
            }
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
        if (editTarget == EditTarget::LIBRARY) {
            if (key == VK_BACK && !libraryQuery.empty()) {
                do {
                    libraryQuery.pop_back();
                } while (!libraryQuery.empty() && (static_cast<unsigned char>(libraryQuery.back()) & 0xC0) == 0x80);
                libraryScroll = 0;
                paintDirty = true;
            }
            return true;
        }
        if (editTarget == EditTarget::ASSET) {
            if (key == VK_BACK && !assetQuery.empty()) {
                do {
                    assetQuery.pop_back();
                } while (!assetQuery.empty() && (static_cast<unsigned char>(assetQuery.back()) & 0xC0) == 0x80);
                assetFilterReady = false;
                paintDirty = true;
            }
            return true;
        }
        if (editTarget == EditTarget::VARIABLE) {
            if (key == VK_BACK && !variableQuery.empty()) {
                do {
                    variableQuery.pop_back();
                } while (!variableQuery.empty() && (static_cast<unsigned char>(variableQuery.back()) & 0xC0) == 0x80);
                variableScroll = 0;
                paintDirty = true;
            }
            return true;
        }
        if (editTarget == EditTarget::VARIABLE_VALUE) {
            if (key == VK_BACK && !variableDraft.empty()) {
                do {
                    variableDraft.pop_back();
                } while (!variableDraft.empty() && (static_cast<unsigned char>(variableDraft.back()) & 0xC0) == 0x80);
            } else if (key == VK_RETURN) {
                finishEdit();
                editTarget = EditTarget::NONE;
                paintDirty = true;
                return true;
            } else {
                return true;
            }
            for (const VariableHit& row : variableRows) {
                if (!row.header && row.name == variableSelected && !variableDraft.empty()) {
                    remember();
                    document.SetToken(row.group, row.name, variableDraft);
                    rebuildPreview();
                }
            }
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
        if (editorFocused && activeTab >= 0 && !workTabs[static_cast<std::size_t>(activeTab)].designer &&
            !workTabs[static_cast<std::size_t>(activeTab)].binary) {
            WorkTab& tab = workTabs[static_cast<std::size_t>(activeTab)];
            if (tab.caret > tab.editorText.size()) {
                tab.caret = tab.editorText.size();
            }
            tab.editorText.insert(tab.caret, utf8);
            tab.caret += utf8.size();
            tab.dirty = true;
            paintDirty = true;
            return;
        }
        if (fileRenaming) {
            if (character >= 32 && renameDraft.size() < 180) {
                if (renameReplace) {
                    renameDraft.clear();
                    renameReplace = false;
                }
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
        if (editTarget == EditTarget::LIBRARY) {
            if (libraryQuery.size() < 40) {
                libraryQuery += utf8;
                libraryScroll = 0;
                paintDirty = true;
            }
            return;
        }
        if (editTarget == EditTarget::ASSET) {
            if (assetQuery.size() < 40) {
                assetQuery += utf8;
                assetFilterReady = false;
                paintDirty = true;
            }
            return;
        }
        if (editTarget == EditTarget::VARIABLE) {
            if (variableQuery.size() < 40) {
                variableQuery += utf8;
                variableScroll = 0;
                paintDirty = true;
            }
            return;
        }
        if (editTarget == EditTarget::VARIABLE_VALUE) {
            if (variableDraft.size() < 64) {
                variableDraft += utf8;
                for (const VariableHit& row : variableRows) {
                    if (!row.header && row.name == variableSelected) {
                        remember();
                        document.SetToken(row.group, row.name, variableDraft);
                        rebuildPreview();
                    }
                }
                paintDirty = true;
            }
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
        for (int index = 0; index < static_cast<int>(menuTitles.size()); ++index) {
            const HitRow& title = menuTitles[static_cast<std::size_t>(index)];
            const bool open = openMenu == index;
            const bool hovered = menuTitleHover == index;
            if (open || hovered) {
                const Rect pill{title.rect.x + 2.0f, 4.0f, title.rect.width - 4.0f, TOOLBAR_HEIGHT - 8.0f};
                renderer.FillRectangle(pill, open ? COLOR_SELECTED : COLOR_HOVER, 6.0f);
            }
            DrawLabel(renderer, title.name, title.rect, COLOR_INK, 13.0f, TextAlign::CENTER);
        }
        renderer.FillRectangle(runButton, runHover ? COLOR_NAV_HOVER : COLOR_NAV, 6.0f);
        FillTriangle(renderer, runButton.x + 12.0f, runButton.y + 6.0f, runButton.x + 12.0f, runButton.y + 18.0f, runButton.x + 22.0f, runButton.y + 12.0f, COLOR_INK);
        DrawLabel(renderer, "Run", Rect{runButton.x + 24.0f, runButton.y, runButton.width - 28.0f, runButton.height}, COLOR_INK, 13.0f, TextAlign::START);

        renderer.FillRectangle(iconSidebar, COLOR_PANEL, 0.0f);
        renderer.FillRectangle(fullSidebar, COLOR_PANEL, 0.0f);
        renderer.FillRectangle(inspector, COLOR_PANEL, 0.0f);
        renderer.FillRectangle(Rect{ICON_SIDEBAR - 1.0f, TOOLBAR_HEIGHT, 1.0f, iconSidebar.height}, COLOR_LINE, 0.0f);
        renderer.FillRectangle(Rect{LEFT_WIDTH - 1.0f, TOOLBAR_HEIGHT, 1.0f, iconSidebar.height}, COLOR_LINE, 0.0f);
        renderer.FillRectangle(Rect{clientWidth - RIGHT_WIDTH, TOOLBAR_HEIGHT, 1.0f, iconSidebar.height}, COLOR_LINE, 0.0f);
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
        if (leftNav == 0 && fullSidebar.width > 1.0f && projectRoot.empty()) {
            DrawLabel(renderer, "Explorer", Rect{fullSidebar.x + 14.0f, fullSidebar.y + 8.0f, fullSidebar.width - 28.0f, 22.0f}, COLOR_MUTED, 12.0f, TextAlign::START);
            const float iconWell = 52.0f;
            const Rect well{openProjectRect.x + (openProjectRect.width - iconWell) * 0.5f, openProjectRect.y - 86.0f, iconWell, iconWell};
            renderer.FillRectangle(well, COLOR_FIELD, 14.0f);
            renderer.StrokeRectangle(well, COLOR_LINE, 1.0f, 14.0f);
            DrawFolderGlyph(renderer, well.x + 18.0f, well.y + 16.0f, false, ICON_FOLDER);
            DrawLabel(renderer, "No folder open", Rect{fullSidebar.x + 16.0f, well.y + well.height + 12.0f, fullSidebar.width - 32.0f, 20.0f}, COLOR_INK, 14.0f, TextAlign::CENTER);
            DrawLabel(renderer, "Choose a project folder", Rect{fullSidebar.x + 16.0f, well.y + well.height + 32.0f, fullSidebar.width - 32.0f, 18.0f}, COLOR_MUTED, 12.0f,
                      TextAlign::CENTER);
            renderer.FillRectangle(openProjectRect, openProjectHover ? COLOR_NAV_HOVER : COLOR_NAV, 8.0f);
            DrawFolderGlyph(renderer, openProjectRect.x + 16.0f, openProjectRect.y + 12.0f, false, ICON_PAPER);
            DrawLabel(renderer, "Open Project", Rect{openProjectRect.x + 36.0f, openProjectRect.y, openProjectRect.width - 48.0f, openProjectRect.height}, ICON_PAPER, 13.0f,
                      TextAlign::CENTER);
        } else if (leftNav == 0 && fullSidebar.width > 1.0f) {
            DrawLabel(renderer, "Explorer", Rect{fullSidebar.x + 14.0f, fullSidebar.y + 8.0f, 72.0f, 22.0f}, COLOR_MUTED, 12.0f, TextAlign::START);
            const std::string scopeName = fileDestination().filename().string();
            DrawLabel(renderer, scopeName.empty() ? "Project" : scopeName, Rect{fullSidebar.x + 86.0f, fullSidebar.y + 8.0f, std::max(0.0f, fileNewRect.x - fullSidebar.x - 94.0f), 22.0f},
                      COLOR_MUTED, 11.0f, TextAlign::START);
            auto paintAction = [&](const Rect& rect, int kind, int action, Color color) {
                if (fileActionHover == action) {
                    renderer.FillRectangle(rect, COLOR_HOVER, 8.0f);
                }
                if (kind == 4) {
                    DrawExplorerIcon(renderer, rect.x + 6.0f, rect.y + 6.0f, kind, color);
                } else {
                    DrawShellIcon(renderer, rect.x + 6.0f, rect.y + 6.0f, kind == 3, false, kind == 3 ? std::filesystem::path("folder") : std::filesystem::path("file.xml"));
                }
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
                const std::string rowKey = entry.path.generic_string();
                const bool primary = entry.pending || rowKey == selectedFile;
                const bool selected = entry.pending || explorerSelected(rowKey);
                const bool hovered = fileHover == index;
                if (selected) {
                    renderer.FillRectangle(entry.rect, COLOR_SELECTED, 6.0f);
                } else if (hovered) {
                    renderer.FillRectangle(entry.rect, COLOR_HOVER, 6.0f);
                }
                if (primary && selected) {
                    renderer.FillRectangle(Rect{entry.rect.x, entry.rect.y + 4.0f, 2.0f, entry.rect.height - 8.0f}, COLOR_NAV, 1.0f);
                }
                const float indent = 10.0f + static_cast<float>(entry.depth) * 14.0f;
                if (entry.directory) {
                    const float twistX = entry.twist.x + 4.0f;
                    const float twistY = entry.twist.y + 4.0f;
                    if (entry.expanded) {
                        DrawIconLine(renderer, twistX, twistY + 1.0f, twistX + 4.0f, twistY + 5.0f, COLOR_MUTED);
                        DrawIconLine(renderer, twistX + 4.0f, twistY + 5.0f, twistX + 8.0f, twistY + 1.0f, COLOR_MUTED);
                    } else {
                        DrawIconLine(renderer, twistX, twistY, twistX + 4.0f, twistY + 4.0f, COLOR_MUTED);
                        DrawIconLine(renderer, twistX + 4.0f, twistY + 4.0f, twistX, twistY + 8.0f, COLOR_MUTED);
                    }
                }
                DrawShellIcon(renderer, fullSidebar.x + indent + 16.0f, entry.rect.y + 5.0f, entry.directory, entry.expanded, entry.path);
                Rect label{fullSidebar.x + indent + 36.0f, entry.rect.y, std::max(0.0f, entry.rect.width - indent - 30.0f), entry.rect.height};
                if (fileRenaming && primary) {
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
        } else if (leftNav == 2 && fullSidebar.width > 1.0f) {
            DrawLabel(renderer, "Library", Rect{fullSidebar.x + 14.0f, fullSidebar.y + 8.0f, fullSidebar.width - 28.0f, 22.0f}, COLOR_MUTED, 12.0f, TextAlign::START);
            const bool searchActive = editTarget == EditTarget::LIBRARY;
            renderer.FillRectangle(librarySearchRect, COLOR_FIELD, 8.0f);
            renderer.StrokeRectangle(librarySearchRect, searchActive ? COLOR_SELECT : COLOR_LINE, 1.0f, 8.0f);
            Rect searchText = librarySearchRect;
            searchText.x += 12.0f;
            searchText.width = std::max(0.0f, searchText.width - 20.0f);
            if (libraryQuery.empty() && !searchActive) {
                DrawLabel(renderer, "Search components", searchText, COLOR_MUTED, 13.0f, TextAlign::START);
            } else {
                DrawLabel(renderer, libraryQuery, searchText, COLOR_INK, 13.0f, TextAlign::START);
                if (searchActive) {
                    const Size measured = renderer.MeasureText(libraryQuery, 13.0f, 100000.0f);
                    renderer.FillRectangle(Rect{searchText.x + measured.width + 1.0f, librarySearchRect.y + 8.0f, 1.0f, librarySearchRect.height - 16.0f}, COLOR_INK, 0.0f);
                }
            }
            const Rect list{fullSidebar.x, fullSidebar.y + 76.0f, fullSidebar.width, std::max(0.0f, fullSidebar.height - 76.0f)};
            renderer.PushClip(list);
            bool anyCard = false;
            for (int index = 0; index < static_cast<int>(libraryRows.size()); ++index) {
                const HitRow& row = libraryRows[static_cast<std::size_t>(index)];
                if (row.rect.y + row.rect.height < list.y || row.rect.y > list.y + list.height) {
                    continue;
                }
                if (row.depth < 0) {
                    DrawLabel(renderer, row.name, row.rect, COLOR_MUTED, 11.0f, TextAlign::START);
                    continue;
                }
                anyCard = true;
                const bool hovered = libraryHover == index;
                renderer.FillRectangle(row.rect, hovered ? COLOR_HOVER : COLOR_FIELD, 10.0f);
                renderer.StrokeRectangle(row.rect, hovered ? COLOR_SELECT : COLOR_LINE, 1.0f, 10.0f);
                const float wellSize = std::min(44.0f, row.rect.width - 24.0f);
                const Rect well{row.rect.x + (row.rect.width - wellSize) * 0.5f, row.rect.y + 10.0f, wellSize, wellSize};
                renderer.FillRectangle(well, COLOR_CHROME, 8.0f);
                renderer.StrokeRectangle(well, COLOR_LINE, 1.0f, 8.0f);
                DrawLibraryGlyph(renderer, well, row.name);
                DrawLabel(renderer, row.name, Rect{row.rect.x + 6.0f, row.rect.y + row.rect.height - 26.0f, row.rect.width - 12.0f, 18.0f}, COLOR_INK, 12.0f,
                          TextAlign::CENTER);
            }
            if (!anyCard) {
                DrawLabel(renderer, "No components", Rect{fullSidebar.x + 16.0f, list.y + 28.0f, fullSidebar.width - 32.0f, 20.0f}, COLOR_MUTED, 13.0f, TextAlign::CENTER);
            }
            renderer.PopClip();
        } else if (leftNav == 3 && fullSidebar.width > 1.0f) {
            DrawLabel(renderer, "Assets", Rect{fullSidebar.x + 14.0f, fullSidebar.y + 8.0f, 72.0f, 22.0f}, COLOR_MUTED, 12.0f, TextAlign::START);
            IconPack& activePack = assetPack == 1 ? countryPack : iconPack;
            auto drawPack = [&](const Rect& rect, const char* label, bool active) {
                renderer.FillRectangle(rect, active ? COLOR_SELECTED : COLOR_FIELD, 8.0f);
                renderer.StrokeRectangle(rect, active ? COLOR_SELECT : COLOR_LINE, 1.0f, 8.0f);
                DrawLabel(renderer, label, rect, active ? COLOR_INK : COLOR_MUTED, 12.0f, TextAlign::CENTER);
            };
            drawPack(assetPackIcons, "Icons", assetPack == 0);
            drawPack(assetPackFlags, "Flags", assetPack == 1);
            const bool searchActive = editTarget == EditTarget::ASSET;
            renderer.FillRectangle(assetSearchRect, COLOR_FIELD, 8.0f);
            renderer.StrokeRectangle(assetSearchRect, searchActive ? COLOR_SELECT : COLOR_LINE, 1.0f, 8.0f);
            Rect searchText = assetSearchRect;
            searchText.x += 12.0f;
            searchText.width = std::max(0.0f, searchText.width - 20.0f);
            if (assetQuery.empty() && !searchActive) {
                DrawLabel(renderer, assetPack == 1 ? "Search flags" : "Search icons", searchText, COLOR_MUTED, 13.0f, TextAlign::START);
            } else {
                DrawLabel(renderer, assetQuery, searchText, COLOR_INK, 13.0f, TextAlign::START);
                if (searchActive) {
                    const Size measured = renderer.MeasureText(assetQuery, 13.0f, 100000.0f);
                    renderer.FillRectangle(Rect{searchText.x + measured.width + 1.0f, assetSearchRect.y + 8.0f, 1.0f, assetSearchRect.height - 16.0f}, COLOR_INK, 0.0f);
                }
            }
            const float previewHeight = assetSelected >= 0 ? 96.0f : 0.0f;
            const Rect list{fullSidebar.x, fullSidebar.y + 108.0f, fullSidebar.width, std::max(0.0f, fullSidebar.height - 108.0f - previewHeight)};
            renderer.PushClip(list);
            if (!activePack.Loaded()) {
                DrawLabel(renderer, assetPack == 1 ? "RoboCountry.pack was not found" : "RoboIcons.pack was not found", Rect{fullSidebar.x + 16.0f, list.y + 28.0f, fullSidebar.width - 32.0f, 20.0f}, COLOR_MUTED, 13.0f, TextAlign::CENTER);
            } else if (assetRows.empty()) {
                DrawLabel(renderer, assetPack == 1 ? "No flags" : "No icons", Rect{fullSidebar.x + 16.0f, list.y + 28.0f, fullSidebar.width - 32.0f, 20.0f}, COLOR_MUTED, 13.0f, TextAlign::CENTER);
            }
            for (int index = 0; index < static_cast<int>(assetRows.size()); ++index) {
                const HitRow& row = assetRows[static_cast<std::size_t>(index)];
                if (row.rect.y + row.rect.height < list.y || row.rect.y > list.y + list.height) {
                    continue;
                }
                const bool selected = row.depth == assetSelected;
                const bool hovered = assetHover == index;
                renderer.FillRectangle(row.rect, selected ? COLOR_SELECTED : hovered ? COLOR_HOVER : COLOR_FIELD, 10.0f);
                renderer.StrokeRectangle(row.rect, selected || hovered ? COLOR_SELECT : COLOR_LINE, 1.0f, 10.0f);
                const float wellWidth = std::min(assetPack == 1 ? 56.0f : 44.0f, row.rect.width - 24.0f);
                const float wellHeight = assetPack == 1 ? wellWidth * 0.75f : wellWidth;
                const Rect well{row.rect.x + (row.rect.width - wellWidth) * 0.5f, row.rect.y + 10.0f, wellWidth, wellHeight};
                renderer.FillRectangle(well, COLOR_CHROME, 4.0f);
                if (const IconPreview* icon = activePack.Preview(row.depth)) {
                    const float inset = 2.0f;
                    const char* prefix = assetPack == 1 ? "roboflag:" : "roboicon:";
                    renderer.DrawCachedImage(std::string(prefix) + row.name, Rect{well.x + inset, well.y + inset, well.width - inset * 2.0f, well.height - inset * 2.0f}, icon->bgra.data(), icon->width, icon->height);
                }
                const char* title = assetPack == 1 ? CountryTitle(row.name) : nullptr;
                DrawLabel(renderer, title != nullptr ? title : row.name.c_str(), Rect{row.rect.x + 6.0f, row.rect.y + row.rect.height - 26.0f, row.rect.width - 12.0f, 18.0f}, COLOR_INK, 11.0f, TextAlign::CENTER);
            }
            renderer.PopClip();
            if (assetSelected >= 0 && assetSelected < activePack.Count()) {
                const Rect previewBar{fullSidebar.x + 8.0f, fullSidebar.y + fullSidebar.height - 88.0f, std::max(0.0f, fullSidebar.width - 16.0f), 80.0f};
                renderer.FillRectangle(previewBar, COLOR_FIELD, 10.0f);
                renderer.StrokeRectangle(previewBar, COLOR_LINE, 1.0f, 10.0f);
                const Rect large{previewBar.x + 12.0f, previewBar.y + 12.0f, assetPack == 1 ? 72.0f : 56.0f, 56.0f};
                renderer.FillRectangle(large, COLOR_CHROME, 8.0f);
                if (const IconPreview* icon = activePack.Preview(assetSelected)) {
                    const char* prefix = assetPack == 1 ? "roboflag:" : "roboicon:";
                    renderer.DrawCachedImage(std::string(prefix) + activePack.Name(assetSelected), Rect{large.x + 6.0f, large.y + 6.0f, large.width - 12.0f, large.height - 12.0f}, icon->bgra.data(), icon->width, icon->height);
                }
                const char* title = assetPack == 1 ? CountryTitle(activePack.Name(assetSelected)) : nullptr;
                const float labelX = large.x + large.width + 12.0f;
                DrawLabel(renderer, title != nullptr ? title : activePack.Name(assetSelected).c_str(), Rect{labelX, previewBar.y + 14.0f, std::max(0.0f, previewBar.width - (labelX - previewBar.x) - 80.0f), 20.0f}, COLOR_INK, 13.0f, TextAlign::START);
                DrawLabel(renderer, assetPack == 1 ? activePack.Name(assetSelected).c_str() : "Vector layers", Rect{labelX, previewBar.y + 34.0f, std::max(0.0f, previewBar.width - (labelX - previewBar.x) - 80.0f), 16.0f}, COLOR_MUTED, 11.0f, TextAlign::START);
                renderer.FillRectangle(assetPlace, COLOR_ACCENT, 6.0f);
                DrawLabel(renderer, "Place", assetPlace, COLOR_CHROME, 12.0f, TextAlign::CENTER);
            }
        } else if (leftNav == 4 && fullSidebar.width > 1.0f) {
            DrawLabel(renderer, "Variables", Rect{fullSidebar.x + 14.0f, fullSidebar.y + 8.0f, 90.0f, 22.0f}, COLOR_MUTED, 12.0f, TextAlign::START);
            renderer.FillRectangle(variableAddRect, COLOR_FIELD, 6.0f);
            renderer.StrokeRectangle(variableAddRect, COLOR_LINE, 1.0f, 6.0f);
            DrawLabel(renderer, "Add", variableAddRect, COLOR_INK, 12.0f, TextAlign::CENTER);
            const bool searchActive = editTarget == EditTarget::VARIABLE;
            renderer.FillRectangle(variableSearchRect, COLOR_FIELD, 8.0f);
            renderer.StrokeRectangle(variableSearchRect, searchActive ? COLOR_SELECT : COLOR_LINE, 1.0f, 8.0f);
            Rect searchText = variableSearchRect;
            searchText.x += 12.0f;
            searchText.width = std::max(0.0f, searchText.width - 20.0f);
            if (variableQuery.empty() && !searchActive) {
                DrawLabel(renderer, "Search variables", searchText, COLOR_MUTED, 13.0f, TextAlign::START);
            } else {
                DrawLabel(renderer, variableQuery, searchText, COLOR_INK, 13.0f, TextAlign::START);
                if (searchActive) {
                    const Size measured = renderer.MeasureText(variableQuery, 13.0f, 100000.0f);
                    renderer.FillRectangle(Rect{searchText.x + measured.width + 1.0f, variableSearchRect.y + 8.0f, 1.0f, variableSearchRect.height - 16.0f}, COLOR_INK, 0.0f);
                }
            }
            const float footer = variableSelected.empty() ? 0.0f : 86.0f;
            const Rect list{fullSidebar.x, fullSidebar.y + 76.0f, fullSidebar.width, std::max(0.0f, fullSidebar.height - 76.0f - footer)};
            renderer.PushClip(list);
            auto swatchOf = [](const std::string& text) -> Color {
                auto nibble = [](char character) -> int {
                    if (character >= '0' && character <= '9') {
                        return character - '0';
                    }
                    if (character >= 'a' && character <= 'f') {
                        return character - 'a' + 10;
                    }
                    if (character >= 'A' && character <= 'F') {
                        return character - 'A' + 10;
                    }
                    return -1;
                };
                if (text.size() != 7 && text.size() != 9) {
                    return COLOR_LINE;
                }
                if (text.front() != '#') {
                    return COLOR_LINE;
                }
                int channels[4] = {0, 0, 0, 255};
                const int digits = text.size() == 7 ? 3 : 4;
                for (int index = 0; index < digits; ++index) {
                    const int high = nibble(text[static_cast<std::size_t>(1 + index * 2)]);
                    const int low = nibble(text[static_cast<std::size_t>(2 + index * 2)]);
                    if (high < 0 || low < 0) {
                        return COLOR_LINE;
                    }
                    channels[index] = high * 16 + low;
                }
                return Color{channels[0] / 255.0f, channels[1] / 255.0f, channels[2] / 255.0f, channels[3] / 255.0f};
            };
            for (const VariableHit& row : variableRows) {
                if (row.rect.y + row.rect.height < list.y || row.rect.y > list.y + list.height) {
                    continue;
                }
                if (row.header) {
                    DrawLabel(renderer, row.name, Rect{row.rect.x + 4.0f, row.rect.y, row.rect.width - 8.0f, row.rect.height}, COLOR_MUTED, 11.0f, TextAlign::START);
                    continue;
                }
                const bool selected = row.name == variableSelected;
                if (selected) {
                    renderer.FillRectangle(row.rect, COLOR_SELECTED, 6.0f);
                }
                if (row.group == "color") {
                    const Rect chip{row.rect.x + 8.0f, row.rect.y + 7.0f, 16.0f, 16.0f};
                    renderer.FillRectangle(chip, swatchOf(row.value), 4.0f);
                    renderer.StrokeRectangle(chip, COLOR_LINE, 1.0f, 4.0f);
                }
                const float textX = row.group == "color" ? 32.0f : 8.0f;
                DrawLabel(renderer, row.name, Rect{row.rect.x + textX, row.rect.y, std::max(0.0f, row.rect.width - textX - 72.0f), row.rect.height}, selected ? COLOR_INK : COLOR_MUTED, 12.0f, TextAlign::START);
                DrawLabel(renderer, row.value, Rect{row.rect.x + row.rect.width - 68.0f, row.rect.y, 60.0f, row.rect.height}, COLOR_MUTED, 11.0f, TextAlign::END);
            }
            renderer.PopClip();
            if (!variableSelected.empty()) {
                const Rect bar{fullSidebar.x + 8.0f, fullSidebar.y + fullSidebar.height - 78.0f, std::max(0.0f, fullSidebar.width - 16.0f), 70.0f};
                renderer.FillRectangle(bar, COLOR_FIELD, 10.0f);
                renderer.StrokeRectangle(bar, COLOR_LINE, 1.0f, 10.0f);
                std::string note;
                for (const VariableHit& row : variableRows) {
                    if (!row.header && row.name == variableSelected) {
                        note = row.note.empty() ? row.group : row.note;
                    }
                }
                DrawLabel(renderer, note, Rect{bar.x + 8.0f, bar.y + 6.0f, std::max(0.0f, bar.width - 16.0f), 18.0f}, COLOR_MUTED, 11.0f, TextAlign::START);
                const bool valueActive = editTarget == EditTarget::VARIABLE_VALUE;
                renderer.FillRectangle(variableValueRect, COLOR_CHROME, 6.0f);
                renderer.StrokeRectangle(variableValueRect, valueActive ? COLOR_SELECT : COLOR_LINE, 1.0f, 6.0f);
                Rect valueText = variableValueRect;
                valueText.x += 8.0f;
                valueText.width = std::max(0.0f, valueText.width - 12.0f);
                DrawLabel(renderer, variableDraft, valueText, COLOR_INK, 12.0f, TextAlign::START);
                renderer.FillRectangle(variableRemoveRect, COLOR_FIELD, 6.0f);
                renderer.StrokeRectangle(variableRemoveRect, COLOR_LINE, 1.0f, 6.0f);
                DrawLabel(renderer, "Remove", variableRemoveRect, COLOR_INK, 11.0f, TextAlign::CENTER);
            }
        }
        renderer.FillRectangle(tabBar, COLOR_PANEL, 0.0f);
        renderer.FillRectangle(Rect{tabBar.x, tabBar.y + tabBar.height - 1.0f, tabBar.width, 1.0f}, COLOR_LINE, 0.0f);
        for (int index = 0; index < static_cast<int>(tabRows.size()); ++index) {
            const HitRow& row = tabRows[static_cast<std::size_t>(index)];
            const WorkTab& tab = workTabs[static_cast<std::size_t>(index)];
            const bool active = index == activeTab;
            const bool hovered = index == tabHover;
            if (active) {
                renderer.FillRectangle(row.rect, COLOR_FIELD, 8.0f);
            } else if (hovered) {
                renderer.FillRectangle(row.rect, COLOR_HOVER, 8.0f);
            }
            if (tab.designer && active) {
                renderer.FillRectangle(Rect{row.rect.x + 10.0f, row.rect.y + row.rect.height - 2.0f, std::max(0.0f, row.rect.width - 20.0f), 2.0f}, COLOR_ACCENT, 1.0f);
            }
            DrawShellIcon(renderer, row.rect.x + 8.0f, row.rect.y + (row.rect.height - 16.0f) * 0.5f, false, false, std::filesystem::path(tab.path));
            Rect label{row.rect.x + 28.0f, row.rect.y, std::max(0.0f, row.rect.width - 50.0f), row.rect.height};
            DrawLabel(renderer, row.name, label, active || hovered ? COLOR_INK : COLOR_MUTED, 12.0f, TextAlign::START);
            if (tab.dirty) {
                renderer.FillRectangle(Rect{row.twist.x - 8.0f, row.rect.y + (row.rect.height - 6.0f) * 0.5f, 6.0f, 6.0f}, COLOR_ACCENT, 3.0f);
            }
            if (active || hovered) {
                const float cx = row.twist.x + 6.0f;
                const float cy = row.twist.y + 6.0f;
                const Color closeColor = tabCloseHover == index ? COLOR_INK : COLOR_MUTED;
                DrawStroke(renderer, cx - 3.5f, cy - 3.5f, cx + 3.5f, cy + 3.5f, closeColor, 1.35f);
                DrawStroke(renderer, cx + 3.5f, cy - 3.5f, cx - 3.5f, cy + 3.5f, closeColor, 1.35f);
            }
        }
        if (surfaceDesigner()) {
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
        const float rulerInset = showRulers ? RULER_SIZE : 0.0f;
        scopeDoneRect = {};
        scopeMainRect = {};
        scopeApplyRect = {};
        if (!editScope.empty()) {
            const XmlNode* scoped = document.NodeAt(editScope);
            const bool master = scoped != nullptr && scoped->name == "component";
            const std::string componentName = master ? document.Attribute(editScope, "name") : document.Attribute(editScope, "component");
            const std::string label = master ? "Editing " + componentName + " component" : "Editing " + (componentName.empty() ? "component" : componentName) + " instance";
            const Rect bar{canvas.x + rulerInset + 8.0f, canvas.y + rulerInset + 8.0f, std::min(520.0f, std::max(0.0f, canvas.width - rulerInset - 16.0f)), 32.0f};
            renderer.FillRectangle(bar, COLOR_PANEL, 8.0f);
            renderer.StrokeRectangle(bar, COLOR_LINE, 1.0f, 8.0f);
            DrawLabel(renderer, label, Rect{bar.x + 10.0f, bar.y, std::max(0.0f, bar.width - 280.0f), bar.height}, COLOR_INK, 12.0f, TextAlign::START);
            scopeDoneRect = Rect{bar.x + bar.width - 64.0f, bar.y + 4.0f, 56.0f, 24.0f};
            renderer.FillRectangle(scopeDoneRect, COLOR_FIELD, 6.0f);
            DrawLabel(renderer, "Done", scopeDoneRect, COLOR_INK, 12.0f, TextAlign::CENTER);
            if (master) {
                scopeApplyRect = Rect{scopeDoneRect.x - 132.0f, scopeDoneRect.y, 124.0f, 24.0f};
                renderer.FillRectangle(scopeApplyRect, COLOR_NAV, 6.0f);
                DrawLabel(renderer, "Apply instances", scopeApplyRect, COLOR_INK, 12.0f, TextAlign::CENTER);
            } else if (!componentName.empty()) {
                scopeMainRect = Rect{scopeDoneRect.x - 148.0f, scopeDoneRect.y, 140.0f, 24.0f};
                renderer.FillRectangle(scopeMainRect, COLOR_NAV, 6.0f);
                DrawLabel(renderer, "Main component", scopeMainRect, COLOR_INK, 12.0f, TextAlign::CENTER);
            }
        }
        const std::string toolLabel = std::string(EditorToolName(activeTool)) + (selections.size() > 1 ? "  " + std::to_string(selections.size()) : "");
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
        } else {
            renderer.FillRectangle(canvas, COLOR_FIELD, 0.0f);
            renderer.PushClip(canvas);
            if (activeTab < 0 || activeTab >= static_cast<int>(workTabs.size())) {
                DrawLabel(renderer, "Open a file from the explorer", Rect{canvas.x, canvas.y + canvas.height * 0.5f - 12.0f, canvas.width, 24.0f}, COLOR_MUTED, 14.0f, TextAlign::CENTER);
            } else {
                const WorkTab& tab = workTabs[static_cast<std::size_t>(activeTab)];
                if (tab.binary) {
                    DrawLabel(renderer, "This file cannot be shown as text", Rect{canvas.x, canvas.y + canvas.height * 0.5f - 12.0f, canvas.width, 24.0f}, COLOR_MUTED, 14.0f, TextAlign::CENTER);
                } else {
                    renderer.FillRectangle(Rect{canvas.x, canvas.y, 48.0f, canvas.height}, COLOR_PANEL, 0.0f);
                    renderer.FillRectangle(Rect{canvas.x + 47.0f, canvas.y, 1.0f, canvas.height}, COLOR_LINE, 0.0f);
                    int lineNumber = 0;
                    std::size_t lineStart = 0;
                    const int visible = std::max(1, static_cast<int>((canvas.height - 16.0f) / 18.0f));
                    for (std::size_t index = 0; index <= tab.editorText.size(); ++index) {
                        const bool end = index == tab.editorText.size() || tab.editorText[index] == '\n';
                        if (!end) {
                            continue;
                        }
                        if (lineNumber >= tab.scroll && lineNumber < tab.scroll + visible) {
                            const float y = canvas.y + 8.0f + static_cast<float>(lineNumber - tab.scroll) * 18.0f;
                            DrawLabel(renderer, std::to_string(lineNumber + 1), Rect{canvas.x + 4.0f, y, 40.0f, 18.0f}, COLOR_MUTED, 12.0f, TextAlign::END);
                            const std::size_t length = index - lineStart;
                            if (length > 0) {
                                DrawLabel(renderer, std::string_view(tab.editorText.data() + lineStart, length), Rect{canvas.x + 56.0f, y, std::max(0.0f, canvas.width - 64.0f), 18.0f}, COLOR_INK, 13.0f, TextAlign::START);
                            }
                            if (editorFocused && tab.caret >= lineStart && tab.caret <= index) {
                                const Size measured = renderer.MeasureText(std::string_view(tab.editorText.data() + lineStart, tab.caret - lineStart), 13.0f, 100000.0f);
                                renderer.FillRectangle(Rect{canvas.x + 56.0f + measured.width, y + 2.0f, 1.0f, 14.0f}, COLOR_ACCENT, 0.0f);
                            }
                        }
                        if (index == tab.editorText.size()) {
                            break;
                        }
                        ++lineNumber;
                        lineStart = index + 1;
                    }
                }
            }
            renderer.PopClip();
        }

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
        if (appMenuOpen && !appMenuRows.empty()) {
            const Rect first = appMenuRows.front().rect;
            const Rect last = appMenuRows.back().rect;
            const Rect popup{first.x - 6.0f, TOOLBAR_HEIGHT, first.width + 12.0f, last.y + last.height - TOOLBAR_HEIGHT + 8.0f};
            renderer.FillRectangle(popup, COLOR_FIELD, 10.0f);
            renderer.StrokeRectangle(popup, COLOR_LINE, 1.0f, 10.0f);
            for (int index = 0; index < static_cast<int>(appMenuRows.size()); ++index) {
                const HitRow& row = appMenuRows[static_cast<std::size_t>(index)];
                if (row.depth < 0) {
                    renderer.FillRectangle(Rect{popup.x + 10.0f, row.rect.y + 4.0f, popup.width - 20.0f, 1.0f}, COLOR_LINE, 0.0f);
                    continue;
                }
                if (appMenuHover == index && !row.hidden) {
                    renderer.FillRectangle(row.rect, COLOR_HOVER, 6.0f);
                }
                const Color iconColor = row.hidden ? COLOR_LINE : row.branch ? COLOR_ACCENT : COLOR_INK;
                if (row.branch) {
                    DrawMenuCheck(renderer, row.rect.x + 6.0f, row.rect.y + (row.rect.height - 14.0f) * 0.5f, COLOR_ACCENT);
                }
                DrawMenuIcon(renderer, row.rect.x + 24.0f, row.rect.y + (row.rect.height - 16.0f) * 0.5f, row.depth, iconColor);
                const std::size_t split = row.name.find('\t');
                const std::string label = split == std::string::npos ? row.name : row.name.substr(0, split);
                const std::string shortcut = split == std::string::npos ? std::string{} : row.name.substr(split + 1);
                const Color labelColor = row.hidden ? COLOR_MUTED : COLOR_INK;
                Rect text = row.rect;
                text.x += 46.0f;
                text.width = std::max(0.0f, row.rect.width - 56.0f);
                DrawLabel(renderer, label, text, labelColor, 13.0f, TextAlign::START);
                if (!shortcut.empty()) {
                    DrawLabel(renderer, shortcut, text, row.hidden ? COLOR_LINE : COLOR_MUTED, 12.0f, TextAlign::END);
                }
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
                DrawMenuIcon(renderer, row.rect.x + 8.0f, row.rect.y + (row.rect.height - 16.0f) * 0.5f, row.depth, labelColor);
                Rect text = row.rect;
                text.x += 30.0f;
                text.width = std::max(0.0f, text.width - 36.0f);
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

    LogInfo("RoboUIDesigner is open. Ctrl+S saves. F5 runs the document. Esc clears the editor focus.");
    bool running = true;
    unsigned long long lastFrame = GetTickCount64();
    while (running) {
        window.Pump(running);
        if (!running) {
            break;
        }
        const unsigned long long now = GetTickCount64();
        float seconds = static_cast<float>(now - lastFrame) / 1000.0f;
        lastFrame = now;
        if (seconds <= 0.0f || seconds > 0.1f) {
            seconds = 1.0f / 60.0f;
        }
        const bool animated = preview != nullptr && preview->Tick(seconds);
        if (animated) {
            paintDirty = true;
        }
        if (paintDirty) {
            if (!paint()) {
                return 1;
            }
            paintDirty = false;
        }
        if (animated) {
            Sleep(12);
        } else {
            window.WaitForMessage();
        }
    }
    return 0;
}

}  // namespace Runtime
