#pragma once

#include "engine/math/Color.h"

namespace Runtime {

inline constexpr Color COLOR_BRAND_PRIMARY{14.0f / 255.0f, 122.0f / 255.0f, 255.0f / 255.0f, 1.0f};
inline constexpr Color COLOR_BRAND_PRIMARY_HOVER{10.0f / 255.0f, 106.0f / 255.0f, 232.0f / 255.0f, 1.0f};
inline constexpr Color COLOR_BRAND_PRIMARY_ACTIVE{7.0f / 255.0f, 87.0f / 255.0f, 199.0f / 255.0f, 1.0f};
inline constexpr Color COLOR_BRAND_PRIMARY_DISABLED{184.0f / 255.0f, 217.0f / 255.0f, 255.0f / 255.0f, 1.0f};
inline constexpr Color COLOR_DANGER{255.0f / 255.0f, 69.0f / 255.0f, 58.0f / 255.0f, 1.0f};
inline constexpr Color COLOR_DANGER_HOVER{255.0f / 255.0f, 90.0f / 255.0f, 80.0f / 255.0f, 1.0f};
inline constexpr Color COLOR_TEXT_PRIMARY{29.0f / 255.0f, 29.0f / 255.0f, 31.0f / 255.0f, 1.0f};
inline constexpr Color COLOR_TEXT_SECONDARY{110.0f / 255.0f, 110.0f / 255.0f, 115.0f / 255.0f, 1.0f};
inline constexpr Color COLOR_TEXT_TERTIARY{134.0f / 255.0f, 134.0f / 255.0f, 139.0f / 255.0f, 1.0f};
inline constexpr Color COLOR_TEXT_ON_BRAND{1.0f, 1.0f, 1.0f, 1.0f};
inline constexpr Color COLOR_BG_PRIMARY{1.0f, 1.0f, 1.0f, 1.0f};
inline constexpr Color COLOR_BG_SECONDARY{245.0f / 255.0f, 245.0f / 255.0f, 247.0f / 255.0f, 1.0f};
inline constexpr Color COLOR_BG_TERTIARY{237.0f / 255.0f, 237.0f / 255.0f, 239.0f / 255.0f, 1.0f};
inline constexpr Color COLOR_BORDER_DEFAULT{210.0f / 255.0f, 210.0f / 255.0f, 215.0f / 255.0f, 1.0f};
inline constexpr Color COLOR_BORDER_STRONG{184.0f / 255.0f, 184.0f / 255.0f, 189.0f / 255.0f, 1.0f};

// Scrollbar: a thin, transparent-track pill. The visible thumb is inset from the
// track so it reads as a light overlay rather than a solid rail. Idle, hover, and
// drag shades match the design reference (border-strong, text-tertiary, text-secondary).
inline constexpr Color COLOR_SCROLLBAR_THUMB = COLOR_BORDER_STRONG;
inline constexpr float SCROLLBAR_THICKNESS = 8.0f;
inline constexpr float SCROLLBAR_THICKNESS_THIN = 6.0f;
inline constexpr float SCROLLBAR_THUMB_INSET = 2.0f;
inline constexpr float SCROLLBAR_MIN_THUMB = 28.0f;
inline constexpr float SCROLLBAR_ARROW_SIZE = 12.0f;
inline constexpr float SCROLLBAR_HOVER_SCALE = 0.73f;
inline constexpr float SCROLLBAR_ACTIVE_SCALE = 0.60f;
inline constexpr float SCROLL_WHEEL_DELTA = 120.0f;
inline constexpr float SCROLL_WHEEL_MIN_STEP = 2.0f;
inline constexpr float SCROLL_INERTIA_FLOOR = 0.4f;
inline constexpr float SCROLL_INERTIA_DECAY = 0.002f;
inline constexpr float SCROLL_FLICK_MIN = 1.5f;

inline constexpr float TYPE_DISPLAY = 80.0f;
inline constexpr float TYPE_H1 = 56.0f;
inline constexpr float TYPE_H2 = 40.0f;
inline constexpr float TYPE_H3 = 28.0f;
inline constexpr float TYPE_SUBHEAD = 24.0f;
inline constexpr float TYPE_BODY = 17.0f;
inline constexpr float TYPE_CAPTION = 12.0f;
inline constexpr float TYPE_BUTTON = 17.0f;
inline constexpr int WEIGHT_REGULAR = 400;
inline constexpr int WEIGHT_MEDIUM = 500;
inline constexpr int WEIGHT_SEMIBOLD = 600;

inline constexpr float BUTTON_HEIGHT_SM = 32.0f;
inline constexpr float BUTTON_HEIGHT_MD = 44.0f;
inline constexpr float BUTTON_HEIGHT_LG = 50.0f;
inline constexpr float BUTTON_PAD_SM = 14.0f;
inline constexpr float BUTTON_PAD_MD = 22.0f;
inline constexpr float BUTTON_PAD_LG = 24.0f;
inline constexpr float RADIUS_SM = 6.0f;
inline constexpr float RADIUS_MD = 8.0f;
inline constexpr float RADIUS_LG = 12.0f;
inline constexpr float RADIUS_XL = 16.0f;
inline constexpr float RADIUS_PILL = 980.0f;

inline constexpr int Z_DESKTOP = 0;
inline constexpr int Z_WINDOW = 10;
inline constexpr int Z_DOCK = 600;
inline constexpr int Z_MENUBAR = 700;
inline constexpr int Z_NOTIFICATIONS = 800;
inline constexpr int Z_SPOTLIGHT = 900;
inline constexpr int Z_MODAL = 900;
inline constexpr int Z_CONTEXT_MENU = 950;
inline constexpr int Z_TOOLTIP = 1000;

}  // namespace Runtime
