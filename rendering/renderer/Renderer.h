#pragma once

#include "engine/math/Color.h"
#include "engine/types/UiTypes.h"
#include "rendering/text/TextMeasurer.h"
#include "rendering/text/TextRun.h"
#include "rendering/text/TextStyle.h"

#include <string>
#include <string_view>
#include <vector>

namespace Runtime {

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
    // Alternating painted and unpainted lengths in pixels. An empty pattern strokes solid,
    // so a caller that never sets one keeps the plain line it always drew.
    std::vector<float> dashes;
    // Where in the pattern the stroke begins, in pixels. Zero begins it at the first point of the
    // geometry, so a caller that never sets one keeps the stroke starting where it always did.
    float dashOffset = 0.0f;
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
    // A paragraph of runs is one draw, not one draw per run: the lines of a paragraph are broken
    // across all of its runs, so the run that ends a line and the run that begins the next one are
    // placed by the same line breaking, and drawing them apart would break the paragraph twice.
    virtual void DrawParagraph(const std::vector<TextRun>& runs, const Rect& rect, const TextStyle& paragraph) = 0;
    virtual void DrawCachedImage(std::string_view key, const Rect& rect, const unsigned char* bgra, int width, int height) = 0;
    virtual void DrawVector(const VectorPaint& paint) = 0;
    virtual void PushClip(const Rect& rect) = 0;
    // A clip that is a shape rather than a box. The path is written the way a shape writes its own
    // outline, in the box-local pixels of `bounds`, so a rounded box is cut off as the rounded box
    // it is drawn as rather than as the box around it. A caller holding only a box uses PushClip,
    // which is the one cut the platform can make without building a shape first. A push is a push
    // whichever form made it, so PopClip unwinds either.
    virtual void PushClipPath(const Rect& bounds, std::string_view path) = 0;
    virtual void PopClip() = 0;
    virtual void PushOpacity(float opacity) = 0;
    virtual void PopOpacity() = 0;
    virtual void PushRotation(float degrees, float centerX, float centerY) = 0;
    virtual void PopTransform() = 0;
};

}  // namespace Runtime
