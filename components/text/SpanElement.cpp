#include "components/text/SpanElement.h"

#include "engine/types/UiTypes.h"
#include "rendering/renderer/Renderer.h"
#include "rendering/text/TextTransform.h"

#include <algorithm>

namespace Runtime {

NodeKind SpanElement::Kind() const {
    return NodeKind::SPAN;
}

void SpanElement::WriteValue(const std::string& property) {
    if (property == "fontFamily") {
        written.family = true;
    } else if (property == "fontSize") {
        written.size = true;
    } else if (property == "fontWeight") {
        written.weight = true;
    } else if (property == "fontStyle") {
        written.italic = true;
    } else if (property == "language") {
        written.locale = true;
    } else if (property == "textColor" || property == "tone") {
        written.color = true;
    } else if (property == "letterSpacing") {
        written.tracking = true;
    } else if (property == "wordSpacing") {
        written.wordSpacing = true;
    } else if (property == "underline") {
        written.underline = true;
    } else if (property == "lineThrough") {
        written.lineThrough = true;
    } else if (property == "decorationColor") {
        written.decorationColor = true;
    } else if (property == "decorationWidth") {
        written.decorationWidth = true;
    } else if (property == "decorationStyle") {
        written.decorationStyle = true;
    } else if (property == "textTransform" || property == "textCase") {
        written.textCase = true;
    } else if (property == "fontFeatures") {
        written.features = true;
    }
}

TextStyle SpanElement::StyleIn(const TextStyle& paragraph) const {
    const TextStyle own = StyleFor();
    TextStyle run = paragraph;
    if (written.family) {
        run.fontFamily = own.fontFamily;
    }
    if (written.size) {
        run.fontSize = own.fontSize;
    }
    if (written.weight) {
        run.fontWeight = own.fontWeight;
    }
    if (written.italic) {
        run.italic = own.italic;
    }
    if (written.locale) {
        run.locale = own.locale;
    }
    if (written.color) {
        run.color = own.color;
        run.decorationColor = own.decorationColor;
    }
    if (written.tracking) {
        // Tracking is written as a fraction of the size a run is drawn at, so the fraction the span
        // wrote is what is taken and it is laid over the size the run ended up with. Laying the
        // distance itself over the run would space a run the block sized differently than it spaced
        // the run the span sized itself.
        const float em = own.fontSize > 0.0f ? own.letterSpacing / own.fontSize : 0.0f;
        run.letterSpacing = em * run.fontSize;
    }
    if (written.wordSpacing) {
        run.wordSpacing = own.wordSpacing;
    }
    if (written.underline) {
        run.underline = own.underline;
    }
    if (written.lineThrough) {
        run.lineThrough = own.lineThrough;
    }
    if (written.decorationColor) {
        run.decorationColor = own.decorationColor;
    }
    if (written.decorationWidth) {
        run.decorationWidth = own.decorationWidth;
    }
    if (written.decorationStyle) {
        run.decorationStyle = own.decorationStyle;
    }
    if (written.textCase) {
        run.textCase = own.textCase;
    }
    if (written.features) {
        run.fontFeatures = own.fontFeatures;
    }
    return run;
}

Size SpanElement::Measure(TextMeasurer& measurer, float availableWidth, float availableHeight, bool) {
    // A run is placed by the block it sits in, so the box a run would cover on its own is only asked
    // for by a reader holding the run and not the block: it is the one line the run is written as,
    // in the case it is written in, because the case of a run is decided before its lines are.
    ResolveRole(ViewportWidth());
    const TextStyle style = StyleFor();
    const Size line = measurer.MeasureText(TextInCase(Text(), style.textCase), style, UNBOUNDED_MEASURE);
    measured = Size{std::max(1.0f, line.width), std::max(1.0f, line.height)};
    (void)availableWidth;
    (void)availableHeight;
    return measured;
}

void SpanElement::OnPaint(Renderer&) {
    // The block draws every run of its paragraph in the one layout its lines were broken in, so a
    // run that drew itself would draw its text a second time at a place no line breaking chose. A
    // span paints nothing: the text it holds is painted by the block, and a span holds no children.
}

}  // namespace Runtime
