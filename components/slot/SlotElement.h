#pragma once

#include "components/frame/Frame.h"

#include <string>

namespace Runtime {

// A slot is the place inside a component where a copy of it puts content of its own. The name the
// slot answers to is the name of the node, so a copy writes `slot="name"` on a child to send it
// there. A slot that no copy filled draws its own fallback, which is a label rather than a child:
// the fallback is a hint about what belongs in the slot, not content that has to be laid out.
//
// The slot is a frame, so a filled one lays out and clips the content it was handed exactly as the
// box around it asks. An empty one draws nothing but its outline and its fallback, which is what
// tells an unfilled slot apart from a box that was given no content by mistake.
class SlotElement : public Frame {
public:
    NodeKind Kind() const override;

    // The text drawn while the slot holds nothing. A slot with no fallback draws only its outline.
    void SetFallbackText(std::string value);
    const std::string& FallbackText() const;

    // Whether the slot holds content of its own. A slot filled by a copy holds children, so a filled
    // slot needs no fallback.
    bool IsFilled() const;

    // Whether the outline of an unfilled slot is drawn. A slot that is left empty on purpose can
    // hide it.
    void SetOutlineVisible(bool value);
    bool OutlineVisible() const;

protected:
    void OnPaint(Renderer& renderer) override;

private:
    std::string fallbackText;
    bool outlineVisible = true;
};

}  // namespace Runtime
