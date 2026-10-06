#include "components/primitive/PrimitiveElement.h"

#include "compiler/types/ValueParser.h"
#include "rendering/image/ImageDecoder.h"
#include "rendering/path/PathCurve.h"
#include "rendering/renderer/Renderer.h"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace Runtime {
namespace {

void StrokeSegment(Renderer& renderer, float x1, float y1, float x2, float y2, Color color, float width) {
    const float dx = x2 - x1;
    const float dy = y2 - y1;
    const float steps = std::max(1.0f, std::max(std::fabs(dx), std::fabs(dy)));
    const int count = static_cast<int>(steps);
    for (int index = 0; index <= count; ++index) {
        const float t = static_cast<float>(index) / steps;
        renderer.FillRectangle(Rect{x1 + dx * t, y1 + dy * t, std::max(1.0f, width), std::max(1.0f, width)}, color, 0.0f);
    }
}

void StrokePath(Renderer& renderer, std::string_view data, const Rect& bounds, Color color, float width) {
    const std::vector<PathSample> points = SamplePath(data);
    for (std::size_t index = 1; index < points.size(); ++index) {
        StrokeSegment(renderer, bounds.x + points[index - 1].x, bounds.y + points[index - 1].y, bounds.x + points[index].x,
                      bounds.y + points[index].y, color, width);
    }
}

}  // namespace

ShapeElement::ShapeElement(NodeKind kind) : nodeKind(kind) {}

NodeKind ShapeElement::Kind() const {
    return nodeKind;
}

void ShapeElement::SetText(std::string value) {
    text = std::move(value);
}

const std::string& ShapeElement::Text() const {
    return text;
}

void ShapeElement::SetSource(std::string value) {
    source = std::move(value);
    imageReady = false;
    decoded = {};
}

void ShapeElement::SetImageFit(std::string value) {
    imageFit = std::move(value);
}

void ShapeElement::SetImageClip(bool value) {
    imageClip = value;
}

void ShapeElement::EnsureImage() {
    if (imageReady || nodeKind != NodeKind::IMAGE || source.empty()) {
        return;
    }
    imageReady = true;
    DecodeImageFile(source, decoded);
}

void ShapeElement::SetPathData(std::string value) {
    pathData = std::move(value);
}

void ShapeElement::SetShapeName(std::string value) {
    shapeName = std::move(value);
}

void ShapeElement::SetFill(Color value) {
    fill = value;
    fillSpecified = true;
}

void ShapeElement::SetViewBox(float x, float y, float boxWidth, float boxHeight) {
    viewX = x;
    viewY = y;
    viewWidth = boxWidth;
    viewHeight = boxHeight;
}

void ShapeElement::SetLineStyle(const std::string& cap, const std::string& join, bool odd) {
    lineCap = cap == "round" ? 'r' : cap == "square" ? 's' : 'b';
    lineJoin = join == "round" ? 'r' : join == "bevel" ? 'b' : 'm';
    evenOdd = odd;
}

void ShapeElement::SetStroke(Color value, float thickness) {
    stroke = value;
    strokeWidth = thickness < 0.0f ? 0.0f : thickness;
}

void ShapeElement::SetFont(float size, int weight, Color color, TextAlign align) {
    fontSize = size < 1.0f ? 1.0f : size;
    fontWeight = weight;
    textColor = color;
    textAlign = align;
}

void ShapeElement::SetTextColor(Color value) {
    textColor = value;
}

void ShapeElement::SetUnderline(bool value) {
    underline = value;
}

void ShapeElement::SetSpin(float degrees) {
    spin = degrees;
}

void ShapeElement::SetTextStyle(std::string family, bool italicValue, bool rtlValue, std::string localeValue) {
    fontFamily = std::move(family);
    italic = italicValue;
    rtl = rtlValue;
    if (!localeValue.empty()) {
        locale = std::move(localeValue);
    }
}

void ShapeElement::SetLine(float startX, float startY, float endX, float endY) {
    lineX1 = startX;
    lineY1 = startY;
    lineX2 = endX;
    lineY2 = endY;
}

