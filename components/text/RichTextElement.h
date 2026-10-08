#pragma once

#include "components/text/TextElement.h"
#include "components/text/SpanElement.h"
#include "rendering/text/TextRun.h"

#include <cstddef>
#include <vector>

namespace Runtime {

// A block of text whose content is a paragraph of runs. Everything that sets a block of text
// belongs to the text component underneath, so a rich text block is a text block in all but one
// thing: its text is not one string but a run per stretch of it, and each run carries the values
// that stretch wrote.
//
// What it adds is that the runs are laid out and drawn as one paragraph. The lines of the block are
// broken across every run of it, so a bold word in the middle of a line is part of that line rather
// than a box beside it, and the runs wrap together the way the words of a plain block do. The runs
// are the block's own text and then the spans it holds, in the order they are set, and each span is
// drawn with the values of the block laid under the values that span wrote.
//
// A span is therefore never drawn by itself: the block lays the whole paragraph out once and the
// box each run landed in is the box that run's span is placed at. So the runs of a paragraph are
// nodes of the document tree like any other, each one covering the stretch of text it holds, and a
// document can pick one word out of a paragraph and change it.
class RichTextElement : public TextElement {
public:
    NodeKind Kind() const override;
    Size Measure(TextMeasurer& measurer, float availableWidth, float availableHeight, bool heightDefinite) override;
    void Arrange(const Rect& rect) override;
    Element* HitTest(Point point) override;
    void OnPaint(Renderer& renderer) override;

    // The runs the block is made of, in the order they are set: the text the block itself carries,
    // and then each span it holds with the values that span wrote laid over the values of the block.
    // The text of a run is the case it is written in, because the case of a run is decided before
    // the lines of the paragraph are broken.
    std::vector<TextRun> Runs() const;

    // Where each run landed, in the space of the block's content box, in the order Runs() returns
    // them. Empty until the block has been measured.
    const std::vector<Rect>& RunBoxes() const { return runBoxes; }

private:
    // Places every run again in the box the block finally covers, which is the box the draw lays the
    // paragraph out in. A run has to be where it is drawn, or a click on a word lands beside it.
    void PlaceRuns(TextMeasurer& measurer, const std::vector<TextRun>& runs, const TextStyle& paragraph, bool wraps);

    std::vector<Rect> runBoxes;
};

}  // namespace Runtime
