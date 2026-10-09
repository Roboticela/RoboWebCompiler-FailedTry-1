#include "components/component/ComponentElement.h"

#include "rendering/renderer/Renderer.h"

#include <utility>

namespace Runtime {
namespace {

// The violet a definition is marked with, so a master is told apart from the frame it is built out
// of. The mark inside it is drawn in the paper colour of the canvas, which reads on both a light
// and a dark background.
inline constexpr Color COMPONENT_ACCENT{0.592f, 0.278f, 1.0f, 1.0f};
inline constexpr Color COMPONENT_MARK{1.0f, 1.0f, 1.0f, 1.0f};
inline constexpr float COMPONENT_BADGE_SIZE = 10.0f;
inline constexpr float COMPONENT_BADGE_INSET = 4.0f;

// A definition is marked with one square and a set, which holds several, with two overlapping ones.
// A box too small to hold the mark is left unmarked rather than marked across its whole face.
void PaintComponentBadge(Renderer& renderer, const Rect& bounds, bool set) {
    const float needed = COMPONENT_BADGE_SIZE + COMPONENT_BADGE_INSET * 2.0f;
    if (bounds.width < needed || bounds.height < needed) {
        return;
    }
    const float x = bounds.x + COMPONENT_BADGE_INSET;
    const float y = bounds.y + COMPONENT_BADGE_INSET;
    if (set) {
        renderer.FillRectangle(Rect{x + COMPONENT_BADGE_SIZE * 0.5f, y, COMPONENT_BADGE_SIZE, COMPONENT_BADGE_SIZE}, COMPONENT_ACCENT, 1.0f);
        renderer.FillRectangle(Rect{x, y + COMPONENT_BADGE_SIZE * 0.5f, COMPONENT_BADGE_SIZE, COMPONENT_BADGE_SIZE}, COMPONENT_ACCENT, 1.0f);
        renderer.FillRectangle(Rect{x + COMPONENT_BADGE_SIZE * 0.5f, y + COMPONENT_BADGE_SIZE * 0.5f, COMPONENT_BADGE_SIZE * 0.5f,
                                    COMPONENT_BADGE_SIZE * 0.5f},
                               COMPONENT_MARK, 0.0f);
        return;
    }
    renderer.FillRectangle(Rect{x, y, COMPONENT_BADGE_SIZE, COMPONENT_BADGE_SIZE}, COMPONENT_ACCENT, 1.0f);
    renderer.FillRectangle(Rect{x + COMPONENT_BADGE_SIZE * 0.25f, y + COMPONENT_BADGE_SIZE * 0.25f, COMPONENT_BADGE_SIZE * 0.5f,
                                COMPONENT_BADGE_SIZE * 0.5f},
                           COMPONENT_MARK, 0.0f);
}

}  // namespace

NodeKind ComponentElement::Kind() const { return NodeKind::COMPONENT; }

void ComponentElement::SetVariant(std::string value) { variant = std::move(value); }
const std::string& ComponentElement::Variant() const { return variant; }
bool ComponentElement::HasVariant() const { return !variant.empty(); }

void ComponentElement::SetDescription(std::string value) { description = std::move(value); }
const std::string& ComponentElement::Description() const { return description; }

void ComponentElement::SetBadgeVisible(bool value) { badgeVisible = value; }
bool ComponentElement::BadgeVisible() const { return badgeVisible; }

void ComponentElement::OnPaint(Renderer& renderer) {
    Frame::OnPaint(renderer);
    if (!badgeVisible || Display() == FrameDisplay::NONE) {
        return;
    }
    PaintComponentBadge(renderer, Bounds(), false);
}

NodeKind ComponentSetElement::Kind() const { return NodeKind::COMPONENTSET; }

void ComponentSetElement::SetDescription(std::string value) { description = std::move(value); }
const std::string& ComponentSetElement::Description() const { return description; }

int ComponentSetElement::VariantCount() const {
    int count = 0;
    for (const std::unique_ptr<Element>& child : Children()) {
        if (dynamic_cast<const ComponentElement*>(child.get()) != nullptr) {
            ++count;
        }
    }
    return count;
}

ComponentElement* ComponentSetElement::VariantAt(int index) {
    if (index < 0) {
        return nullptr;
    }
    int at = 0;
    for (const std::unique_ptr<Element>& child : Children()) {
        ComponentElement* variant = dynamic_cast<ComponentElement*>(child.get());
        if (variant == nullptr) {
            continue;
        }
        if (at == index) {
            return variant;
        }
        ++at;
    }
    return nullptr;
}

ComponentElement* ComponentSetElement::VariantNamed(std::string_view variantName) {
    ComponentElement* first = nullptr;
    for (const std::unique_ptr<Element>& child : Children()) {
        ComponentElement* variant = dynamic_cast<ComponentElement*>(child.get());
        if (variant == nullptr) {
            continue;
        }
        if (first == nullptr) {
            first = variant;
        }
        if (!variantName.empty() && std::string_view(variant->Variant()) == variantName) {
            return variant;
        }
    }
    return first;
}

void ComponentSetElement::SetBadgeVisible(bool value) { badgeVisible = value; }
bool ComponentSetElement::BadgeVisible() const { return badgeVisible; }

void ComponentSetElement::OnPaint(Renderer& renderer) {
    Frame::OnPaint(renderer);
    if (!badgeVisible || Display() == FrameDisplay::NONE) {
        return;
    }
    PaintComponentBadge(renderer, Bounds(), true);
}

}  // namespace Runtime
