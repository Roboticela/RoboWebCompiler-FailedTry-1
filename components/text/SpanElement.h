#pragma once

#include "components/text/TextElement.h"

#include <string>

namespace Runtime {

// The values that set a run of text, one flag each. A run takes every value it did not write from
// the block it sits in, so the font, size, colour and tracking of the block reach a run that wrote
// none of its own, and a run laid over its block changes exactly the values it named. One flag per
// value rather than a set of property names, because the merge below is the list of values a run can
// write, and a list written once is a list a new value cannot be quietly left out of.
struct WrittenRunValues {
    bool family = false;
    bool size = false;
    bool weight = false;
    bool italic = false;
    bool locale = false;
    bool color = false;
    bool tracking = false;
    bool wordSpacing = false;
    bool underline = false;
    bool lineThrough = false;
    bool decorationColor = false;
    bool decorationWidth = false;
    bool decorationStyle = false;
    bool textCase = false;
    bool features = false;
};

// A span is a run of text inside a block of text: a word, a phrase, or a sentence drawn with values
// of its own in the middle of lines that are set by the block around it. Everything that sets a run
// of text belongs to the text component underneath, so a span takes the whole vocabulary a text
// block has for how its text looks and none of the vocabulary for the box it sits in: how wide the
// block is, where it sits, how its lines are spaced, and how many lines it holds are questions about
// the block, and a run answers none of them.
//
// What it adds is which of those values it wrote. The block it sits in measures and draws every run
// of the paragraph in one layout, so a span never draws itself and never places itself: it hands
// over the text it holds and the values it wrote, and the block lays the lines out. What it keeps is
// the block's style with those values laid over it, which is why a run follows a change to the block
// it is inside without being told again.
class SpanElement : public TextElement {
public:
    NodeKind Kind() const override;
    Size Measure(TextMeasurer& measurer, float availableWidth, float availableHeight, bool heightDefinite) override;
    void OnPaint(Renderer& renderer) override;

    // The values the span wrote, named the way a document names them, so the block can lay over its
    // own exactly the values this run named. A name that is not one of the values a run can write is
    // simply not one of them: it is refused by the compiler before it ever reaches here.
    void WriteValue(const std::string& property);
    const WrittenRunValues& Written() const { return written; }

    // The style this run is drawn with: the style of the block it sits in with the values the span
    // wrote laid over it.
    TextStyle StyleIn(const TextStyle& paragraph) const;

private:
    WrittenRunValues written;
};

}  // namespace Runtime
