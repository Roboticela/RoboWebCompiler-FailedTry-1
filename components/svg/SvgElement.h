#pragma once

#include "components/vector/VectorElement.h"

#include <string>

namespace Runtime {

// An SVG is the art of an SVG document drawn in a box. Everything that draws it belongs to the
// vector underneath: the box, the view box the art is written in, the fill the artwork takes its
// current colour from, the turn, the cursor, the appearance, the hit test, and the way the art
// scales with the box rather than sitting at the size it was written at.
//
// What it brings is the file. The art itself is not written in the document, so the paths of the
// file are read into the children of the node when it is built: they are the same paths, and the
// same groups around them, that an imported icon is made of, which is why an SVG file and a
// pasted path end up drawing the same tree.
//
// The class holds only the file it was given. Reading it, and turning it into those paths, happens
// where every other child of a document is created, so a document keeps the one place that turns
// written values into a tree.
class SvgElement : public VectorElement {
public:
    NodeKind Kind() const override;
    // The file the art is read from, or the markup itself when the document carries it. It is kept
    // so the node still says where its art came from once the art has been placed.
    void SetSource(std::string value);
    const std::string& Source() const;
    void ApplyBound(const std::string& property, const std::string& value) override;

private:
    std::string source;
};

}  // namespace Runtime
