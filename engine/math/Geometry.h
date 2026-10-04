#pragma once

namespace Runtime {

struct Point {
    float x = 0.0f;
    float y = 0.0f;
};

struct Size {
    float width = 0.0f;
    float height = 0.0f;
};

struct Rect {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
};

inline bool ContainsPoint(Rect rect, Point point) {
    return point.x >= rect.x && point.y >= rect.y && point.x < rect.x + rect.width &&
           point.y < rect.y + rect.height;
}

}  // namespace Runtime
