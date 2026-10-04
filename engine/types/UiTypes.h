#pragma once

#include "engine/math/Color.h"

#include <algorithm>
#include <cstdint>

namespace Runtime {

inline constexpr int DEFAULT_WINDOW_WIDTH = 800;
inline constexpr int DEFAULT_WINDOW_HEIGHT = 600;
inline constexpr int MAX_TREE_DEPTH = 64;
inline constexpr float DEFAULT_TEXT_FONT_SIZE = 17.0f;
inline constexpr float DEFAULT_BUTTON_FONT_SIZE = 17.0f;
inline constexpr float BUTTON_PADDING_X = 22.0f;
inline constexpr float BUTTON_PADDING_Y = 10.0f;
inline constexpr float DEFAULT_BUTTON_MIN_HEIGHT = 44.0f;
inline constexpr float DEFAULT_CORNER_RADIUS = 980.0f;
inline constexpr float UNBOUNDED_MEASURE = 100000.0f;

enum class NodeKind : std::uint8_t {
    WINDOW,
    COLUMN,
    ROW,
    FLEX,
    GRID,
    STACK,
    TEXT,
    BUTTON,
    SCROLL,
    PANEL,
    CARD,
    SPACER,
    DIVIDER,
    ABSOLUTE,
    SPLIT,
    IMAGE,
    CHECKBOX,
    SWITCH,
    RADIO,
    SLIDER,
    PROGRESS,
    TEXT_FIELD,
    SELECT,
    OPTION,
    ALERT,
    BADGE,
    LINK,
    ICON_BUTTON,
    TABS,
    TAB,
    CONTAINER,
    WRAP,
    NAVBAR,
    HEADER,
    SECTION,
    FOOTER,
    LIST,
    LIST_ITEM,
    MENU,
    MENU_ITEM,
    CLIP,
    ASPECT,
    OVERLAY,
    DIALOG,
    FORM,
    SKELETON,
    AVATAR,
    EMPTY,
    BREADCRUMB,
    PAGINATION,
};

enum class ButtonVariant : std::uint8_t {
    PRIMARY,
    SECONDARY,
    ACTION,
    GHOST,
    DANGER,
};

enum class ButtonSize : std::uint8_t {
    SM,
    MD,
    LG,
};

enum class TextRole : std::uint8_t {
    BODY,
    DISPLAY,
    H1,
    H2,
    H3,
    SUBHEAD,
    CAPTION,
};

enum class FlexDirection : std::uint8_t {
    ROW,
    COLUMN,
};

enum class JustifyContent : std::uint8_t {
    START,
    CENTER,
    END,
    SPACE_BETWEEN,
};

enum class FlexWrap : std::uint8_t {
    NO_WRAP,
    WRAP,
};

enum class TypeKind : std::uint8_t {
    STRING,
    BOOL,
    INT,
    FLOAT,
    COLOR,
    LENGTH,
    EDGES,
    FONT_SIZE,
    ALIGNMENT,
    EVENT_HANDLER,
};

enum class LengthUnit : std::uint8_t {
    PIXELS,
    PERCENT,
    FILL,
    FIT,
};

enum class Alignment : std::uint8_t {
    START,
    CENTER,
    END,
    STRETCH,
};

enum class TextAlign : std::uint8_t {
    START,
    CENTER,
    END,
};

struct Length {
    float value = 0.0f;
    LengthUnit unit = LengthUnit::FIT;

    static Length Pixels(float pixels) {
        return Length{pixels, LengthUnit::PIXELS};
    }

    static Length Percent(float percent) {
        return Length{percent, LengthUnit::PERCENT};
    }

    static Length Fill() {
        return Length{0.0f, LengthUnit::FILL};
    }

    static Length Fit() {
        return Length{0.0f, LengthUnit::FIT};
    }

    float Resolve(float available, float fit) const {
        float resolved = fit;
        switch (unit) {
        case LengthUnit::PIXELS:
            resolved = value;
            break;
        case LengthUnit::PERCENT:
            resolved = available * (value / 100.0f);
            break;
        case LengthUnit::FILL:
            resolved = available;
            break;
        case LengthUnit::FIT:
            resolved = fit;
            break;
        }
        if (resolved < 0.0f) {
            return 0.0f;
        }
        return resolved;
    }
};

struct Edges {
    Length top = Length::Pixels(0.0f);
    Length right = Length::Pixels(0.0f);
    Length bottom = Length::Pixels(0.0f);
    Length left = Length::Pixels(0.0f);

    static Edges All(Length length) {
        return Edges{length, length, length, length};
    }

