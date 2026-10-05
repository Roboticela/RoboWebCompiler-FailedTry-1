#include "components/text/TextElement.h"

#include "compiler/types/ValueParser.h"
#include "rendering/renderer/Renderer.h"

#include <algorithm>
#include <cctype>

namespace Runtime {
namespace {

struct RoleStyle {
    const char* name;
    float desktop;
    float mobile;
    int weight;
    float tracking;
    float leading;
    Color color;
    const char* transform;
};

Color Rgb(int red, int green, int blue) {
    return Color{red / 255.0f, green / 255.0f, blue / 255.0f, 1.0f};
}

const Color TONE_PRIMARY = Rgb(0x1D, 0x1D, 0x1F);
const Color TONE_SECONDARY = Rgb(0x6E, 0x6E, 0x73);
const Color TONE_TERTIARY = Rgb(0x86, 0x86, 0x8B);
const Color TONE_DISABLED = Rgb(0xC7, 0xC7, 0xCC);
const Color TONE_LINK = Rgb(0x0E, 0x7A, 0xFF);
const Color TONE_DANGER = Rgb(0xFF, 0x3B, 0x30);
const Color TONE_SUCCESS = Rgb(0x30, 0xD1, 0x58);
const Color TONE_WARNING = Rgb(0xFF, 0x9F, 0x0A);
const Color TONE_ON_BRAND = Rgb(0xFF, 0xFF, 0xFF);

const RoleStyle ROLES[] = {
    {"display", 80.0f, 40.0f, 600, -0.015f, 1.05f, TONE_PRIMARY, "none"},
    {"h1", 56.0f, 32.0f, 600, -0.015f, 1.07f, TONE_PRIMARY, "none"},
    {"h2", 40.0f, 28.0f, 600, -0.01f, 1.1f, TONE_PRIMARY, "none"},
    {"h3", 28.0f, 21.0f, 600, -0.005f, 1.14f, TONE_PRIMARY, "none"},
    {"subhead", 24.0f, 19.0f, 400, 0.0f, 1.33f, TONE_SECONDARY, "none"},
    {"content", 19.0f, 17.0f, 400, 0.0f, 1.42f, TONE_SECONDARY, "none"},
    {"content-sm", 17.0f, 17.0f, 400, 0.0f, 1.47f, TONE_SECONDARY, "none"},
    {"caption", 12.0f, 12.0f, 400, 0.0f, 1.33f, TONE_TERTIARY, "none"},
    {"eyebrow", 12.0f, 12.0f, 400, 0.05f, 1.33f, TONE_TERTIARY, "uppercase"},
    {"label", 17.0f, 17.0f, 500, 0.0f, 1.0f, TONE_PRIMARY, "none"},
};

const RoleStyle* FindRole(const std::string& name) {
    for (const RoleStyle& role : ROLES) {
        if (name == role.name) {
            return &role;
        }
    }
    return &ROLES[5];
}

}  // namespace

NodeKind TextElement::Kind() const {
    return NodeKind::TEXT;
}

void TextElement::SetText(std::string value) {
    text = std::move(value);
}

const std::string& TextElement::Text() const {
    return text;
}

void TextElement::SetRole(std::string value) {
    role = std::move(value);
    ResolveRole(ViewportWidth());
}

void TextElement::SetFont(float size, int weight, Color color, TextAlign horizontal) {
    fontSize = size;
    fontWeight = weight;
    textColor = color;
    decorationColor = color;
    textAlign = horizontal;
}

void TextElement::SetFontSpecified(bool size, bool weight, bool color) {
    sizeSpecified = size;
    weightSpecified = weight;
    colorSpecified = color;
}

void TextElement::SetTracking(float em, bool specified) {
    letterSpacingEm = em;
    trackingSpecified = specified;
}

void TextElement::SetLeading(float multiplier, bool specified) {
    lineHeight = multiplier;
    leadingSpecified = specified;
}

void TextElement::SetTextStyle(std::string family, std::string file, bool italicValue, bool rtlValue, std::string localeValue) {
    if (!family.empty()) {
        fontFamily = std::move(family);
    }
    fontFile = std::move(file);
    italic = italicValue;
    rtl = rtlValue;
    if (!localeValue.empty()) {
        locale = std::move(localeValue);
    }
}

void TextElement::SetWrapText(bool value) {
    wrapText = value;
}

void TextElement::SetMaxLines(int value) {
    maxLines = value;
}

void TextElement::SetEllipsis(bool value) {
    ellipsis = value;
}

void TextElement::SetVerticalAlign(TextAlign value) {
    verticalAlign = value;
}

void TextElement::SetTransform(std::string value, bool specified) {
    textTransform = std::move(value);
    transformSpecified = specified;
}

void TextElement::SetDecoration(bool underlineValue, bool strike, bool overlineValue, Color color, float thickness, std::string style) {
    underline = underlineValue;
    lineThrough = strike;
    overline = overlineValue;
    decorationColor = color;
    decorationWidth = thickness;
    if (!style.empty()) {
        decorationStyle = std::move(style);
    }
}

void TextElement::SetWordSpacing(float value) {
    wordSpacing = value;
}

void TextElement::SetFontFeatures(std::string value) {
    fontFeatures = std::move(value);
}

void TextElement::SetWritingMode(std::string value) {
    writingMode = std::move(value);
}

float TextElement::FontSize() const {
    return fontSize;
}

int TextElement::FontWeight() const {
    return fontWeight;
}

const std::string& TextElement::FontFamily() const {
    return fontFamily;
}

void TextElement::ResolveRole(float viewport) {
    const RoleStyle* style = FindRole(role);
    const bool mobile = viewport > 0.0f && viewport <= 734.0f;
    if (!sizeSpecified) {
        fontSize = mobile ? style->mobile : style->desktop;
    }
    if (!weightSpecified) {
        fontWeight = style->weight;
    }
    if (!trackingSpecified) {
        letterSpacingEm = style->tracking;
    }
    if (!leadingSpecified) {
        if (std::string(style->name) == "content" && mobile) {
            lineHeight = 1.47f;
        } else {
            lineHeight = style->leading;
        }
    }
    if (!colorSpecified) {
        textColor = style->color;
        decorationColor = style->color;
    }
    if (!transformSpecified) {
        textTransform = style->transform;
    }
}

std::string TextElement::Transformed(const std::string& source) const {
    if (textTransform == "none" || textTransform.empty()) {
        return source;
    }
    std::string result = source;
    if (textTransform == "uppercase") {
        for (char& character : result) {
            character = static_cast<char>(std::toupper(static_cast<unsigned char>(character)));
        }
    } else if (textTransform == "lowercase") {
        for (char& character : result) {
            character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
        }
    } else if (textTransform == "capitalize") {
        bool word = true;
        for (char& character : result) {
            const unsigned char value = static_cast<unsigned char>(character);
            if (std::isspace(value)) {
                word = true;
            } else if (word) {
                character = static_cast<char>(std::toupper(value));
                word = false;
            } else {
                character = static_cast<char>(std::tolower(value));
            }
        }
    }
    return result;
}

std::string TextElement::DisplayText() const {
    return Transformed(text);
}

Size TextElement::Measure(TextMeasurer& measurer, float availableWidth, float availableHeight, bool heightDefinite) {
    ResolveRole(ViewportWidth());
    if (!Children().empty()) {
        return Frame::Measure(measurer, availableWidth, availableHeight, heightDefinite);
    }
    const float widthSpace = Width().unit == LengthUnit::SCREEN && ViewportWidth() > 0.0f ? ViewportWidth() : availableWidth;
    const float heightSpace = Height().unit == LengthUnit::SCREEN && ViewportHeight() > 0.0f ? ViewportHeight() : availableHeight;
    const float measureWidth = Width().unit == LengthUnit::PIXELS || Width().unit == LengthUnit::PERCENT || Width().unit == LengthUnit::FILL ||
                                       Width().unit == LengthUnit::SCREEN
                                   ? Width().Resolve(widthSpace, availableWidth)
                                   : (availableWidth > 1.0f ? availableWidth : 100000.0f);
    const float contentWidth = std::max(1.0f, measureWidth);
    const Size textSize = measurer.MeasureText(DisplayText(), fontSize, wrapText ? contentWidth : 100000.0f);
    const float block = std::max(fontSize * (lineHeight > 0.0f ? lineHeight : 1.0f), textSize.height);
    const Edges pad = Padding();
    float resolvedWidth = std::max(8.0f, textSize.width) + pad.SpanX(contentWidth);
    float resolvedHeight = block + pad.SpanY(contentWidth);
    if (Width().unit == LengthUnit::PIXELS || Width().unit == LengthUnit::PERCENT || Width().unit == LengthUnit::FILL || Width().unit == LengthUnit::SCREEN) {
        resolvedWidth = Width().Resolve(widthSpace, resolvedWidth);
    }
    if (Height().unit == LengthUnit::PIXELS || Height().unit == LengthUnit::PERCENT || Height().unit == LengthUnit::FILL || Height().unit == LengthUnit::SCREEN) {
        resolvedHeight = Height().Resolve(heightSpace, resolvedHeight);
    }
    if (maxLines > 0) {
        resolvedHeight = std::min(resolvedHeight, fontSize * lineHeight * static_cast<float>(maxLines) + pad.SpanY(contentWidth));
    }
    measured = Size{std::max(1.0f, resolvedWidth), std::max(1.0f, resolvedHeight)};
    return FinishMeasure(widthSpace, heightSpace);
}

void TextElement::OnPaint(Renderer& renderer) {
    Frame::OnPaint(renderer);
    if (Display() == FrameDisplay::NONE || text.empty()) {
        return;
    }
    const Edges pad = Padding();
    Rect area = Bounds();
    const float left = pad.left.Resolve(area.width, 0.0f);
    const float right = pad.right.Resolve(area.width, 0.0f);
    const float top = pad.top.Resolve(area.width, 0.0f);
    const float bottom = pad.bottom.Resolve(area.width, 0.0f);
    area.x += left;
    area.y += top;
    area.width = std::max(0.0f, area.width - left - right);
    area.height = std::max(0.0f, area.height - top - bottom);
    TextStyle style;
    style.fontSize = fontSize;
    style.fontWeight = fontWeight;
    style.color = textColor;
    style.horizontal = textAlign;
    style.vertical = verticalAlign;
    style.fontFamily = fontFamily;
    style.fontFile = fontFile;
    style.locale = locale;
    style.italic = italic;
    style.rtl = rtl;
    style.wrap = wrapText;
    style.letterSpacing = letterSpacingEm * fontSize;
    style.wordSpacing = wordSpacing;
    style.lineHeight = lineHeight;
    style.maxLines = maxLines;
    style.ellipsis = ellipsis;
    style.underline = underline;
    style.lineThrough = lineThrough;
    style.overline = overline;
    style.decorationColor = decorationColor;
    style.decorationWidth = decorationWidth;
    style.decorationStyle = decorationStyle;
    style.fontFeatures = fontFeatures;
    style.verticalWriting = writingMode == "vertical";
    renderer.DrawString(DisplayText(), area, style);
}

void TextElement::ApplyBound(const std::string& property, const std::string& value) {
    Frame::ApplyBound(property, value);
    if (property == "text" || property == "value") {
        text = value;
    } else if (property == "textColor") {
        if (const std::optional<Color> color = ParseColor(value)) {
            textColor = *color;
            colorSpecified = true;
        }
    }
}

}  // namespace Runtime