void ShapeElement::SetCornerRadius(float value) {
    cornerRadius = value < 0.0f ? 0.0f : value;
}

bool ShapeElement::PlacesChildren() const {
    return nodeKind == NodeKind::GROUP || nodeKind == NodeKind::VECTOR || nodeKind == NodeKind::COMPONENT || nodeKind == NodeKind::INSTANCE || nodeKind == NodeKind::SLOT;
}

Size ShapeElement::Measure(TextMeasurer& measurer, float availableWidth, float availableHeight, bool) {
    float resolvedWidth = 80.0f;
    float resolvedHeight = 32.0f;
    if (nodeKind == NodeKind::TEXT) {
        const Size textSize = measurer.MeasureText(text, fontSize, availableWidth > 1.0f ? availableWidth : 100000.0f);
        resolvedWidth = std::max(8.0f, textSize.width + 4.0f);
        resolvedHeight = std::max(fontSize, textSize.height);
    } else if (nodeKind == NodeKind::IMAGE) {
        EnsureImage();
        if (!decoded.frames.empty() && decoded.frames.front().width > 0 && decoded.frames.front().height > 0) {
            resolvedWidth = static_cast<float>(decoded.frames.front().width);
            resolvedHeight = static_cast<float>(decoded.frames.front().height);
        } else {
            resolvedWidth = 120.0f;
            resolvedHeight = 80.0f;
        }
    } else if (nodeKind == NodeKind::LINE) {
        resolvedWidth = std::max(strokeWidth, std::fabs(lineX2 - lineX1));
        resolvedHeight = std::max(strokeWidth, std::fabs(lineY2 - lineY1));
    } else if (PlacesChildren()) {
        resolvedWidth = 8.0f;
        resolvedHeight = 8.0f;
        for (const std::unique_ptr<Element>& child : Children()) {
            child->Measure(measurer, availableWidth, availableHeight, false);
            const float childRight = child->OffsetX().Resolve(availableWidth, 0.0f, true) + child->Measured().width;
            const float childBottom = child->OffsetY().Resolve(availableHeight, 0.0f, true) + child->Measured().height;
            resolvedWidth = std::max(resolvedWidth, childRight);
            resolvedHeight = std::max(resolvedHeight, childBottom);
        }
    }
    if (Width().unit == LengthUnit::PIXELS || Width().unit == LengthUnit::PERCENT || Width().unit == LengthUnit::FILL) {
        resolvedWidth = Width().Resolve(availableWidth, resolvedWidth);
    }
    if (Height().unit == LengthUnit::PIXELS || Height().unit == LengthUnit::PERCENT || Height().unit == LengthUnit::FILL) {
        resolvedHeight = Height().Resolve(availableHeight, resolvedHeight);
    }
    measured = Size{std::max(1.0f, resolvedWidth), std::max(1.0f, resolvedHeight)};
    return FinishMeasure(availableWidth, availableHeight);
}

void ShapeElement::Arrange(const Rect& rect) {
    Element::Arrange(rect);
    if (!PlacesChildren()) {
        return;
    }
    for (const std::unique_ptr<Element>& child : Children()) {
        const float childX = Bounds().x + child->OffsetX().Resolve(Bounds().width, 0.0f, true);
        const float childY = Bounds().y + child->OffsetY().Resolve(Bounds().height, 0.0f, true);
        child->Arrange(Rect{childX, childY, child->Measured().width, child->Measured().height});
    }
}

void ShapeElement::ApplyBound(const std::string& property, const std::string& value) {
    Element::ApplyBound(property, value);
    if (property == "text" || property == "value") {
        text = value;
    } else if (property == "textColor" || property == "stroke" || property == "fill") {
        if (const std::optional<Color> color = ParseColor(value)) {
            if (property == "textColor") {
                textColor = *color;
            } else if (property == "stroke") {
                stroke = *color;
            } else {
                fill = *color;
            }
        }
    } else if (property == "underline") {
        underline = value != "false" && value != "0" && value != "hidden";
    }
}

