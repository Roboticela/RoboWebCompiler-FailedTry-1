#pragma once

#include "engine/math/Color.h"
#include "engine/types/UiTypes.h"
#include "rendering/text/TextMeasurer.h"

#include <string>

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
    virtual void PushClip(const Rect& rect) = 0;
    virtual void PopClip() = 0;
    virtual void PushOpacity(float opacity) = 0;
    virtual void PopOpacity() = 0;
};

}  // namespace Runtime
