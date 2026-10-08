#pragma once

#include "engine/math/Geometry.h"
#include "rendering/path/PathCurve.h"
#include "rendering/renderer/Renderer.h"

#include <string_view>
#include <vector>

namespace Runtime {

// A renderer that answers with the outlines a tree draws instead of drawing them.
//
// A shape already knows how to draw itself: a box of four rounded corners, an ellipse of two arcs, a
// ring of sides, the paths of an imported icon, and the turn a node was given. Nothing else knows
// that geometry, so a shape that needs the outlines of other shapes paints them here and reads back
// what they covered. What it gets is every outline those draws cover, written in one space, which
// is what combining several shapes into one shape starts from.
//
// A stroke is read as the outline it was stroked along rather than as the band the stroke covers:
// combining shapes is combining the shapes themselves, so the width a shape was stroked with does
// not widen what it contributes. A string and an image have no outline to be read, so each
// contributes the box it was given. A clip is not read: it decides what is visible of what is
// already there, not what a shape covers.
class OutlineRecorder : public Renderer {
public:
    void SetDpi(float dpi) override;
    void SetTransform(float originX, float originY, float scale) override;
    void BeginFrame(Color clearColor) override;
    bool EndFrame() override;
    void FillRectangle(const Rect& rect, const Color& color, float radius) override;
    void StrokeRectangle(const Rect& rect, const Color& color, float thickness, float radius) override;
    void DrawString(std::string_view text, const Rect& rect, const TextStyle& style) override;
    void DrawParagraph(const std::vector<TextRun>& runs, const Rect& rect, const TextStyle& paragraph) override;
    void DrawCachedImage(std::string_view key, const Rect& rect, const unsigned char* bgra, int width, int height) override;
    void DrawVector(const VectorPaint& paint) override;
    void PushClip(const Rect& rect) override;
    void PushClipPath(const Rect& bounds, std::string_view path) override;
    void PopClip() override;
    void PushOpacity(float opacity) override;
    void PopOpacity() override;
    void PushRotation(float degrees, float centerX, float centerY) override;
    void PopTransform() override;
    // Both forms of the measurement are offered, so a caller holding a recorder can ask by size
    // alone as well as ask with the style a block is drawn with.
    using TextMeasurer::MeasureText;
    Size MeasureText(std::string_view text, const TextStyle& style, float maxWidth) override;
    TextParagraph MeasureParagraph(const std::vector<TextRun>& runs, const TextStyle& paragraph, float width, float height) override;

    // Where the outlines are written from. Every point captured is written relative to this corner,
    // which is how a shape painted in composition coordinates comes back as the outline it draws
    // inside the box that holds it rather than as the place on the page it was drawn at.
    void SetOrigin(Point origin);

    // The outlines captured so far, one contour per loop of one draw.
    const std::vector<PathContour>& Outlines() const { return outlines; }

private:
    // The placement a draw asked for, kept as an affine transform: a turn reaches a shape as a
    // rotated box, and its outline has to arrive turned with it.
    struct Transform {
        float m00 = 1.0f;
        float m01 = 0.0f;
        float m10 = 0.0f;
        float m11 = 1.0f;
        float tx = 0.0f;
        float ty = 0.0f;

        Point Apply(Point point) const {
            return Point{m00 * point.x + m10 * point.y + tx, m01 * point.x + m11 * point.y + ty};
        }
    };

    // Reads one path into the outlines it covers: the path is written where the draw said it was
    // written, in the space the draw said it was written in, and every point of it is carried
    // through the transform the paint pass was under and out to where the outlines are written from.
    void Capture(std::string_view data, const Rect& bounds, float viewX, float viewY, float viewWidth, float viewHeight);
    // The transform that turns a point about a centre by an angle, the same way the platform turns a
    // shape about the centre it was given.
    static Transform RotationAbout(float degrees, Point center);

    std::vector<PathContour> outlines;
    std::vector<Transform> transforms;
    Point origin{0.0f, 0.0f};
};

}  // namespace Runtime
