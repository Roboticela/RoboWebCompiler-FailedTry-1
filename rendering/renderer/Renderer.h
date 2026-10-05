#pragma once

#include "engine/math/Color.h"
#include "engine/types/UiTypes.h"
#include "rendering/text/TextMeasurer.h"

#include <string>
#include <string_view>

namespace Runtime {

struct TextStyle {
    float fontSize = DEFAULT_TEXT_FONT_SIZE;
    int fontWeight = 400;
    Color color = COLOR_TEXT;
    TextAlign horizontal = TextAlign::START;
    TextAlign vertical = TextAlign::START;
    std::string fontFamily;
    std::string locale = "en-us";
    bool italic = false;
    bool rtl = false;
    bool wrap = true;
};

struct VectorPaint {
    Rect bounds;
    std::string_view data;
    float viewX = 0.0f;
    float viewY = 0.0f;
    float viewWidth = 0.0f;
    float viewHeight = 0.0f;
    Color fill;
    Color stroke;
    float strokeWidth = 0.0f;
    bool evenOdd = false;
    bool fillEnabled = false;
    char lineCap = 'b';
    char lineJoin = 'm';
};

class Renderer : public TextMeasurer {
public:
    ~Renderer() override = default;

    virtual void SetDpi(float dpi) = 0;
    virtual void SetTransform(float originX, float originY, float scale) = 0;
    virtual void BeginFrame(Color clearColor) = 0;
    virtual bool EndFrame() = 0;
    virtual void FillRectangle(const Rect& rect, const Color& color, float radius) = 0;
    virtual void StrokeRectangle(const Rect& rect, const Color& color, float thickness, float radius) = 0;
    virtual void DrawString(std::string_view text, const Rect& rect, const TextStyle& style) = 0;
    virtual void DrawCachedImage(std::string_view key, const Rect& rect, const unsigned char* bgra, int width, int height) = 0;
    virtual void DrawVector(const VectorPaint& paint) = 0;
    virtual void PushClip(const Rect& rect) = 0;
    virtual void PopClip() = 0;
    virtual void PushOpacity(float opacity) = 0;
    virtual void PopOpacity() = 0;
};

}  // namespace Runtime