    float SpanX(float available) const {
        return left.Resolve(available, 0.0f) + right.Resolve(available, 0.0f);
    }

    float SpanY(float available) const {
        return top.Resolve(available, 0.0f) + bottom.Resolve(available, 0.0f);
    }
};

inline float ClampAxis(float value, Length minimum, Length maximum, float available) {
    if (minimum.unit == LengthUnit::PIXELS || minimum.unit == LengthUnit::PERCENT) {
        value = std::max(value, minimum.Resolve(available, value));
    }
    if (maximum.unit == LengthUnit::PIXELS || maximum.unit == LengthUnit::PERCENT) {
        value = std::min(value, maximum.Resolve(available, value));
    }
    return value < 0.0f ? 0.0f : value;
}

inline float ResolveDefinite(Length length, float available, float fit, bool definite) {
    if (!definite && (length.unit == LengthUnit::FILL || length.unit == LengthUnit::PERCENT)) {
        return fit < 0.0f ? 0.0f : fit;
    }
    return length.Resolve(available, fit);
}

inline const char* TypeKindName(TypeKind kind) {
    switch (kind) {
    case TypeKind::STRING:
        return "String";
    case TypeKind::BOOL:
        return "Bool";
    case TypeKind::INT:
        return "Int";
    case TypeKind::FLOAT:
        return "Float";
    case TypeKind::COLOR:
        return "Color";
    case TypeKind::LENGTH:
        return "Length";
    case TypeKind::EDGES:
        return "Edges";
    case TypeKind::FONT_SIZE:
        return "FontSize";
    case TypeKind::ALIGNMENT:
        return "Alignment";
    case TypeKind::EVENT_HANDLER:
        return "EventHandler";
    }
    return "Unknown";
}

inline const char* NodeKindName(NodeKind kind) {
    switch (kind) {
    case NodeKind::WINDOW:
        return "window";
    case NodeKind::COLUMN:
        return "column";
    case NodeKind::ROW:
        return "row";
    case NodeKind::FLEX:
        return "flex";
    case NodeKind::GRID:
        return "grid";
    case NodeKind::STACK:
        return "stack";
    case NodeKind::TEXT:
        return "text";
    case NodeKind::BUTTON:
        return "button";
    case NodeKind::SCROLL:
        return "scroll";
    case NodeKind::PANEL:
        return "panel";
    case NodeKind::CARD:
        return "card";
    case NodeKind::SPACER:
        return "spacer";
    case NodeKind::DIVIDER:
        return "divider";
    case NodeKind::ABSOLUTE:
        return "absolute";
    case NodeKind::SPLIT:
        return "split";
    case NodeKind::IMAGE:
        return "image";
    case NodeKind::CHECKBOX:
        return "checkbox";
    case NodeKind::SWITCH:
        return "switch";
    case NodeKind::RADIO:
        return "radio";
    case NodeKind::SLIDER:
        return "slider";
    case NodeKind::PROGRESS:
        return "progress";
    case NodeKind::TEXT_FIELD:
        return "textField";
    case NodeKind::SELECT:
        return "select";
    case NodeKind::OPTION:
        return "option";
    case NodeKind::ALERT:
        return "alert";
    case NodeKind::BADGE:
        return "badge";
    case NodeKind::LINK:
        return "link";
    case NodeKind::ICON_BUTTON:
        return "iconButton";
    case NodeKind::TABS:
        return "tabs";
    case NodeKind::TAB:
        return "tab";
    case NodeKind::CONTAINER:
        return "container";
    case NodeKind::WRAP:
        return "wrap";
    case NodeKind::NAVBAR:
        return "navbar";
    case NodeKind::HEADER:
        return "header";
    case NodeKind::SECTION:
        return "section";
    case NodeKind::FOOTER:
        return "footer";
    case NodeKind::LIST:
        return "list";
    case NodeKind::LIST_ITEM:
        return "listItem";
    case NodeKind::MENU:
        return "menu";
    case NodeKind::MENU_ITEM:
        return "menuItem";
    case NodeKind::CLIP:
        return "clip";
    case NodeKind::ASPECT:
        return "aspect";
    case NodeKind::OVERLAY:
        return "overlay";
    case NodeKind::DIALOG:
        return "dialog";
    case NodeKind::FORM:
        return "form";
    case NodeKind::SKELETON:
        return "skeleton";
    case NodeKind::AVATAR:
        return "avatar";
    case NodeKind::EMPTY:
        return "empty";
    case NodeKind::BREADCRUMB:
        return "breadcrumb";
    case NodeKind::PAGINATION:
        return "pagination";
    }
    return "unknown";
}

}  // namespace Runtime
