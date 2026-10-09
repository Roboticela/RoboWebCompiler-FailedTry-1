#pragma once

#include "components/frame/Frame.h"

#include <functional>
#include <string>

namespace Runtime {

// A link is a frame that goes somewhere when it is clicked. It is a frame in every way a document
// cares about, so a link that holds content lays that content out exactly as a box would; a link that
// holds nothing draws its own label, which is what makes a link a thing a document can write in one
// element.
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

    NodeKind Kind() const override;

    void SetHref(std::string value);
    const std::string& Href() const;
    bool HasHref() const;

    void SetTo(std::string value);
    const std::string& To() const;
    bool HasTo() const;

    void SetTarget(std::string value);
    const std::string& Target() const;

    // The label drawn while the link holds no content of its own.
    void SetLabel(std::string value);
    const std::string& Label() const;

    void SetDescription(std::string value);
    const std::string& Description() const;

    // What the program does with the names the link carries. A link with no handler is still drawn.
    void SetActivateHandler(ActivateHandler handler);

    void Activate() override;
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
