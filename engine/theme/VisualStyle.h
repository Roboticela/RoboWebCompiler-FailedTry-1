#pragma once

#include "engine/types/UiTypes.h"

namespace Runtime {

inline constexpr int STYLE_NORMAL = 0;
inline constexpr int STYLE_HOVER = 1;
inline constexpr int STYLE_PRESSED = 2;
inline constexpr int STYLE_FOCUSED = 3;
inline constexpr int STYLE_DISABLED = 4;
inline constexpr int STYLE_SELECTED = 5;
inline constexpr int STYLE_COUNT = 6;

struct StylePatch {
    bool background = false;
    Color backgroundColor{};
    bool foreground = false;
    Color foregroundColor{};
    bool border = false;
    Color borderColor{};
    float borderWidth = 1.0f;
    bool radius = false;
    Length radiusLength = Length::Pixels(0.0f);
    bool padding = false;
    Edges paddingEdges{};
    bool fontSize = false;
    float fontSizeValue = 17.0f;
    bool fontWeight = false;
    int fontWeightValue = 400;
};

inline void OverlayPatch(StylePatch& base, const StylePatch& over) {
    if (over.background) {
        base.background = true;
        base.backgroundColor = over.backgroundColor;
    }
    if (over.foreground) {
        base.foreground = true;
        base.foregroundColor = over.foregroundColor;
    }
    if (over.border) {
        base.border = true;
        base.borderColor = over.borderColor;
        base.borderWidth = over.borderWidth;
    }
    if (over.radius) {
        base.radius = true;
        base.radiusLength = over.radiusLength;
    }
    if (over.padding) {
        base.padding = true;
        base.paddingEdges = over.paddingEdges;
    }
    if (over.fontSize) {
        base.fontSize = true;
        base.fontSizeValue = over.fontSizeValue;
    }
    if (over.fontWeight) {
        base.fontWeight = true;
        base.fontWeightValue = over.fontWeightValue;
    }
}

struct VisualStyle {
    bool used = false;
    StylePatch phases[STYLE_COUNT]{};
};

}  // namespace Runtime
