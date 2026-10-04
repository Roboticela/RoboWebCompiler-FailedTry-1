#pragma once

#include "engine/theme/DevKitTheme.h"
#include "ui/element/Element.h"

namespace Runtime {

class TextBlock : public Element {
public:
    NodeKind Kind() const override;
    Size Measure(TextMeasurer& measurer, float availableWidth, float availableHeight, bool heightDefinite) override;
    void OnPaint(Renderer& renderer) override;
    Element* HitTest(Point point) override;
    TextBlock* AsText() override;
    void ApplyBound(const std::string& property, const std::string& value) override;

    void SetText(std::string value);
    const std::string& Text() const;
    void SetFontSize(float value);
    void SetFontWeight(int value);
    void SetColor(Color value);
    void SetRole(TextRole value);
    void SetTextAlign(TextAlign value);
    void SetVerticalAlign(TextAlign value);

private:
    std::string text;
    float fontSize = TYPE_BODY;
    int fontWeight = WEIGHT_REGULAR;
    Color color = COLOR_TEXT_PRIMARY;
    TextRole role = TextRole::BODY;
    TextAlign textAlign = TextAlign::START;
    TextAlign verticalAlign = TextAlign::START;
};

}  // namespace Runtime
