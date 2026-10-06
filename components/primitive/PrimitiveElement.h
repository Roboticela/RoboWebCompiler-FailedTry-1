#pragma once

#include "components/frame/Frame.h"
#include "rendering/image/ImageDecoder.h"

#include <string>

namespace Runtime {

class ShapeElement : public Element {
public:
    explicit ShapeElement(NodeKind nodeKind);

    NodeKind Kind() const override;
    Size Measure(TextMeasurer& measurer, float availableWidth, float availableHeight, bool heightDefinite) override;
    void Arrange(const Rect& rect) override;
    Element* HitTest(Point point) override;
    void ApplyBound(const std::string& property, const std::string& value) override;

    void SetText(std::string value);
    const std::string& Text() const;
    void SetSource(std::string value);
    void SetImageFit(std::string value);
    void SetImageClip(bool value);
    void SetPathData(std::string value);
    void SetShapeName(std::string value);
    void SetFill(Color value);
    void SetStroke(Color value, float thickness);
    void SetViewBox(float x, float y, float boxWidth, float boxHeight);
    void SetLineStyle(const std::string& cap, const std::string& join, bool evenOdd);
    void SetFont(float size, int weight, Color color, TextAlign align);
    void SetTextColor(Color value);
    void SetUnderline(bool value);
    void SetSpin(float degrees);
    void SetTextStyle(std::string family, bool italic, bool rtl, std::string locale);
    void SetCornerRadius(float value);

protected:
    void OnPaint(Renderer& renderer) override;

private:
    bool PlacesChildren() const;
    void EnsureImage();

    NodeKind nodeKind = NodeKind::TEXT;
    std::string text;
    std::string source;
    std::string imageFit = "fill";
    bool imageClip = false;
    bool imageReady = false;
    DecodedImage decoded;
    std::string pathData;
    std::string shapeName = "rect";
    Color fill{0.055f, 0.478f, 1.0f, 1.0f};
    Color stroke{0.114f, 0.114f, 0.122f, 1.0f};
    bool fillSpecified = false;
    float viewX = 0.0f;
    float viewY = 0.0f;
    float viewWidth = 0.0f;
    float viewHeight = 0.0f;
    char lineCap = 'b';
    char lineJoin = 'm';
    bool evenOdd = false;
    Color textColor{0.094f, 0.094f, 0.102f, 1.0f};
    float strokeWidth = 1.0f;
    float fontSize = 16.0f;
    int fontWeight = 400;
    TextAlign textAlign = TextAlign::START;
    std::string fontFamily;
    std::string locale = "en-us";
    bool italic = false;
    bool rtl = false;
    bool underline = false;
    float spin = 0.0f;
    float cornerRadius = 0.0f;
};

class ScrollElement : public Frame {
public:
    ScrollElement();
    NodeKind Kind() const override;
};

}  // namespace Runtime
