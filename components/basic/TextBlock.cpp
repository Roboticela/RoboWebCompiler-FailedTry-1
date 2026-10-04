#include "components/basic/TextBlock.h"

#include "rendering/renderer/Renderer.h"

#include <algorithm>

namespace Runtime {

NodeKind TextBlock::Kind() const {
    return NodeKind::TEXT;
}

void TextBlock::SetText(std::string value) {
    text = std::move(value);
}

const std::string& TextBlock::Text() const {
    return text;
}

void TextBlock::SetFontSize(float value) {
    fontSize = value;
}

void TextBlock::SetFontWeight(int value) {
    fontWeight = value;
}

void TextBlock::SetRole(TextRole value) {
    role = value;
    switch (role) {
    case TextRole::DISPLAY:
        fontSize = TYPE_DISPLAY;
        fontWeight = WEIGHT_SEMIBOLD;
        break;
    case TextRole::H1:
        fontSize = TYPE_H1;
        fontWeight = WEIGHT_SEMIBOLD;
        break;
    case TextRole::H2:
        fontSize = TYPE_H2;
        fontWeight = WEIGHT_SEMIBOLD;
        break;
    case TextRole::H3:
        fontSize = TYPE_H3;
        fontWeight = WEIGHT_SEMIBOLD;
        break;
    case TextRole::SUBHEAD:
        fontSize = TYPE_SUBHEAD;
        fontWeight = WEIGHT_REGULAR;
        break;
    case TextRole::CAPTION:
        fontSize = TYPE_CAPTION;
        fontWeight = WEIGHT_REGULAR;
        color = COLOR_TEXT_SECONDARY;
        break;
    case TextRole::BODY:
        fontSize = TYPE_BODY;
        fontWeight = WEIGHT_REGULAR;
        break;
    }
}

void TextBlock::SetColor(Color value) {
    color = value;
}

TextBlock* TextBlock::AsText() {
    return this;
}

void TextBlock::ApplyBound(const std::string& property, const std::string& value) {
    Element::ApplyBound(property, value);
    if (property == "text" || property == "value") {
        SetText(value);
    }
}

Size TextBlock::Measure(TextMeasurer& measurer, float availableWidth, float availableHeight, bool heightDefinite) {
    float limitWidth = UNBOUNDED_MEASURE;
    if (width.unit != LengthUnit::FIT) {
        limitWidth = width.Resolve(availableWidth, availableWidth);
    }
    Size textSize = measurer.MeasureText(text, fontSize, limitWidth);
    if (text.empty()) {
        textSize.height = std::max(textSize.height, fontSize);
    }
    measured.width = width.unit == LengthUnit::FIT ? textSize.width : limitWidth;
    measured.height = ResolveDefinite(height, availableHeight, textSize.height, heightDefinite);
    return FinishMeasure(availableWidth, availableHeight);
}

void TextBlock::SetTextAlign(TextAlign value) {
    textAlign = value;
}

void TextBlock::SetVerticalAlign(TextAlign value) {
    verticalAlign = value;
}

void TextBlock::OnPaint(Renderer& renderer) {
    renderer.PushClip(bounds);
    TextStyle style;
    const StylePatch patch = ActivePatch();
    style.fontSize = patch.fontSize ? patch.fontSizeValue : fontSize;
    style.fontWeight = patch.fontWeight ? patch.fontWeightValue : fontWeight;
    style.color = patch.foreground ? patch.foregroundColor : color;
    style.horizontal = textAlign;
    style.vertical = verticalAlign;
    renderer.DrawString(text, bounds, style);
    renderer.PopClip();
}

Element* TextBlock::HitTest(Point point) {
    if (!visible) {
        return nullptr;
    }
    if (ContainsPoint(bounds, point)) {
        return this;
    }
    return nullptr;
}

}  // namespace Runtime
