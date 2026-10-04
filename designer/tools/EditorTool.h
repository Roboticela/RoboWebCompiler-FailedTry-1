#pragma once

namespace Runtime {

enum class EditorTool {
    SELECT,
    FRAME,
    HAND,
    ZOOM,
};

inline const char* EditorToolName(EditorTool tool) {
    switch (tool) {
    case EditorTool::SELECT:
        return "Select";
    case EditorTool::FRAME:
        return "Frame";
    case EditorTool::HAND:
        return "Hand";
    case EditorTool::ZOOM:
        return "Zoom";
    }
    return "Select";
}

inline const char* EditorToolShortcut(EditorTool tool) {
    switch (tool) {
    case EditorTool::SELECT:
        return "V";
    case EditorTool::FRAME:
        return "F";
    case EditorTool::HAND:
        return "H";
    case EditorTool::ZOOM:
        return "Z";
    }
    return "";
}

}  // namespace Runtime
