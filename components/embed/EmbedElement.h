#pragma once

#include "components/frame/Frame.h"

#include <string>

namespace Runtime {

// An embed is a document inside a document. It is a frame in every way a document cares about, so the
// document it holds is laid out and drawn exactly as it would be anywhere else: an embed is not a
// second kind of layout, it is a second file. What makes it an embed rather than a box is the name of
// the document it was read from.
//
// The name is kept rather than forgotten, because a document that was built out of a folder of files
// has to be able to say which file each part of it came from. An embed that names a document is drawn
// with a small mark in its corner, the way a definition is, so the parts of a screen that come from
// other files are told apart from the parts that were written here.
class EmbedElement : public Frame {
public:
    NodeKind Kind() const override;

    void SetSource(std::string value);
    const std::string& Source() const;

    void SetDescription(std::string value);
    const std::string& Description() const;

    // Whether the corner mark is drawn. A document that shows an embedded part as finished content
    // can hide it, the way it can hide the mark of a definition.
    void SetBadgeVisible(bool value);
    bool BadgeVisible() const;

protected:
    void OnPaint(Renderer& renderer) override;

private:
    std::string source;
    std::string description;
    bool badgeVisible = true;
};

}  // namespace Runtime
