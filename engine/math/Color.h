#pragma once

namespace Runtime {

struct Color {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float a = 1.0f;
};

inline Color ScaleColor(Color color, float scale) {
    return Color{color.r * scale, color.g * scale, color.b * scale, color.a};
}

inline constexpr Color COLOR_WINDOW_BACKGROUND{1.0f, 1.0f, 1.0f, 1.0f};
inline constexpr Color COLOR_TEXT{29.0f / 255.0f, 29.0f / 255.0f, 31.0f / 255.0f, 1.0f};
inline constexpr Color COLOR_BUTTON_BACKGROUND{14.0f / 255.0f, 122.0f / 255.0f, 255.0f / 255.0f, 1.0f};
inline constexpr Color COLOR_BUTTON_TEXT{1.0f, 1.0f, 1.0f, 1.0f};
inline constexpr Color COLOR_BUTTON_DISABLED{0.620f, 0.640f, 0.680f, 1.0f};

}  // namespace Runtime
