#include "components/link/LinkElement.h"

#include "rendering/renderer/Renderer.h"
#include "rendering/text/TextStyle.h"

#include <algorithm>
#include <utility>

namespace Runtime {
namespace {

// The blue a link is drawn in, so a link that was given no colour of its own still reads as a link.
// A document that wrote a colour of its own keeps it: this is the default, not a rule.
inline constexpr Color LINK_LABEL{0.055f, 0.478f, 1.0f, 1.0f};

bool HoldsContent(const Element& element) {
    for (const std::unique_ptr<Element>& child : element.Children()) {
        if (child->Visible()) {
            return true;
        }
    }
    return false;
}

}  // namespace

NodeKind LinkElement::Kind() const { return NodeKind::LINK; }

void LinkElement::SetHref(std::string value) { href = std::move(value); }
const std::string& LinkElement::Href() const { return href; }
bool LinkElement::HasHref() const { return !href.empty(); }

void LinkElement::SetTo(std::string value) { to = std::move(value); }
const std::string& LinkElement::To() const { return to; }
bool LinkElement::HasTo() const { return !to.empty(); }

void LinkElement::SetTarget(std::string value) { target = std::move(value); }
const std::string& LinkElement::Target() const { return target; }

void LinkElement::SetLabel(std::string value) { label = std::move(value); }
const std::string& LinkElement::Label() const { return label; }

void LinkElement::SetDescription(std::string value) {
    description = value;
    SetTip(value);
}
const std::string& LinkElement::Description() const { return description; }

void LinkElement::SetActivateHandler(ActivateHandler handler) { activateHandler = std::move(handler); }

// A link the program handed a handler answers the activation itself, so a click on the label written
// inside it is a click on the link rather than a click on the run of text. A link the program handed
// nothing is a link that goes nowhere, so it answers nothing and the activation is handed on to
// whatever is around it.
bool LinkElement::AnswersActivation() const { return activateHandler != nullptr || Frame::AnswersActivation(); }

// A link is activated the way anything else is, so a link answers the keyboard the way it answers the
// mouse. What is left to do is hand the names it carries to the program, which is the one thing only
// the program can decide.
void LinkElement::Activate() {
    Frame::Activate();
    if (activateHandler) {
        activateHandler(href, to, target);
    }
}

bool LinkElement::WantsHandCursor() const { return true; }

// A label is drawn here only for a link that was built by hand and holds neither content nor the
// label child the factory builds from one: a link read from a document holds its label as a run of
// text, so the label is laid out and drawn the way every other run of text is. The place the link
// covers is not measured from this drawing, which is why a link read from a document never depends
// on it.
void LinkElement::OnPaint(Renderer& renderer) {
    Frame::OnPaint(renderer);
    if (Display() == FrameDisplay::NONE || label.empty() || HoldsContent(*this)) {
        return;
    }
    const Rect area = Bounds();
    if (area.height < LABEL_FONT_SIZE) {
        return;
    }
    TextStyle style;
    style.fontSize = LABEL_FONT_SIZE;
    style.color = LINK_LABEL;
    style.horizontal = TextAlign::CENTER;
    style.vertical = TextAlign::CENTER;
    style.maxLines = 1;
    renderer.DrawString(label, Rect{area.x + 4.0f, area.y, std::max(0.0f, area.width - 8.0f), area.height}, style);
}

}  // namespace Runtime
