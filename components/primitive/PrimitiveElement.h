#pragma once

#include "components/frame/Frame.h"

#include <string>

namespace Runtime {

class ShapeElement : public Element {
public:
    explicit ShapeElement(NodeKind nodeKind);

    NodeKind Kind() const override;
    Size Measure(TextMeasurer& measurer, float availableWidth, float availableHeight, bool heightDefinite) override;
    void Arrange(const Rect& rect) override;
    Element* HitTest(Point point) override;

    void SetText(std::string value);
    void SetSource(std::string value);
    void SetPathData(std::string value);
    void SetShapeName(std::string value);
    void SetFill(Color value);
    void SetStroke(Color value, float width);
    void SetFont(float size, int weight, Color color, TextAlign align);
    void SetLine(float startX, float startY, float endX, float endY);
    void SetCornerRadius(float value);

protected:
    void OnPaint(Renderer& renderer) override;

private:
    bool PlacesChildren() const;

    NodeKind nodeKind = NodeKind::TEXT;
    std::string text;
    std::string source;
    std::string pathData;
    std::string shapeName = "rect";
    Color fill{0.055f, 0.478f, 1.0f, 1.0f};
    Color stroke{0.114f, 0.114f, 0.122f, 1.0f};
    Color textColor{0.094f, 0.094f, 0.102f, 1.0f};
    float strokeWidth = 1.0f;
    float fontSize = 16.0f;
    int fontWeight = 400;
    TextAlign textAlign = TextAlign::START;
    float lineX1 = 0.0f;
    float lineY1 = 0.0f;
    float lineX2 = 80.0f;
    float lineY2 = 0.0f;
    float cornerRadius = 0.0f;
};

class ScrollElement : public Frame {
public:
    ScrollElement();
    NodeKind Kind() const override;
};

}  // namespace Runtime
