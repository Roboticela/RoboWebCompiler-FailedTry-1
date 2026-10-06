#pragma once

namespace Runtime {

enum class EditorTool {
    SELECT,
    FRAME,
    RECTANGLE,
    ELLIPSE,
    TEXT,
    IMAGE,
    LINE,
    ARROW,
    HAND,
    ZOOM,
};

inline const char* EditorToolName(EditorTool tool) {
    switch (tool) {
    case EditorTool::SELECT:
        return "Select";
    case EditorTool::FRAME:
        return "Frame";
    case EditorTool::RECTANGLE:
        return "Rectangle";
    case EditorTool::ELLIPSE:
        return "Ellipse";
    case EditorTool::TEXT:
        return "Text";
    case EditorTool::IMAGE:
        return "Image";
    case EditorTool::LINE:
        return "Line";
    case EditorTool::ARROW:
        return "Arrow";
    case EditorTool::HAND:
        return "Hand";
    case EditorTool::ZOOM:
        return "Zoom";
    }
    return "Select";
}

inline const char* EditorToolComponent(EditorTool tool) {
    switch (tool) {
    case EditorTool::FRAME:
        return "frame";
    case EditorTool::RECTANGLE:
        return "rectangle";
    case EditorTool::ELLIPSE:
        return "ellipse";
    case EditorTool::TEXT:
        return "text";
    case EditorTool::IMAGE:
        return "image";
    case EditorTool::LINE:
        return "line";
    case EditorTool::ARROW:
        return "arrow";
    case EditorTool::SELECT:
    case EditorTool::HAND:
    case EditorTool::ZOOM:
        return nullptr;
    }
    return nullptr;
}

inline const char* EditorToolShortcut(EditorTool tool) {
    switch (tool) {
    case EditorTool::SELECT:
        return "V";
    case EditorTool::FRAME:
        return "F";
    case EditorTool::RECTANGLE:
        return "R";
    case EditorTool::ELLIPSE:
        return "O";
    case EditorTool::TEXT:
        return "T";
    case EditorTool::IMAGE:
        return "I";
    case EditorTool::LINE:
        return "L";
    case EditorTool::ARROW:
        return "A";
    case EditorTool::HAND:
        return "H";
    case EditorTool::ZOOM:
        return "Z";
    }
    return "";
}

}  // namespace Runtime
