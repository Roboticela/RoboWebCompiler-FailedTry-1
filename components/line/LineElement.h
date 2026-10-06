#pragma once

#include "ui/element/Element.h"

#include <string>
#include <vector>

namespace Runtime {

// A line is two endpoints and the stroke drawn between them. It holds nothing and lays out
// nothing, so it is one item in its parent's flow whose box is the span of its endpoints.
// A box wider or taller than that span stretches the endpoints with it, which is what makes
// dragging a line's resize handle lengthen the line rather than just its hit area.
class LineElement : public Element {
public:
    LineElement();

    NodeKind Kind() const override;
    Size Measure(TextMeasurer& measurer, float availableWidth, float availableHeight, bool heightDefinite) override;
    Element* HitTest(Point point) override;
    void ApplyBound(const std::string& property, const std::string& value) override;
    bool WantsHandCursor() const override;

    void SetEndpoints(float startX, float startY, float endX, float endY);
    void SetStroke(Color value, float thickness);
    void SetStrokeWidth(float thickness);
    void SetLineCap(std::string value);
    void SetDash(std::string value, float gap);
    void SetRotation(float degrees);
    void SetCursor(std::string value);
    void SetDisplay(std::string value);

    float StartX() const { return x1; }
    float StartY() const { return y1; }
    float EndX() const { return x2; }
    float EndY() const { return y2; }
    Color StrokeColor() const { return stroke; }
    float StrokeWidth() const { return strokeWidth; }
    const std::string& LineCap() const { return capName; }
    const std::vector<float>& Dashes() const { return dashes; }
    float Rotation() const { return rotationZ; }
    const std::string& Cursor() const { return cursor; }

protected:
    void OnPaint(Renderer& renderer) override;

private:
    // The two endpoints placed inside the arranged box, in box-local pixels.
    void PlaceEndpoints(Point& start, Point& end) const;
    std::string SegmentPath() const;

    float x1 = 0.0f;
    float y1 = 0.0f;
    float x2 = 80.0f;
    float y2 = 0.0f;

    // The span of the endpoints before any explicit size, and where that span starts.
    // A span of zero on an axis is not a division by zero but a line with no run, so the
    // endpoints are centred on that axis instead of being scaled by it.
    float spanX = 80.0f;
    float spanY = 0.0f;
    float spanOriginX = 0.0f;
    float spanOriginY = 0.0f;

    Color stroke{0.094f, 0.094f, 0.102f, 1.0f};
    float strokeWidth = 2.0f;
    char lineCap = 'b';
    std::string capName = "butt";
    std::vector<float> dashes;
    float rotationZ = 0.0f;
    std::string cursor;
};

}  // namespace Runtime
