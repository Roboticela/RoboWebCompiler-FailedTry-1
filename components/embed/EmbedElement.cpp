#include "components/embed/EmbedElement.h"

#include "rendering/renderer/Renderer.h"
#include "rendering/text/TextStyle.h"

#include <algorithm>
#include <utility>

namespace Runtime {
namespace {

// The teal an embedded document is marked with, which reads as a document that is here but was
// written somewhere else. It is deliberately not the violet of a definition: a definition is
// something this document offers, and an embed is something this document was built from.
inline constexpr Color EMBED_ACCENT{0.055f, 0.647f, 0.702f, 1.0f};
inline constexpr Color EMBED_MARK{1.0f, 1.0f, 1.0f, 1.0f};
inline constexpr Color EMBED_LABEL{0.145f, 0.451f, 0.478f, 1.0f};
inline constexpr float EMBED_BADGE_SIZE = 10.0f;
inline constexpr float EMBED_BADGE_INSET = 4.0f;
inline constexpr float EMBED_LABEL_FONT_SIZE = 12.0f;

// The mark of an embedded document: one square with a fold in its corner, which is the shape a
// document is drawn as. A box too small to hold it is left unmarked rather than marked across its
// whole face.
void PaintEmbedBadge(Renderer& renderer, const Rect& bounds) {
    const float needed = EMBED_BADGE_SIZE + EMBED_BADGE_INSET * 2.0f;
    if (bounds.width < needed || bounds.height < needed) {
        return;
    }
    const float x = bounds.x + EMBED_BADGE_INSET;
    const float y = bounds.y + EMBED_BADGE_INSET;
    renderer.FillRectangle(Rect{x, y, EMBED_BADGE_SIZE, EMBED_BADGE_SIZE}, EMBED_ACCENT, 1.0f);
    const float fold = EMBED_BADGE_SIZE * 0.45f;
    renderer.FillRectangle(Rect{x + EMBED_BADGE_SIZE - fold, y, fold, fold}, EMBED_MARK, 0.0f);
    renderer.FillRectangle(Rect{x + EMBED_BADGE_SIZE - fold, y + fold * 0.5f, fold * 0.5f, fold * 0.5f}, EMBED_ACCENT, 0.0f);
}

}  // namespace

NodeKind EmbedElement::Kind() const { return NodeKind::EMBED; }

void EmbedElement::SetSource(std::string value) { source = std::move(value); }
const std::string& EmbedElement::Source() const { return source; }

// What a document is for is drawn where every other description in the document is drawn: as the tip
// a layer shows while it is hovered.
void EmbedElement::SetDescription(std::string value) {
    description = value;
    SetTip(value);
}
const std::string& EmbedElement::Description() const { return description; }

void EmbedElement::SetBadgeVisible(bool value) { badgeVisible = value; }
bool EmbedElement::BadgeVisible() const { return badgeVisible; }

void EmbedElement::OnPaint(Renderer& renderer) {
    Frame::OnPaint(renderer);
    if (Display() == FrameDisplay::NONE) {
        return;
    }
    const Rect area = Bounds();
    bool holdsContent = false;
    for (const std::unique_ptr<Element>& child : Children()) {
        if (child->Visible()) {
            holdsContent = true;
            break;
        }
    }
    // An embed that the document named no file for holds nothing, and a box that looks as if it were
    // meant to be empty is a box nobody can tell from one that was. It draws its outline and the name
    // it asked for instead, so an embed waiting for a file says so.
    if (!holdsContent) {
        renderer.StrokeRectangle(area, EMBED_ACCENT, 1.0f, 2.0f);
        if (source.empty() || area.height < EMBED_LABEL_FONT_SIZE) {
            return;
        }
        TextStyle style;
        style.fontSize = EMBED_LABEL_FONT_SIZE;
        style.color = EMBED_LABEL;
        style.horizontal = TextAlign::CENTER;
        style.vertical = TextAlign::CENTER;
        style.maxLines = 1;
        renderer.DrawString(source, Rect{area.x + 4.0f, area.y, std::max(0.0f, area.width - 8.0f), area.height}, style);
        return;
    }
    if (badgeVisible) {
        PaintEmbedBadge(renderer, area);
    }
}

}  // namespace Runtime
