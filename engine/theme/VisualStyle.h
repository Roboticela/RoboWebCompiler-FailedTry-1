#pragma once

#include "engine/types/UiTypes.h"

#include <string>
#include <unordered_map>
#include <vector>

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
    float maxWidth = 0.0f;
    float minWidth = 0.0f;
};

struct MotionKey {
    float time = 0.0f;
    std::string property;
    float number = 0.0f;
    bool hasNumber = false;
    std::string text;
};

struct MotionDecl {
    std::string name;
    std::string target;
    std::string property;
    float from = 0.0f;
    float to = 1.0f;
    bool hasFrom = false;
    bool hasTo = false;
    float duration = 0.15f;
    std::string easing = "linear";
    std::string trigger;
    std::string sequence;
    std::string parallel;
    float stagger = 0.0f;
    float mass = 1.0f;
    float stiffness = 180.0f;
    float damping = 12.0f;
    float velocity = 0.0f;
    std::vector<MotionKey> keys;
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
    if (over.maxWidth > 0.0f) {
        base.maxWidth = over.maxWidth;
    }
    if (over.minWidth > 0.0f) {
        base.minWidth = over.minWidth;
    }
}

struct VisualStyle {
    bool used = false;
    std::string extends;
    std::string variant;
    StylePatch phases[STYLE_COUNT]{};
    std::unordered_map<std::string, StylePatch> extraPhases;
    std::vector<MotionDecl> motions;
};

}  // namespace Runtime
