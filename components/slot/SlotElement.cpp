#include "components/slot/SlotElement.h"

#include "rendering/renderer/Renderer.h"
#include "rendering/text/TextStyle.h"

#include <algorithm>
#include <utility>

namespace Runtime {
namespace {

// The violet a slot is marked with, the same violet a definition and its copies are marked with,
// because a slot is one of the parts a definition is built out of.
inline constexpr Color SLOT_ACCENT{0.592f, 0.278f, 1.0f, 1.0f};
inline constexpr Color SLOT_LABEL{0.420f, 0.208f, 0.720f, 1.0f};
inline constexpr float SLOT_LABEL_FONT_SIZE = 12.0f;

}  // namespace

NodeKind SlotElement::Kind() const { return NodeKind::SLOT; }

void SlotElement::SetFallbackText(std::string value) { fallbackText = std::move(value); }
const std::string& SlotElement::FallbackText() const { return fallbackText; }

bool SlotElement::IsFilled() const {
    for (const std::unique_ptr<Element>& child : Children()) {
        if (child->Visible()) {
            return true;
        }
    }
    return false;
}

void SlotElement::SetOutlineVisible(bool value) { outlineVisible = value; }
bool SlotElement::OutlineVisible() const { return outlineVisible; }

void SlotElement::OnPaint(Renderer& renderer) {
    Frame::OnPaint(renderer);
    if (!outlineVisible || Display() == FrameDisplay::NONE || IsFilled()) {
        return;
    }
    const Rect area = Bounds();
    renderer.StrokeRectangle(area, SLOT_ACCENT, 1.0f, 2.0f);
    if (fallbackText.empty() || area.height < SLOT_LABEL_FONT_SIZE) {
        return;
    }
    TextStyle style;
    style.fontSize = SLOT_LABEL_FONT_SIZE;
    style.color = SLOT_LABEL;
    style.horizontal = TextAlign::CENTER;
    style.vertical = TextAlign::CENTER;
    style.maxLines = 1;
    renderer.DrawString(fallbackText, Rect{area.x + 4.0f, area.y, std::max(0.0f, area.width - 8.0f), area.height}, style);
}

}  // namespace Runtime
