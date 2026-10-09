#pragma once

#include "components/frame/Frame.h"

#include <functional>
#include <string>

namespace Runtime {

// A link is a frame that goes somewhere when it is clicked. It is a frame in every way a document
// cares about, so a link that holds content lays that content out exactly as a box would; a link that
// wrote a label instead holds the label as the run of text it is, which the factory builds as its
// child, and the link then hugs it.
//
// What makes it a link is the place it names and what is done with that name. `href` is a name the
// system opens -- a page, a folder, a file -- and `to` is a screen of the same document. The runtime
// does not decide what going there means: it hands the two names to the handler the program gave it,
// so the document says where a link goes and the program says what going there does. A link with no
// handler is a link that is drawn and does nothing, which is what lets a document be laid out and
// shown before the program behind it exists.
class LinkElement : public Frame {
public:
    using ActivateHandler = std::function<void(const std::string& href, const std::string& to, const std::string& target)>;

    // The size a link's label is written in when the document wrote no size of its own, so a link
    // that wrote a label reads the way a link is expected to read without anything being said.
    static constexpr float LABEL_FONT_SIZE = 15.0f;

    NodeKind Kind() const override;

    void SetHref(std::string value);
    const std::string& Href() const;
    bool HasHref() const;

    void SetTo(std::string value);
    const std::string& To() const;
    bool HasTo() const;

    void SetTarget(std::string value);
    const std::string& Target() const;

    // The label of a link that wrote one instead of content. A link holding content of its own is
    // drawn with that content, and the label is then nothing that is said.
    void SetLabel(std::string value);
    const std::string& Label() const;

    void SetDescription(std::string value);
    const std::string& Description() const;

    // What the program does with the names the link carries. A link with no handler is still drawn.
    void SetActivateHandler(ActivateHandler handler);

    void Activate() override;
    bool AnswersActivation() const override;
    bool WantsHandCursor() const override;

protected:
    void OnPaint(Renderer& renderer) override;

private:
    std::string href;
    std::string to;
    std::string target;
    std::string label;
    std::string description;
    ActivateHandler activateHandler;
};

}  // namespace Runtime
