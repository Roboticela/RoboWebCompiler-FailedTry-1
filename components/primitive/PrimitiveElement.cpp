#include "components/primitive/PrimitiveElement.h"

#include "rendering/renderer/Renderer.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>

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
    float currentX = 0.0f;
    float currentY = 0.0f;
    float startX = 0.0f;
    float startY = 0.0f;
    std::size_t index = 0;
    auto readNumber = [&]() {
        while (index < data.size() && (data[index] == ' ' || data[index] == ',')) {
            ++index;
        }
        const std::size_t start = index;
        if (index < data.size() && (data[index] == '-' || data[index] == '+')) {
            ++index;
        }
        while (index < data.size() && (std::isdigit(static_cast<unsigned char>(data[index])) || data[index] == '.')) {
            ++index;
        }
        if (start == index) {
            return 0.0f;
        }
        return std::strtof(std::string(data.substr(start, index - start)).c_str(), nullptr);
    };
    while (index < data.size()) {
        const char command = data[index];
        if (command == ' ' || command == ',') {
            ++index;
            continue;
        }
        ++index;
        if (command == 'M' || command == 'm') {
            currentX = readNumber();
            currentY = readNumber();
            startX = currentX;
            startY = currentY;
        } else if (command == 'L' || command == 'l') {
            const float nextX = readNumber();
            const float nextY = readNumber();
            StrokeSegment(renderer, bounds.x + currentX, bounds.y + currentY, bounds.x + nextX, bounds.y + nextY, color, width);
            currentX = nextX;
            currentY = nextY;
        } else if (command == 'H' || command == 'h') {
            const float nextX = readNumber();
            StrokeSegment(renderer, bounds.x + currentX, bounds.y + currentY, bounds.x + nextX, bounds.y + currentY, color, width);
            currentX = nextX;
        } else if (command == 'V' || command == 'v') {
            const float nextY = readNumber();
            StrokeSegment(renderer, bounds.x + currentX, bounds.y + currentY, bounds.x + currentX, bounds.y + nextY, color, width);
            currentY = nextY;
        } else if (command == 'Z' || command == 'z') {
            StrokeSegment(renderer, bounds.x + currentX, bounds.y + currentY, bounds.x + startX, bounds.y + startY, color, width);
            currentX = startX;
            currentY = startY;
        }
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

void ShapeElement::SetSource(std::string value) {
    source = std::move(value);
}

void ShapeElement::SetPathData(std::string value) {
    pathData = std::move(value);
}

void ShapeElement::SetShapeName(std::string value) {
    shapeName = std::move(value);
}

void ShapeElement::SetFill(Color value) {
    fill = value;
}

void ShapeElement::SetStroke(Color value, float width) {
    stroke = value;
    strokeWidth = width < 0.0f ? 0.0f : width;
}

void ShapeElement::SetFont(float size, int weight, Color color, TextAlign align) {
    fontSize = size < 1.0f ? 1.0f : size;
    fontWeight = weight;
    textColor = color;
    textAlign = align;
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
    return nodeKind == NodeKind::GROUP || nodeKind == NodeKind::COMPONENT || nodeKind == NodeKind::INSTANCE || nodeKind == NodeKind::SLOT;
}

Size ShapeElement::Measure(TextMeasurer& measurer, float availableWidth, float availableHeight, bool) {
    float resolvedWidth = 80.0f;
    float resolvedHeight = 32.0f;
    if (nodeKind == NodeKind::TEXT) {
        const Size textSize = measurer.MeasureText(text, fontSize, availableWidth > 1.0f ? availableWidth : 100000.0f);
        resolvedWidth = std::max(8.0f, textSize.width + 4.0f);
        resolvedHeight = std::max(fontSize, textSize.height);
    } else if (nodeKind == NodeKind::IMAGE) {
        resolvedWidth = 120.0f;
        resolvedHeight = 80.0f;
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
        renderer.DrawString(text, area, style);
        return;
    }
    if (nodeKind == NodeKind::IMAGE) {
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
    if (nodeKind == NodeKind::VECTOR) {
        const float radius = shapeName == "ellipse" ? std::min(area.width, area.height) * 0.5f : cornerRadius;
        renderer.FillRectangle(area, fill, radius);
        if (strokeWidth > 0.0f) {
            renderer.StrokeRectangle(area, stroke, strokeWidth, radius);
        }
        return;
    }
    if (nodeKind == NodeKind::PATH) {
        StrokePath(renderer, pathData, area, stroke, strokeWidth);
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
