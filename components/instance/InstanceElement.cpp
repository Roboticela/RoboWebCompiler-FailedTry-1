#include "components/instance/InstanceElement.h"

#include "rendering/renderer/Renderer.h"

#include <algorithm>
#include <utility>

namespace Runtime {
namespace {

// The violet a definition and its copies are marked with. A copy that names a definition the
// document does not hold is marked with it too, because the mark says which layer the node belongs
// to and the box itself says the copy could not be filled.
inline constexpr Color INSTANCE_ACCENT{0.592f, 0.278f, 1.0f, 1.0f};
inline constexpr Color INSTANCE_MARK{1.0f, 1.0f, 1.0f, 1.0f};

}  // namespace

NodeKind InstanceElement::Kind() const { return NodeKind::INSTANCE; }

void InstanceElement::SetVariant(std::string value) { variant = std::move(value); }
const std::string& InstanceElement::Variant() const { return variant; }
bool InstanceElement::HasVariant() const { return !variant.empty(); }

void InstanceElement::SetResolved(bool value) { resolved = value; }
bool InstanceElement::Resolved() const { return resolved; }

void InstanceElement::SetMainInstance(bool value) { mainInstance = value; }
bool InstanceElement::MainInstance() const { return mainInstance; }

void InstanceElement::SetDetached(bool value) { detached = value; }
bool InstanceElement::Detached() const { return detached; }

void InstanceElement::SetOverrideCount(int value) { overrideCount = value < 0 ? 0 : value; }
int InstanceElement::OverrideCount() const { return overrideCount; }

void InstanceElement::OnPaint(Renderer& renderer) {
    Frame::OnPaint(renderer);
    if (resolved || Display() == FrameDisplay::NONE) {
        return;
    }
    // A copy with no master has nothing to show, so its box is outlined rather than left invisible:
    // what is wrong is then on the canvas instead of only in the diagnostic list.
    const Rect area = Bounds();
    renderer.StrokeRectangle(area, INSTANCE_ACCENT, 1.0f, 2.0f);
    const float size = std::min(12.0f, std::min(area.width, area.height) * 0.5f);
    if (size < 2.0f) {
        return;
    }
    renderer.FillRectangle(Rect{area.x + (area.width - size) * 0.5f, area.y + (area.height - size) * 0.5f, size, size},
                           INSTANCE_ACCENT, 1.0f);
    renderer.FillRectangle(Rect{area.x + (area.width - size) * 0.25f, area.y + (area.height - size) * 0.25f, size * 0.5f, size * 0.5f},
                           INSTANCE_MARK, 0.0f);
}

}  // namespace Runtime