Element* ShapeElement::HitTest(Point point) {
    if (!Visible()) {
        return nullptr;
    }
    if (Element* child = HitTestChildren(point)) {
        return child;
    }
    if (point.x >= Bounds().x && point.y >= Bounds().y && point.x <= Bounds().x + Bounds().width && point.y <= Bounds().y + Bounds().height) {
        return this;
    }
    return nullptr;
}

void ShapeElement::OnPaint(Renderer& renderer) {
    const Rect area = Bounds();
    if (nodeKind == NodeKind::TEXT) {
        TextStyle style;
        style.fontSize = fontSize;
        style.fontWeight = fontWeight;
        style.color = textColor;
        style.horizontal = textAlign;
        style.vertical = TextAlign::CENTER;
        style.fontFamily = fontFamily;
        style.locale = locale;
        style.italic = italic;
        style.rtl = rtl;
        renderer.DrawString(text, area, style);
        if (underline && area.width > 0.0f) {
            const float baseline = area.y + area.height - 2.0f;
            renderer.FillRectangle(Rect{area.x, baseline, area.width, 1.0f}, textColor, 0.0f);
        }
        return;
    }
    if (nodeKind == NodeKind::IMAGE) {
        EnsureImage();
        if (!decoded.frames.empty()) {
            int frameIndex = 0;
            if (decoded.frames.size() > 1) {
                unsigned total = 0;
                for (const ImageFrame& candidate : decoded.frames) {
                    total += static_cast<unsigned>(candidate.delayMs > 0 ? candidate.delayMs : 100);
                }
                unsigned at = total == 0 ? 0 : static_cast<unsigned>(GetTickCount64() % total);
                unsigned cursor = 0;
                for (std::size_t index = 0; index < decoded.frames.size(); ++index) {
                    const int delay = decoded.frames[index].delayMs > 0 ? decoded.frames[index].delayMs : 100;
                    cursor += static_cast<unsigned>(delay);
                    if (at < cursor) {
                        frameIndex = static_cast<int>(index);
                        break;
                    }
                }
            }
            const ImageFrame& frame = decoded.frames[static_cast<std::size_t>(frameIndex)];
            Rect dest = area;
            if (frame.width > 0 && frame.height > 0 && area.width > 0.0f && area.height > 0.0f && imageFit != "fill") {
                const float scaleX = area.width / static_cast<float>(frame.width);
                const float scaleY = area.height / static_cast<float>(frame.height);
                float scale = 1.0f;
                if (imageFit == "cover") {
                    scale = std::max(scaleX, scaleY);
                } else if (imageFit == "none") {
                    scale = 1.0f;
                } else if (imageFit == "scaleDown") {
                    scale = std::min(1.0f, std::min(scaleX, scaleY));
                } else {
                    scale = std::min(scaleX, scaleY);
                }
                dest.width = static_cast<float>(frame.width) * scale;
                dest.height = static_cast<float>(frame.height) * scale;
                dest.x = area.x + (area.width - dest.width) * 0.5f;
                dest.y = area.y + (area.height - dest.height) * 0.5f;
            }
            if (imageClip || imageFit == "cover") {
                renderer.PushClip(area);
            }
            renderer.DrawCachedImage(source, dest, frame.bgra.data(), frame.width, frame.height);
            if (imageClip || imageFit == "cover") {
                renderer.PopClip();
            }
            return;
        }
        renderer.FillRectangle(area, Color{0.91f, 0.92f, 0.93f, 1.0f}, cornerRadius);
        renderer.StrokeRectangle(area, stroke, 1.0f, cornerRadius);
        TextStyle style;
        style.fontSize = 12.0f;
        style.color = textColor;
        style.horizontal = TextAlign::CENTER;
        style.vertical = TextAlign::CENTER;
        renderer.DrawString(source.empty() ? "Image" : source, area, style);
        return;
    }
    if (nodeKind == NodeKind::VECTOR && !Children().empty()) {
        if (fillSpecified && fill.a > 0.0f && area.width > 0.0f && area.height > 0.0f) {
            renderer.FillRectangle(area, fill, cornerRadius);
        }
        PaintChildren(renderer);
        return;
    }
    if (nodeKind == NodeKind::VECTOR && shapeName == "spinnerArc") {
        const float box = std::min(area.width, area.height);
        if (box <= 1.0f) {
            return;
        }
        const float viewStroke = std::clamp(strokeWidth * 32.0f / box, 0.75f, 14.0f);
        const float radius = std::max(1.0f, 16.0f - viewStroke);
        char buffer[160];
        std::snprintf(buffer, sizeof(buffer),
                      "M 16 %.3f A %.3f %.3f 0 1 1 16 %.3f A %.3f %.3f 0 1 1 16 %.3f Z",
                      16.0f - radius, radius, radius, 16.0f + radius, radius, radius, 16.0f - radius);
        const std::string trackPath = buffer;
        const float start = spin * 0.01745329252f;
        const float end = start + 0.28f * 6.28318530718f;
        const float x0 = 16.0f + std::cos(start) * radius;
        const float y0 = 16.0f + std::sin(start) * radius;
        const float x1 = 16.0f + std::cos(end) * radius;
        const float y1 = 16.0f + std::sin(end) * radius;
        std::snprintf(buffer, sizeof(buffer), "M %.3f %.3f A %.3f %.3f 0 0 1 %.3f %.3f", x0, y0, radius, radius, x1, y1);
        const std::string arcPath = buffer;
        Color track = fillSpecified ? fill : stroke;
        if (!fillSpecified) {
            track.a *= 0.35f;
        }
        VectorPaint paint;
        paint.bounds = area;
        paint.viewWidth = 32.0f;
        paint.viewHeight = 32.0f;
        paint.strokeWidth = viewStroke;
        paint.fillEnabled = false;
        paint.data = trackPath;
        paint.stroke = track;
        paint.lineCap = 'b';
        renderer.DrawVector(paint);
        paint.data = arcPath;
        paint.stroke = stroke;
        paint.lineCap = 'r';
        renderer.DrawVector(paint);
        return;
    }
    if (nodeKind == NodeKind::VECTOR) {
        const float radius = shapeName == "ellipse" ? std::min(area.width, area.height) * 0.5f : cornerRadius;
        renderer.FillRectangle(area, fill, radius);
        if (strokeWidth > 0.0f) {
            renderer.StrokeRectangle(area, stroke, strokeWidth, radius);
        }
        return;
    }
    if (nodeKind == NodeKind::PATH) {
        VectorPaint paint;
        paint.bounds = area;
        paint.data = pathData;
        paint.viewX = viewX;
        paint.viewY = viewY;
        paint.viewWidth = viewWidth;
        paint.viewHeight = viewHeight;
        paint.fill = fill;
        paint.stroke = stroke;
        paint.strokeWidth = strokeWidth;
        paint.evenOdd = evenOdd;
        paint.fillEnabled = fillSpecified && fill.a > 0.0f;
        paint.lineCap = lineCap;
        paint.lineJoin = lineJoin;
        renderer.DrawVector(paint);
        return;
    }
    if (nodeKind == NodeKind::LINE) {
        const float left = std::min(lineX1, lineX2);
        const float top = std::min(lineY1, lineY2);
        StrokeSegment(renderer, area.x + (lineX1 - left), area.y + (lineY1 - top), area.x + (lineX2 - left), area.y + (lineY2 - top), stroke, strokeWidth);
        return;
    }
    if (nodeKind == NodeKind::COMPONENT || nodeKind == NodeKind::SLOT) {
        renderer.StrokeRectangle(area, stroke, 1.0f, 0.0f);
        if (nodeKind == NodeKind::SLOT) {
            TextStyle style;
            style.fontSize = 12.0f;
            style.color = textColor;
            style.horizontal = TextAlign::START;
            style.vertical = TextAlign::CENTER;
            renderer.DrawString(text.empty() ? "Slot" : text, area, style);
        }
    }
    PaintChildren(renderer);
}

ScrollElement::ScrollElement() {
    SetOverflow(OverflowMode::SCROLL);
    SetLayout(FrameLayout::VERTICAL);
}

NodeKind ScrollElement::Kind() const {
    return NodeKind::SCROLL;
}

}  // namespace Runtime
