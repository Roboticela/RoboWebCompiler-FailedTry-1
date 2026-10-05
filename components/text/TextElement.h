#pragma once

#include "components/frame/Frame.h"

#include <string>

namespace Runtime {

class TextElement : public Frame {
public:
    NodeKind Kind() const override;
    Size Measure(TextMeasurer& measurer, float availableWidth, float availableHeight, bool heightDefinite) override;
    void OnPaint(Renderer& renderer) override;
    void ApplyBound(const std::string& property, const std::string& value) override;

    void SetText(std::string value);
    const std::string& Text() const;
    void SetRole(std::string value);
    void SetFont(float size, int weight, Color color, TextAlign horizontal);
    void SetFontSpecified(bool size, bool weight, bool color);
    void SetTracking(float em, bool specified);
    void SetLeading(float multiplier, bool specified);
    void SetTextStyle(std::string family, std::string file, bool italic, bool rtl, std::string locale);
    void SetWrapText(bool value);
    void SetMaxLines(int value);
    void SetEllipsis(bool value);
    void SetVerticalAlign(TextAlign value);
    void SetTransform(std::string value, bool specified);
    void SetDecoration(bool underline, bool strike, bool overline, Color color, float thickness, std::string style);
    void SetWordSpacing(float value);
    void SetFontFeatures(std::string value);
    void SetWritingMode(std::string value);
    float FontSize() const;
    int FontWeight() const;
    const std::string& FontFamily() const;
    std::string DisplayText() const;

private:
    void ResolveRole(float viewport);
    std::string Transformed(const std::string& source) const;

    std::string text;
    std::string role = "content";
    std::string fontFamily = "Inter";
    std::string fontFile;
    std::string locale = "en-us";
    std::string textTransform = "none";
    std::string decorationStyle = "solid";
    std::string fontFeatures;
    std::string writingMode = "horizontal";
    Color textColor{0x1D / 255.0f, 0x1D / 255.0f, 0x1F / 255.0f, 1.0f};
    Color decorationColor{0x1D / 255.0f, 0x1D / 255.0f, 0x1F / 255.0f, 1.0f};
    float fontSize = 19.0f;
    float letterSpacingEm = 0.0f;
    float lineHeight = 1.42f;
    float wordSpacing = 0.0f;
    float decorationWidth = 1.0f;
    int fontWeight = 400;
    int maxLines = 0;
    TextAlign textAlign = TextAlign::START;
    TextAlign verticalAlign = TextAlign::START;
    bool italic = false;
    bool rtl = false;
    bool wrapText = true;
    bool ellipsis = false;
    bool underline = false;
    bool lineThrough = false;
    bool overline = false;
    bool sizeSpecified = false;
    bool weightSpecified = false;
    bool colorSpecified = false;
    bool trackingSpecified = false;
    bool leadingSpecified = false;
    bool transformSpecified = false;
};

}  // namespace Runtime
