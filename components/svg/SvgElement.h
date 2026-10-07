#pragma once

#include "components/vector/VectorElement.h"

#include <string>

namespace Runtime {

// An SVG is art drawn in a box, and it is a vector in every way a document cares about: the same
// box, the same view box the art is written in, the same fill, stroke, pattern, rule, ends and
// joins, the same turn, cursor, appearance and hit test, and the same way the art scales with the
// box rather than sitting at the size it was written at.
//
// What it adds is a second way for the art to arrive. Art written into the document is the children
// of the node itself, the same paths and groups an imported icon is made of, so every path of it is
// a node of the document tree and can be picked, moved, recoloured and edited the way any other
// path can. Art that is not written into the document is named by `source`: the file is read where
// the node is built, and its paths become the children of the node, which is what makes a file and
// a pasted path draw the same tree. Written art is the default, because art a document carries is
// art a document can edit.
//
// The class holds only the file it was given. Reading it, and turning it into those paths, happens
// where every other child of a document is created, so a document keeps the one place that turns
// written values into a tree.
class SvgElement : public VectorElement {
public:
    NodeKind Kind() const override;
    // The file the art is read from, or the markup itself when the document carries it. It is kept
    // so the node still says where its art came from once the art has been placed. A node with
    // children of its own draws those children and never reads the name.
    //
    // The file is read where the node is built and not again, so a binding that writes a new
    // source is not one of the values the node answers for: the art would stay the art it was
    // built from, and a node that changed its name while keeping the old art would be lying about
    // where the art came from. A document that changes the file is built again instead, which is
    // what an editor does with every value it writes.
    void SetSource(std::string value);
    const std::string& Source() const;

private:
    std::string source;
};

}  // namespace Runtime
