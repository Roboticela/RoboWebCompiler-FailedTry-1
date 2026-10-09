#pragma once

namespace Runtime {

enum class EditorTool {
    SELECT,
    FRAME,
    RECTANGLE,
    ELLIPSE,
    POLYGON,
    STAR,
    TEXT,
    RICH_TEXT,
    IMAGE,
    VIDEO,
    SVG,
    LINE,
    ARROW,
    BOOLEAN,
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
    case EditorTool::POLYGON:
        return "Polygon";
    case EditorTool::STAR:
        return "Star";
    case EditorTool::TEXT:
        return "Text";
    case EditorTool::RICH_TEXT:
        return "Rich Text";
    case EditorTool::IMAGE:
        return "Image";
    case EditorTool::VIDEO:
        return "Video";
    case EditorTool::SVG:
        return "Svg";
    case EditorTool::LINE:
        return "Line";
    case EditorTool::ARROW:
        return "Arrow";
    case EditorTool::BOOLEAN:
        return "Boolean";
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
    case EditorTool::POLYGON:
        return "polygon";
    case EditorTool::STAR:
        return "star";
    case EditorTool::TEXT:
        return "text";
    case EditorTool::RICH_TEXT:
        return "richtext";
    case EditorTool::IMAGE:
        return "image";
    case EditorTool::VIDEO:
        return "video";
    case EditorTool::SVG:
        return "svg";
    case EditorTool::LINE:
        return "line";
    case EditorTool::ARROW:
        return "arrow";
    case EditorTool::BOOLEAN:
        return "boolean";
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
    case EditorTool::POLYGON:
        return "P";
    case EditorTool::STAR:
        return "S";
    case EditorTool::TEXT:
        return "T";
    // The letter a text block is made with is the letter a paragraph of runs would ask for too, so
    // the tool is reached through the command list rather than by a key of its own.
    case EditorTool::RICH_TEXT:
        return "";
    case EditorTool::IMAGE:
        return "I";
    // The letter a video is named by is the letter selecting is made with, so the tool is reached
    // through the command list rather than by a key of its own, the way a vector file is.
    case EditorTool::VIDEO:
        return "";
    // The letters a vector file would be named by are already spoken for, so the tool is reached
    // through the command list rather than by a key of its own.
    case EditorTool::SVG:
        return "";
    case EditorTool::LINE:
        return "L";
    case EditorTool::ARROW:
        return "A";
    case EditorTool::BOOLEAN:
        // The letter a board is added with is already spoken for, so a boolean group is reached
        // through the command list rather than by a key of its own, the way an SVG is.
        return "";
    case EditorTool::HAND:
        return "H";
    case EditorTool::ZOOM:
        return "Z";
    }
    return "";
}

}  // namespace Runtime
