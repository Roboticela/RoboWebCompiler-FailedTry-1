#pragma once

namespace Runtime {

enum class EditorTool {
    SELECT,
    FRAME,
    TEXT,
    IMAGE,
    LINE,
    HAND,
    ZOOM,
};

inline const char* EditorToolName(EditorTool tool) {
    switch (tool) {
    case EditorTool::SELECT:
        return "Select";
    case EditorTool::FRAME:
        return "Frame";
    case EditorTool::TEXT:
        return "Text";
    case EditorTool::IMAGE:
        return "Image";
    case EditorTool::LINE:
        return "Line";
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
    case EditorTool::TEXT:
        return "text";
    case EditorTool::IMAGE:
        return "image";
    case EditorTool::LINE:
        return "line";
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
    case EditorTool::TEXT:
        return "T";
    case EditorTool::IMAGE:
        return "I";
    case EditorTool::LINE:
        return "L";
    case EditorTool::HAND:
        return "H";
    case EditorTool::ZOOM:
        return "Z";
    }
    return "";
}

}  // namespace Runtime
