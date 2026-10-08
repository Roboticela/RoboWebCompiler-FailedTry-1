#include "components/text/RichTextElement.h"

#include "rendering/renderer/Renderer.h"
#include "rendering/text/TextTransform.h"

#include <algorithm>
#include <memory>

namespace Runtime {

NodeKind RichTextElement::Kind() const {
    return NodeKind::RICHTEXT;
}

std::vector<TextRun> RichTextElement::Runs() const {
    const TextStyle paragraph = StyleFor();
    std::vector<TextRun> runs;
    // The text the block carries is the run it begins with, so a paragraph can be a line of plain
    // text with the words that are set differently picked out of it.
    if (!Text().empty()) {
        TextRun lead;
        lead.text = DisplayText();
        lead.style = paragraph;
        runs.push_back(std::move(lead));
    }
    for (const std::unique_ptr<Element>& child : Children()) {
        const SpanElement* span = dynamic_cast<const SpanElement*>(child.get());
        if (span == nullptr) {
            continue;
        }
        TextRun run;
        run.style = span->StyleIn(paragraph);
        run.text = TextInCase(span->Text(), run.style.textCase);
        runs.push_back(std::move(run));
    }
    return runs;
}

Size RichTextElement::Measure(TextMeasurer& measurer, float availableWidth, float availableHeight, bool) {
    // A block of rich text is measured by its own reading of the values a block of text reads, so the
    // one value it has to read the same way as the frame underneath is the one that takes the block
    // out of the layout: it covers nothing, and none of its runs has a line to be placed on.
    if (Display() == FrameDisplay::NONE) {
        measured = {};
        runBoxes.clear();
        return measured;
    }
    ResolveRole(ViewportWidth());
    const TextStyle paragraph = StyleFor();
    const std::vector<TextRun> runs = Runs();
    // The same room a block of plain text is measured in: the room it was offered, unless the
    // document asked for a size of its own, in which case that is the size the lines are held in.
    const float widthSpace = Width().unit == LengthUnit::SCREEN && ViewportWidth() > 0.0f ? ViewportWidth() : availableWidth;
    const float heightSpace = Height().unit == LengthUnit::SCREEN && ViewportHeight() > 0.0f ? ViewportHeight() : availableHeight;
    const bool widthGiven = Width().unit == LengthUnit::PIXELS || Width().unit == LengthUnit::PERCENT || Width().unit == LengthUnit::FILL ||
                            Width().unit == LengthUnit::SCREEN;
    const bool heightGiven = Height().unit == LengthUnit::PIXELS || Height().unit == LengthUnit::PERCENT || Height().unit == LengthUnit::FILL ||
                             Height().unit == LengthUnit::SCREEN;
    const bool wraps = paragraph.wrap;
    const float leading = paragraph.lineHeight > 0.0f ? paragraph.lineHeight : 1.0f;
    const float measureWidth = widthGiven ? Width().Resolve(widthSpace, availableWidth)
                                         : (availableWidth > 1.0f ? availableWidth : UNBOUNDED_MEASURE);
    const float wrapWidth = std::max(1.0f, measureWidth);
    // The paragraph is laid out with the style it is drawn with, so the lines the runs wrap into, the
    // line height they are set on, and the cap on the lines all reach the height of the box. A box
    // only as tall as the lines is the reading of the height, so the runs come back at the top of it.
    const TextParagraph laid = runs.empty() ? TextParagraph{} : measurer.MeasureParagraph(runs, paragraph, wraps ? wrapWidth : UNBOUNDED_MEASURE, 0.0f);
    const float block = std::max(paragraph.fontSize * leading, laid.size.height);
    const Edges pad = Padding();
    float resolvedWidth = std::max(8.0f, laid.size.width) + pad.SpanX(wrapWidth);
    float resolvedHeight = block + pad.SpanY(wrapWidth);
    if (widthGiven) {
        resolvedWidth = Width().Resolve(widthSpace, resolvedWidth);
    }
    if (heightGiven) {
        resolvedHeight = Height().Resolve(heightSpace, resolvedHeight);
    }
    const float cappedHeight = paragraph.maxLines > 0 ? paragraph.fontSize * leading * static_cast<float>(paragraph.maxLines) + pad.SpanY(wrapWidth) : 0.0f;
    if (paragraph.maxLines > 0) {
        resolvedHeight = std::min(resolvedHeight, cappedHeight);
    }
    switch (Sizing()) {
    case TextSizing::AUTO_WIDTH:
        resolvedWidth = std::max(8.0f, laid.size.width) + pad.SpanX(wrapWidth);
        break;
    case TextSizing::AUTO_HEIGHT:
        resolvedHeight = paragraph.maxLines > 0 ? std::min(block + pad.SpanY(wrapWidth), cappedHeight) : block + pad.SpanY(wrapWidth);
        break;
    case TextSizing::FIXED:
        resolvedWidth = widthGiven ? Width().Resolve(widthSpace, resolvedWidth) : std::max(1.0f, availableWidth);
        resolvedHeight = heightGiven ? Height().Resolve(heightSpace, resolvedHeight) : block + pad.SpanY(wrapWidth);
        break;
    case TextSizing::UNSET:
        break;
    }
    measured = Size{std::max(1.0f, resolvedWidth), std::max(1.0f, resolvedHeight)};
    // The box is settled before the runs are placed, because the box is the room they are placed in:
    // a clamped size or a margin moves the room the lines are drawn in, and a run has to land where
    // the draw will put it.
    const Size box = FinishMeasure(widthSpace, heightSpace);
    PlaceRuns(measurer, runs, paragraph, wraps);
    return box;
}

void RichTextElement::PlaceRuns(TextMeasurer& measurer, const std::vector<TextRun>& runs, const TextStyle& paragraph, bool wraps) {
    runBoxes.clear();
    if (runs.empty() || !Visible()) {
        return;
    }
    const Edges pad = Padding();
    // The box the paragraph is placed in is the box it is drawn in: the block it covers, less the
    // padding it holds its content off the edge with.
    const float insideWidth = std::max(1.0f, measured.width - pad.SpanX(measured.width));
    const float insideHeight = std::max(0.0f, measured.height - pad.SpanY(measured.height));
    const TextParagraph placed = measurer.MeasureParagraph(runs, paragraph, wraps ? insideWidth : UNBOUNDED_MEASURE, insideHeight);
    runBoxes = placed.runs;
}

void RichTextElement::Arrange(const Rect& rect) {
    // The block places its own runs, so the layout of the frame underneath is not asked to place
    // them: a run belongs to the line it was broken onto, not to a row of boxes.
    Element::Arrange(rect);
    if (Display() == FrameDisplay::NONE) {
        runBoxes.clear();
        return;
    }
    const Edges pad = Padding();
    Rect area = Bounds();
    const float left = pad.left.Resolve(area.width, 0.0f);
    const float right = pad.right.Resolve(area.width, 0.0f);
    const float top = pad.top.Resolve(area.width, 0.0f);
    const float bottom = pad.bottom.Resolve(area.width, 0.0f);
    area.x += left;
    area.y += top;
    area.width = std::max(0.0f, area.width - left - right);
    area.height = std::max(0.0f, area.height - top - bottom);
    std::size_t run = Text().empty() ? 0 : 1;
    for (const std::unique_ptr<Element>& child : Children()) {
        if (dynamic_cast<SpanElement*>(child.get()) == nullptr) {
            // A child that is not a run of text is no part of the paragraph, so none of the lines
            // holds a box for it. It is placed where it covers nothing rather than being handed a
            // box the line breaking never chose.
            child->Arrange(Rect{area.x, area.y, 0.0f, 0.0f});
            continue;
        }
        if (run < runBoxes.size()) {
            const Rect& box = runBoxes[run];
            child->Arrange(Rect{area.x + box.x, area.y + box.y, box.width, box.height});
        } else {
            child->Arrange(Rect{area.x, area.y, 0.0f, 0.0f});
        }
        ++run;
    }
}

Element* RichTextElement::HitTest(Point point) {
    if (!Visible() || Display() == FrameDisplay::NONE) {
        return nullptr;
    }
    // The runs of the paragraph are what a click is aimed at, so a click on a word picks that word
    // out of the block. The block still owns itself, so a click inside it beside its text picks the
    // block rather than falling through to whatever is behind it.
    if (Element* child = HitTestChildren(point)) {
        return child;
    }
    return Frame::HitTest(point);
}

void RichTextElement::OnPaint(Renderer& renderer) {
    Frame::OnPaint(renderer);
    const std::vector<TextRun> runs = Runs();
    if (Display() == FrameDisplay::NONE || runs.empty()) {
        return;
    }
    const Edges pad = Padding();
    Rect area = Bounds();
    const float left = pad.left.Resolve(area.width, 0.0f);
    const float right = pad.right.Resolve(area.width, 0.0f);
    const float top = pad.top.Resolve(area.width, 0.0f);
    const float bottom = pad.bottom.Resolve(area.width, 0.0f);
    area.x += left;
    area.y += top;
    area.width = std::max(0.0f, area.width - left - right);
    area.height = std::max(0.0f, area.height - top - bottom);
    // The whole paragraph is one draw, in the box the runs were placed in, so the lines are broken
    // exactly once and every run is drawn in the line it was placed on.
    renderer.DrawParagraph(runs, area, StyleFor());
}

}  // namespace Runtime
