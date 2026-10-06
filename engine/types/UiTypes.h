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
    FRAME,
    RECTANGLE,
    ELLIPSE,
    LINE,
    SHADOW,
    TEXT,
    IMAGE,
    VECTOR,
    PATH,
    GROUP,
    COMPONENT,
    INSTANCE,
    SLOT,
    SCROLL,
    BOARD,
};

enum class FrameLayout : std::uint8_t {
    NONE,
    HORIZONTAL,
    VERTICAL,
    GRID,
    WRAP,
    STACK,
};

enum class FramePosition : std::uint8_t {
    FLOW,
    ABSOLUTE,
    FIXED,
    STICKY,
};

enum class SizeMode : std::uint8_t {
    UNSET,
    FIXED,
    FILL,
    FIT,
    PERCENT,
};

enum class OverflowMode : std::uint8_t {
    VISIBLE,
    HIDDEN,
    SCROLL,
    AUTO,
};

enum class FrameDisplay : std::uint8_t {
    NORMAL,
    NONE,
};

enum class FrameAnchor : std::uint8_t {
    NONE,
    TOP,
    RIGHT,
    BOTTOM,
    LEFT,
    CENTER,
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
    SCREEN,
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
    JUSTIFY,
};

enum class BindMode : std::uint8_t {
    ONE_WAY,
    TWO_WAY,
    ONE_TIME,
    COMPUTED,
    REACTIVE,
    ASYNC,
};

enum class BoxEdge : std::uint8_t {
    LEFT,
    RIGHT,
    TOP,
    BOTTOM,
    CENTER_X,
    CENTER_Y,
    COUNT,
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

    static Length Screen(float percent = 100.0f) {
        return Length{percent, LengthUnit::SCREEN};
    }

    float Resolve(float available, float fit, bool allowNegative = false) const {
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
        case LengthUnit::SCREEN:
            resolved = available * (value <= 0.0f ? 1.0f : value / 100.0f);
            break;
        case LengthUnit::FIT:
            resolved = fit;
            break;
        }
        if (!allowNegative && resolved < 0.0f) {
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
    case NodeKind::FRAME:
        return "frame";
    case NodeKind::RECTANGLE:
        return "rectangle";
    case NodeKind::ELLIPSE:
        return "ellipse";
    case NodeKind::LINE:
        return "line";
    case NodeKind::SHADOW:
        return "shadow";
    case NodeKind::TEXT:
        return "text";
    case NodeKind::IMAGE:
        return "image";
    case NodeKind::VECTOR:
        return "vector";
    case NodeKind::PATH:
        return "path";
    case NodeKind::GROUP:
        return "group";
    case NodeKind::COMPONENT:
        return "component";
    case NodeKind::INSTANCE:
        return "instance";
    case NodeKind::SLOT:
        return "slot";
    case NodeKind::SCROLL:
        return "scroll";
    case NodeKind::BOARD:
        return "board";
    }
    return "unknown";
}

}  // namespace Runtime
