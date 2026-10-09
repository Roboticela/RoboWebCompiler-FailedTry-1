#include "compiler/ir/UiIr.h"
#include "compiler/semantic/ComponentCatalog.h"
#include "compiler/semantic/UiCompiler.h"
#include "compiler/types/ValueParser.h"
#include "components/arrow/ArrowElement.h"
#include "components/boolean/BooleanElement.h"
#include "components/component/ComponentElement.h"
#include "components/embed/EmbedElement.h"
#include "components/ellipse/EllipseElement.h"
#include "components/frame/Frame.h"
#include "components/group/GroupElement.h"
#include "components/image/ImageElement.h"
#include "components/instance/InstanceElement.h"
#include "components/line/LineElement.h"
#include "components/link/LinkElement.h"
#include "components/layout/BoardElement.h"
#include "components/polygon/PolygonElement.h"
#include "components/layout/WindowElement.h"
#include "components/primitive/PrimitiveElement.h"
#include "components/rectangle/RectangleElement.h"
#include "components/slot/SlotElement.h"
#include "components/star/StarElement.h"
#include "components/svg/SvgElement.h"
#include "components/text/RichTextElement.h"
#include "components/text/SpanElement.h"
#include "components/text/TextElement.h"
#include "components/vector/VectorElement.h"
#include "components/video/VideoElement.h"
#include "app/LinkRoute.h"
#include "designer/model/DesignDocument.h"
#include "designer/tools/EditorTool.h"
#include "engine/motion/Motion.h"
#include "platform/windows/WicImageProvider.h"
#include "rendering/image/ImageAdjust.h"
#include "rendering/image/ImageDecoder.h"
#include "rendering/image/VideoStream.h"
#include "rendering/renderer/Renderer.h"
#include "rendering/path/PathBoolean.h"
#include "rendering/path/PathCurve.h"
#include "rendering/text/TextLayout.h"
#include "rendering/text/TextMeasurer.h"
#include "tests/TestHarness.h"
#include "ui/element/Element.h"
#include "ui/factory/UiFactory.h"
#include "ui/input/PointerInput.h"
#include "xml/parser/XmlParser.h"
#include "xml/serializer/XmlSerializer.h"

#include <wincodec.h>
#include <wrl/client.h>

#ifdef ABSOLUTE
#undef ABSOLUTE
#endif
#ifdef RELATIVE
#undef RELATIVE
#endif

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace Runtime {
namespace {

// The one reading of a paragraph the tests measure against: every character is half a size unit
// wide, the runs of the paragraph follow one another and wrap when they reach the width they are
// held in, and a line is as tall as the line height of the paragraph over the tallest run on it.
// The line breaking of a real platform is finer than this (it breaks between words, not between
// runs), so this is a measurer to test the element against rather than the platform it draws with.
TextParagraph LayOutRuns(const std::vector<TextRun>& runs, const TextStyle& paragraph, float width) {
    TextParagraph measured;
    measured.runs.assign(runs.size(), Rect{0.0f, 0.0f, 0.0f, 0.0f});
    const float multiple = paragraph.lineHeight > 0.0f ? paragraph.lineHeight : 1.0f;
    float line = paragraph.fontSize * multiple;
    for (const TextRun& run : runs) {
        line = std::max(line, run.style.fontSize * multiple);
    }
    float at = 0.0f;
    float top = 0.0f;
    for (std::size_t index = 0; index < runs.size(); ++index) {
        const float runWidth = static_cast<float>(runs[index].text.size()) * runs[index].style.fontSize * 0.5f;
        if (at > 0.0f && width > 0.0f && width < UNBOUNDED_MEASURE && at + runWidth > width) {
            at = 0.0f;
            top += line;
        }
        measured.runs[index] = Rect{at, top, runWidth, line};
        at += runWidth;
        measured.size.width = std::max(measured.size.width, at);
    }
    measured.size.height = top + line;
    return measured;
}

class FakeMeasurer : public TextMeasurer {
public:
    Size MeasureText(std::string_view, const TextStyle& style, float) override {
        return Size{style.fontSize, style.fontSize};
    }

    TextParagraph MeasureParagraph(const std::vector<TextRun>& runs, const TextStyle& paragraph, float width, float) override {
        return LayOutRuns(runs, paragraph, width);
    }
};

// Measures a block the way the platform does: the layout is built from the style the block is
// drawn with, so the lines it wraps into and the line height they are set on are both in the box
// the block is told it covers.
class ShapedMeasurer : public TextMeasurer {
public:
    Size MeasureText(std::string_view text, const TextStyle& style, float maxWidth) override {
        TextStyle shaped = style;
        shaped.wrap = style.wrap && maxWidth < UNBOUNDED_MEASURE;
        const ShapedText measured = ShapeText(text, shaped, shaped.wrap ? maxWidth : UNBOUNDED_MEASURE);
        return Size{measured.width, measured.height};
    }

    TextParagraph MeasureParagraph(const std::vector<TextRun>& runs, const TextStyle& paragraph, float width, float) override {
        return LayOutRuns(runs, paragraph, width);
    }
};

// Deliberately poor at measuring and good at remembering: the style a block hands over can be
// compared with the style it draws with, which is where the height of a wrapped block went wrong.
class StyleWatchingMeasurer : public TextMeasurer {
public:
    Size MeasureText(std::string_view text, const TextStyle& watched, float maxWidth) override {
        style = watched;
        measuredText.assign(text);
        measuredWidth = maxWidth;
        return Size{watched.fontSize, watched.fontSize};
    }

    TextParagraph MeasureParagraph(const std::vector<TextRun>& runs, const TextStyle& paragraph, float width, float) override {
        style = paragraph;
        measuredText.clear();
        for (const TextRun& run : runs) {
            measuredText += run.text;
        }
        runCount = runs.size();
        measuredWidth = width;
        return LayOutRuns(runs, paragraph, width);
    }

    TextStyle style;
    std::string measuredText;
    float measuredWidth = 0.0f;
    std::size_t runCount = 0;
};

struct PaintRecord {
    Rect rect;
    float opacity = 1.0f;
    int clipDepth = 0;
    int rotationDepth = 0;
    float rotation = 0.0f;
    // True when the draw went through a sampled path rather than a platform rectangle,
    // which is how a shape with corners or a curve is told apart from a plain box. The
    // path itself is kept so a test can measure the curve that was actually drawn.
    bool vector = false;
    std::string path;
    // What a vector draw was asked for, so a shape that carries a line style of its own can be
    // told apart from one that takes the default. The dash pattern and the offset the pattern
    // starts at are kept so a dashed stroke can be told apart from a solid one.
    bool evenOdd = false;
    char lineCap = 'b';
    char lineJoin = 'r';
    std::vector<float> dashes;
    float dashOffset = 0.0f;
    // What a string draw was asked for, so the values the text subsystem decides can be read off a
    // paint pass: the string as the case it is written in left it, the line height it is set on,
    // whether it was wrapped, and the lines drawn through it.
    std::string text;
    float lineHeight = 0.0f;
    bool wrap = true;
    bool underline = false;
    bool lineThrough = false;
    bool overline = false;
    // What a paragraph draw was asked for, one entry per run, so the values each run reached the
    // draw with can be read off a paint pass: the text of the run as the case it is written in left
    // it, the size and weight it is set at, the colour it is drawn in, and its lines.
    struct PaintedRun {
        std::string text;
        std::string fontFamily;
        float fontSize = 0.0f;
        int fontWeight = 400;
        Color color;
        bool italic = false;
        bool underline = false;
        bool lineThrough = false;
    };
    std::vector<PaintedRun> runs;
    // What an image draw was asked for: the name the pixels are cached under, and whether pixels
    // were handed over at all, so a picture that was drawn can be told apart from the box a picture
    // that could not be read is drawn as. The box the picture landed in is the rect of the record.
    std::string imageName;
    int imageWidth = 0;
    int imageHeight = 0;
    bool imagePixels = false;
    // The outline of the cut a draw was held inside, when that cut was a shape rather than a box.
    // Empty means the draw was not cut at all, or was cut by a plain box, which is the cut a picture
    // with square corners is given.
    std::string clipShape;
};

// Records what a paint pass handed to each draw, so a group can be checked for passing its
// own opacity, clip, rotation, and translation down to every child it holds.
class RecordingRenderer : public Renderer {
public:
    Size MeasureText(std::string_view, const TextStyle& style, float) override { return Size{style.fontSize, style.fontSize}; }
    TextParagraph MeasureParagraph(const std::vector<TextRun>& runs, const TextStyle& paragraph, float width, float) override {
        return LayOutRuns(runs, paragraph, width);
    }
    void SetDpi(float) override {}
    void SetTransform(float, float, float) override {}
    void BeginFrame(Color) override {}
    bool EndFrame() override { return true; }
    void FillRectangle(const Rect& rect, const Color&, float) override { Record(rect); }
    void StrokeRectangle(const Rect& rect, const Color&, float, float) override { Record(rect); }
    void DrawString(std::string_view text, const Rect& rect, const TextStyle& style) override {
        Record(rect);
        records.back().text.assign(text);
        records.back().lineHeight = style.lineHeight;
        records.back().wrap = style.wrap;
        records.back().underline = style.underline;
        records.back().lineThrough = style.lineThrough;
        records.back().overline = style.overline;
    }
    void DrawParagraph(const std::vector<TextRun>& paragraph, const Rect& rect, const TextStyle& style) override {
        Record(rect);
        records.back().lineHeight = style.lineHeight;
        records.back().wrap = style.wrap;
        records.back().overline = style.overline;
        for (const TextRun& run : paragraph) {
            PaintRecord::PaintedRun painted;
            painted.text = run.text;
            painted.fontFamily = run.style.fontFamily;
            painted.fontSize = run.style.fontSize;
            painted.fontWeight = run.style.fontWeight;
            painted.color = run.style.color;
            painted.italic = run.style.italic;
            painted.underline = run.style.underline;
            painted.lineThrough = run.style.lineThrough;
            records.back().runs.push_back(std::move(painted));
        }
    }
    void DrawCachedImage(std::string_view name, const Rect& rect, const unsigned char* pixels, int width, int height) override {
        Record(rect);
        records.back().imageName.assign(name);
        records.back().imageWidth = width;
        records.back().imageHeight = height;
        records.back().imagePixels = pixels != nullptr;
    }
    void DrawVector(const VectorPaint& paint) override {
        Record(paint.bounds);
        records.back().vector = true;
        records.back().path.assign(paint.data);
        records.back().evenOdd = paint.evenOdd;
        records.back().lineCap = paint.lineCap;
        records.back().lineJoin = paint.lineJoin;
        records.back().dashes = paint.dashes;
        records.back().dashOffset = paint.dashOffset;
    }
    void PushClip(const Rect& rect) override {
        clips.push_back(rect);
        clipShapes.emplace_back();
    }
    void PushClipPath(const Rect& bounds, std::string_view path) override {
        clips.push_back(bounds);
        clipShapes.emplace_back(path);
    }
    void PopClip() override {
        if (!clips.empty()) {
            clips.pop_back();
        }
        if (!clipShapes.empty()) {
            clipShapes.pop_back();
        }
    }
    void PushOpacity(float value) override {
        const float parent = opacityStack.empty() ? 1.0f : opacityStack.back();
        opacityStack.push_back(parent * std::clamp(value, 0.0f, 1.0f));
    }
    void PopOpacity() override {
        if (!opacityStack.empty()) {
            opacityStack.pop_back();
        }
    }
    void PushRotation(float degrees, float, float) override { rotations.push_back(degrees); }
    void PopTransform() override {
        if (!rotations.empty()) {
            rotations.pop_back();
        }
    }

    std::vector<PaintRecord> records;

private:
    void Record(const Rect& rect) {
        PaintRecord record;
        record.rect = rect;
        record.opacity = opacityStack.empty() ? 1.0f : opacityStack.back();
        record.clipDepth = static_cast<int>(clips.size());
        record.clipShape = clipShapes.empty() ? std::string() : clipShapes.back();
        record.rotationDepth = static_cast<int>(rotations.size());
        record.rotation = rotations.empty() ? 0.0f : rotations.back();
        records.push_back(record);
    }

    std::vector<float> opacityStack;
    std::vector<Rect> clips;
    std::vector<std::string> clipShapes;
    std::vector<float> rotations;
};

const PaintRecord* PaintRecordFor(const RecordingRenderer& renderer, const Rect& rect) {
    for (const PaintRecord& record : renderer.records) {
        const bool same = std::fabs(record.rect.x - rect.x) < 0.01f && std::fabs(record.rect.y - rect.y) < 0.01f &&
                          std::fabs(record.rect.width - rect.width) < 0.01f && std::fabs(record.rect.height - rect.height) < 0.01f;
        if (same) {
            return &record;
        }
    }
    return nullptr;
}

SourceFile Source(std::string text) {
    return SourceFile{"memory.xml", std::move(text)};
}

void Finish(TestContext& test, const char* name, int before) {
    if (test.failed == before) {
        std::cout << "✓ " << name << '\n';
    }
}

std::unique_ptr<Element> Layout(const char* xml, float width, float height) {
    const CompileResult compiled = CompileUi(Source(xml));
    if (!compiled.Ok()) {
        return nullptr;
    }
    std::unique_ptr<Element> root = Instantiate(*compiled.root, {});
    if (root == nullptr) {
        return nullptr;
    }
    FakeMeasurer measurer;
    root->SetViewport(width, height);
    root->Measure(measurer, width, height, true);
    root->Arrange(Rect{0.0f, 0.0f, width, height});
    return root;
}

void TestParserAndSerializer(TestContext& test) {
    const int before = test.failed;
    const ParseResult parsed = ParseXml(Source("<window width=\"800\"><frame name=\"Hello\"/></window>"));
    CHECK(test, parsed.root.has_value());
    CHECK(test, parsed.diagnostics.empty());
    CHECK(test, parsed.root->children[0].name == "frame");
    const std::string serialized = SerializeXml(*parsed.root);
    CHECK(test, serialized.find("frame") != std::string::npos);
    CHECK(test, serialized.find("Hello") != std::string::npos);
    CHECK(test, ParseXml(Source(serialized)).root.has_value());
    Finish(test, "parser", before);
}

void TestTypeMismatch(TestContext& test) {
    const int before = test.failed;
    const CompileResult compiled = CompileUi(Source("<window><frame width=\"hello\"/></window>"));
    CHECK(test, !compiled.Ok());
    CHECK(test, compiled.diagnostics.front().code == DIAGNOSTIC_TYPE_MISMATCH);
    CHECK(test, compiled.diagnostics.front().message.find("Length") != std::string::npos);
    Finish(test, "types", before);
}

void TestSuggestions(TestContext& test) {
    const int before = test.failed;
    const CompileResult component = CompileUi(Source("<window><fram/></window>"));
    CHECK(test, !component.Ok());
    CHECK(test, component.diagnostics.front().code == DIAGNOSTIC_UNKNOWN_COMPONENT);
    CHECK(test, component.diagnostics.front().message.find("frame") != std::string::npos);
    const CompileResult property = CompileUi(Source("<window><frame backgroundColour=\"red\"/></window>"));
    CHECK(test, !property.Ok());
    CHECK(test, property.diagnostics.front().code == DIAGNOSTIC_UNKNOWN_PROPERTY);
    CHECK(test, property.diagnostics.front().message.find("backgroundColor") != std::string::npos);
    Finish(test, "suggestions", before);
}

void TestFrameProperties(TestContext& test) {
    const int before = test.failed;
    const CompileResult compiled = CompileUi(Source(
        "<window><frame id=\"box\" name=\"Card\" class=\"panel\" component=\"Box\" instance=\"one\" tag=\"section\" role=\"group\" "
        "widthMode=\"fixed\" heightMode=\"fixed\" width=\"120\" height=\"80\" aspectRatio=\"2\" position=\"absolute\" anchor=\"center\" "
        "layout=\"vertical\" align=\"center\" justify=\"spaceBetween\" wrap=\"wrap\" columns=\"3\" rows=\"2\" column=\"1\" row=\"1\" "
        "columnSpan=\"2\" rowSpan=\"2\" grow=\"1\" shrink=\"0\" basis=\"40\" gap=\"8\" background=\"#0E7AFF\" backgroundColor=\"#FFFFFF\" "
        "gradient=\"linear\" image=\"photo\" opacity=\"0.5\" borderWidth=\"1\" borderColor=\"#D2D2D7\" borderStyle=\"solid\" "
        "radius=\"8\" radiusTopLeft=\"4\" overflow=\"scroll\" clip=\"true\" display=\"normal\" blendMode=\"multiply\" cursor=\"pointer\" "
        "blur=\"2\" translateX=\"3\" rotation=\"15\" scale=\"1.1\" shadowColor=\"#000000\" shadowX=\"1\" shadowY=\"2\" shadowBlur=\"4\" "
        "shadowSpread=\"0\">"
        "<shadow shadowColor=\"#111111\" shadowX=\"4\" shadowY=\"6\" shadowBlur=\"8\" shadowSpread=\"1\"/>"
        "</frame></window>"));
    CHECK(test, compiled.Ok());
    std::unique_ptr<Element> root = Instantiate(*compiled.root, {});
    Frame* frame = dynamic_cast<Frame*>(root->FindById("box"));
    CHECK(test, frame != nullptr);
    CHECK(test, frame->Name() == "Card");
    CHECK(test, frame->ClassName() == "panel");
    CHECK(test, frame->ComponentName() == "Box");
    CHECK(test, frame->InstanceName() == "one");
    CHECK(test, frame->Tag() == "section");
    CHECK(test, frame->Role() == "group");
    CHECK(test, frame->Layout() == FrameLayout::VERTICAL);
    CHECK(test, frame->PositionMode() == FramePosition::ABSOLUTE);
    CHECK(test, frame->Anchor() == FrameAnchor::CENTER);
    CHECK(test, frame->Overflow() == OverflowMode::SCROLL);
    CHECK(test, frame->Cursor() == "pointer");
    CHECK(test, frame->BlendMode() == "multiply");
    CHECK(test, frame->Gradient() == "linear");
    CHECK(test, frame->BackgroundImage() == "photo");
    CHECK(test, frame->WantsHandCursor());
    CHECK(test, frame->Clips());
    CHECK(test, std::fabs(frame->Blur() - 2.0f) < 0.01f);
    CHECK(test, std::fabs(frame->CornerRadius(0) - 4.0f) < 0.01f);
    CHECK(test, frame->Shadows().size() == 2);
    CHECK(test, std::fabs(frame->Shadows()[1].x - 4.0f) < 0.01f);
    Finish(test, "frame properties", before);
}

void TestFrameLayout(TestContext& test) {
    const int before = test.failed;
    std::unique_ptr<Element> centered = Layout(
        "<window width=\"800\" height=\"600\"><frame layout=\"vertical\" width=\"fill\" height=\"fill\" align=\"center\">"
        "<frame id=\"box\" width=\"100\" height=\"40\"/></frame></window>",
        800.0f, 600.0f);
    CHECK(test, centered != nullptr);
    Element* box = centered->FindById("box");
    CHECK(test, box != nullptr);
    CHECK(test, std::fabs(box->Bounds().x - 350.0f) < 0.01f);
    CHECK(test, std::fabs(box->Bounds().width - 100.0f) < 0.01f);
    CHECK(test, std::fabs(box->Bounds().height - 40.0f) < 0.01f);

    std::unique_ptr<Element> grown = Layout(
        "<window width=\"300\" height=\"40\"><frame layout=\"horizontal\" width=\"300\" height=\"40\">"
        "<frame id=\"grow\" width=\"40\" height=\"20\" grow=\"1\"/><frame id=\"rest\" width=\"40\" height=\"20\"/></frame></window>",
        300.0f, 40.0f);
    CHECK(test, grown != nullptr);
    CHECK(test, std::fabs(grown->FindById("grow")->Bounds().width - 260.0f) < 0.01f);
    CHECK(test, std::fabs(grown->FindById("rest")->Bounds().x - 260.0f) < 0.01f);

    std::unique_ptr<Element> grid = Layout(
        "<window width=\"300\" height=\"80\"><frame layout=\"grid\" columns=\"3\" width=\"300\" height=\"80\">"
        "<frame id=\"a\" width=\"40\" height=\"40\"/><frame id=\"b\" width=\"40\" height=\"40\"/>"
        "<frame id=\"c\" width=\"40\" height=\"40\" columnSpan=\"1\"/></frame></window>",
        300.0f, 80.0f);
    CHECK(test, grid != nullptr);
    CHECK(test, std::fabs(grid->FindById("a")->Bounds().x - 0.0f) < 0.01f);
    CHECK(test, std::fabs(grid->FindById("b")->Bounds().x - 100.0f) < 0.01f);
    CHECK(test, std::fabs(grid->FindById("c")->Bounds().x - 200.0f) < 0.01f);

    std::unique_ptr<Element> percent = Layout(
        "<window width=\"400\" height=\"100\"><frame width=\"400\" height=\"100\" layout=\"vertical\">"
        "<frame id=\"half\" width=\"50%\" height=\"10\"/></frame></window>",
        400.0f, 100.0f);
    CHECK(test, percent != nullptr);
    CHECK(test, std::fabs(percent->FindById("half")->Bounds().width - 200.0f) < 0.01f);

    std::unique_ptr<Element> minimum = Layout(
        "<window width=\"200\" height=\"80\"><frame id=\"min\" width=\"40\" height=\"20\" minWidth=\"80\" margin=\"10\"/></window>",
        200.0f, 80.0f);
    CHECK(test, minimum != nullptr);
    CHECK(test, std::fabs(minimum->FindById("min")->Measured().width - 100.0f) < 0.01f);
    CHECK(test, std::fabs(minimum->FindById("min")->Bounds().width - 80.0f) < 0.01f);

    std::unique_ptr<Element> placed = Layout(
        "<window width=\"200\" height=\"100\"><frame width=\"200\" height=\"100\" layout=\"vertical\">"
        "<frame id=\"abs\" position=\"absolute\" x=\"20\" y=\"30\" width=\"10\" height=\"10\"/>"
        "<frame id=\"neg\" position=\"absolute\" x=\"-32\" y=\"-8\" width=\"10\" height=\"10\"/>"
        "<frame id=\"mid\" position=\"absolute\" anchor=\"center\" width=\"40\" height=\"20\"/></frame></window>",
        200.0f, 100.0f);
    CHECK(test, placed != nullptr);
    CHECK(test, std::fabs(placed->FindById("abs")->Bounds().x - 20.0f) < 0.01f);
    CHECK(test, std::fabs(placed->FindById("abs")->Bounds().y - 30.0f) < 0.01f);
    CHECK(test, std::fabs(placed->FindById("neg")->Bounds().x - (-32.0f)) < 0.01f);
    CHECK(test, std::fabs(placed->FindById("neg")->Bounds().y - (-8.0f)) < 0.01f);

    std::unique_ptr<Element> padded = Layout(
        "<window width=\"200\" height=\"120\"><frame width=\"200\" height=\"120\" padding=\"32\">"
        "<frame id=\"inset\" position=\"absolute\" x=\"0\" y=\"0\" width=\"10\" height=\"10\"/>"
        "<frame id=\"out\" position=\"absolute\" x=\"-32\" y=\"8\" width=\"10\" height=\"10\"/></frame></window>",
        200.0f, 120.0f);
    CHECK(test, padded != nullptr);
    CHECK(test, std::fabs(padded->FindById("inset")->Bounds().x - 32.0f) < 0.01f);
    CHECK(test, std::fabs(padded->FindById("out")->Bounds().x - 0.0f) < 0.01f);
    CHECK(test, std::fabs(padded->FindById("out")->Bounds().y - 40.0f) < 0.01f);
    CHECK(test, std::fabs(placed->FindById("mid")->Bounds().x - 80.0f) < 0.01f);
    CHECK(test, std::fabs(placed->FindById("mid")->Bounds().y - 40.0f) < 0.01f);

    std::unique_ptr<Element> scrolled = Layout(
        "<window width=\"100\" height=\"50\"><frame id=\"scroll\" overflow=\"scroll\" layout=\"vertical\" width=\"100\" height=\"50\">"
        "<frame id=\"first\" width=\"40\" height=\"40\"/><frame width=\"40\" height=\"40\"/></frame></window>",
        100.0f, 50.0f);
    CHECK(test, scrolled != nullptr);
    Element* scroll = scrolled->FindById("scroll");
    CHECK(test, scroll->OnWheel(-120));
    scrolled->Arrange(Rect{0.0f, 0.0f, 100.0f, 50.0f});
    CHECK(test, scrolled->FindById("first")->Bounds().y < scroll->Bounds().y);

    std::unique_ptr<Element> hidden = Layout("<window width=\"80\" height=\"40\"><frame id=\"gone\" display=\"none\" width=\"40\" height=\"40\"/></window>",
                                             80.0f, 40.0f);
    CHECK(test, hidden != nullptr);
    CHECK(test, hidden->FindById("gone")->Measured().width == 0.0f);

    std::unique_ptr<Element> ratio = Layout(
        "<window width=\"200\" height=\"200\"><frame id=\"ratio\" width=\"100\" aspectRatio=\"2\"/></window>", 200.0f, 200.0f);
    CHECK(test, ratio != nullptr);
    CHECK(test, std::fabs(ratio->FindById("ratio")->Bounds().height - 50.0f) < 0.01f);
    std::unique_ptr<Element> packed = Layout(
        "<window width=\"200\" height=\"40\"><frame layout=\"horizontal\" justifyContent=\"spaceBetween\" width=\"200\" height=\"40\">"
        "<frame id=\"a\" width=\"20\" height=\"20\"/><frame id=\"b\" width=\"20\" height=\"20\"/><frame id=\"c\" width=\"20\" height=\"20\"/>"
        "</frame></window>",
        200.0f, 40.0f);
    CHECK(test, packed != nullptr);
    CHECK(test, std::fabs(packed->FindById("a")->Bounds().x) < 0.01f);
    CHECK(test, std::fabs(packed->FindById("b")->Bounds().x - 90.0f) < 0.01f);
    CHECK(test, std::fabs(packed->FindById("c")->Bounds().x - 180.0f) < 0.01f);

    std::unique_ptr<Element> ended = Layout(
        "<window width=\"200\" height=\"80\"><frame layout=\"horizontal\" alignItems=\"end\" width=\"200\" height=\"80\">"
        "<frame id=\"low\" width=\"40\" height=\"20\"/></frame></window>",
        200.0f, 80.0f);
    CHECK(test, ended != nullptr);
    CHECK(test, std::fabs(ended->FindById("low")->Bounds().y - 60.0f) < 0.01f);

    std::unique_ptr<Element> stretched = Layout(
        "<window width=\"200\" height=\"80\"><frame width=\"200\" height=\"80\">"
        "<frame id=\"bar\" position=\"absolute\" x=\"16\" y=\"20\" width=\"40\" height=\"20\" horizontal=\"leftRight\"/>"
        "</frame></window>",
        200.0f, 80.0f);
    CHECK(test, stretched != nullptr);
    CHECK(test, std::fabs(stretched->FindById("bar")->Bounds().x - 16.0f) < 0.01f);
    CHECK(test, std::fabs(stretched->FindById("bar")->Bounds().width - 168.0f) < 0.01f);

    std::unique_ptr<Element> wrapped = Layout(
        "<window width=\"200\" height=\"120\"><frame id=\"row\" layout=\"horizontal\" wrap=\"true\" gap=\"8\" width=\"160\" height=\"96\" padding=\"8\">"
        "<frame id=\"a\" width=\"40\" height=\"28\"/><frame id=\"b\" width=\"40\" height=\"28\"/><frame id=\"c\" width=\"40\" height=\"28\"/>"
        "<frame id=\"d\" width=\"40\" height=\"28\"/><frame id=\"e\" width=\"40\" height=\"28\"/></frame></window>",
        200.0f, 120.0f);
    CHECK(test, wrapped != nullptr);
    CHECK(test, std::fabs(wrapped->FindById("c")->Bounds().y - wrapped->FindById("a")->Bounds().y) < 0.01f);
    CHECK(test, wrapped->FindById("d")->Bounds().y > wrapped->FindById("a")->Bounds().y);
    CHECK(test, std::fabs(wrapped->FindById("d")->Bounds().x - wrapped->FindById("a")->Bounds().x) < 0.01f);
    CHECK(test, wrapped->FindById("e")->Bounds().x > wrapped->FindById("d")->Bounds().x);

    std::unique_ptr<Element> scaled = Layout(
        "<window width=\"200\" height=\"200\"><frame width=\"120\" height=\"120\">"
        "<frame id=\"bar\" position=\"absolute\" vertical=\"scale\" x=\"36\" y=\"20%\" width=\"48\" height=\"40%\"/>"
        "</frame></window>",
        200.0f, 200.0f);
    CHECK(test, scaled != nullptr);
    CHECK(test, std::fabs(scaled->FindById("bar")->Bounds().height - 48.0f) < 0.01f);
    CHECK(test, std::fabs(scaled->FindById("bar")->Bounds().y - 24.0f) < 0.01f);

    std::unique_ptr<Element> paddedFill = Layout(
        "<window width=\"400\" height=\"200\">"
        "<frame id=\"even\" layout=\"horizontal\" padding=\"8\" width=\"140\" height=\"72\">"
        "<frame id=\"evenInner\" width=\"fill\" height=\"fill\"/></frame>"
        "<frame id=\"uneven\" layout=\"horizontal\" paddingTop=\"24\" paddingRight=\"8\" paddingBottom=\"8\" paddingLeft=\"40\" width=\"180\" height=\"88\">"
        "<frame id=\"unevenInner\" width=\"fill\" height=\"fill\"/></frame>"
        "</window>",
        400.0f, 200.0f);
    CHECK(test, paddedFill != nullptr);
    CHECK(test, std::fabs(paddedFill->FindById("evenInner")->Bounds().x - 8.0f) < 0.01f);
    CHECK(test, std::fabs(paddedFill->FindById("evenInner")->Bounds().y - 8.0f) < 0.01f);
    CHECK(test, std::fabs(paddedFill->FindById("evenInner")->Bounds().width - 124.0f) < 0.01f);
    CHECK(test, std::fabs(paddedFill->FindById("evenInner")->Bounds().height - 56.0f) < 0.01f);
    CHECK(test, std::fabs(paddedFill->FindById("unevenInner")->Bounds().x - (paddedFill->FindById("uneven")->Bounds().x + 40.0f)) < 0.01f);
    CHECK(test, std::fabs(paddedFill->FindById("unevenInner")->Bounds().y - (paddedFill->FindById("uneven")->Bounds().y + 24.0f)) < 0.01f);
    CHECK(test, std::fabs(paddedFill->FindById("unevenInner")->Bounds().width - 132.0f) < 0.01f);
    CHECK(test, std::fabs(paddedFill->FindById("unevenInner")->Bounds().height - 56.0f) < 0.01f);

    std::unique_ptr<Element> filled = Layout(
        "<window width=\"800\" height=\"400\" layout=\"vertical\" padding=\"24\">"
        "<frame id=\"card\" layout=\"vertical\" width=\"fill\" heightMode=\"hug\" padding=\"16\">"
        "<frame id=\"row\" layout=\"horizontal\" width=\"fill\" gap=\"16\">"
        "<frame id=\"a\" width=\"fill\" height=\"40\"/><frame id=\"b\" width=\"fill\" height=\"40\"/><frame id=\"c\" width=\"fill\" height=\"40\"/>"
        "</frame></frame></window>",
        800.0f, 400.0f);
    CHECK(test, filled != nullptr);
    CHECK(test, std::fabs(filled->FindById("card")->Bounds().width - 752.0f) < 0.01f);
    CHECK(test, std::fabs(filled->FindById("row")->Bounds().width - 720.0f) < 0.01f);
    CHECK(test, std::fabs(filled->FindById("a")->Bounds().width - 229.333f) < 0.5f);
    CHECK(test, std::fabs(filled->FindById("b")->Bounds().x - (filled->FindById("a")->Bounds().x + filled->FindById("a")->Bounds().width + 16.0f)) < 0.5f);
    CHECK(test, std::fabs(filled->FindById("c")->Bounds().width - filled->FindById("a")->Bounds().width) < 0.5f);

    std::unique_ptr<Element> bar = Layout(
        "<window width=\"120\" height=\"100\"><frame id=\"scroller\" overflow=\"scroll\" clip=\"true\" scrollbarVisibility=\"visible\" width=\"120\" height=\"100\">"
        "<frame id=\"block\" width=\"40\" height=\"240\"/>"
        "<frame id=\"track\" role=\"scrollbar\" orientation=\"vertical\" position=\"absolute\" width=\"14\" height=\"100\">"
        "<frame id=\"up\" role=\"decrement\" width=\"14\" height=\"14\"/>"
        "<frame id=\"thumb\" role=\"thumb\" width=\"14\" height=\"20\"/>"
        "<frame id=\"down\" role=\"increment\" width=\"14\" height=\"14\"/></frame></frame></window>",
        120.0f, 100.0f);
    CHECK(test, bar != nullptr);
    Element* thumb = bar->FindById("thumb");
    Element* track = bar->FindById("track");
    Element* block = bar->FindById("block");
    Element* down = bar->FindById("down");
    CHECK(test, thumb != nullptr && track != nullptr && block != nullptr && down != nullptr);
    CHECK(test, thumb->Bounds().height < track->Bounds().height - 1.0f);
    CHECK(test, thumb->Bounds().y > track->Bounds().y + 1.0f);
    CHECK(test, !track->WantsHandCursor());
    const float beforeArrow = block->Bounds().y;
    down->OnPointerDown(Point{down->Bounds().x + 2.0f, down->Bounds().y + 2.0f});
    bar->Arrange(Rect{0.0f, 0.0f, 120.0f, 100.0f});
    CHECK(test, block->Bounds().y < beforeArrow);
    thumb->OnPointerDown(Point{thumb->Bounds().x + 2.0f, thumb->Bounds().y + 4.0f});
    thumb->OnDrag(Point{track->Bounds().x + 2.0f, track->Bounds().y + track->Bounds().height - 2.0f});
    bar->Arrange(Rect{0.0f, 0.0f, 120.0f, 100.0f});
    CHECK(test, block->Bounds().y < -1.0f);
    CHECK(test, thumb->Bounds().y > track->Bounds().y + 1.0f);

    std::unique_ptr<Element> hbar = Layout(
        "<window width=\"160\" height=\"80\"><frame id=\"hscroll\" overflow=\"scroll\" clip=\"true\" scrollbarVisibility=\"visible\" width=\"160\" height=\"80\">"
        "<frame id=\"wide\" width=\"400\" height=\"40\"/>"
        "<frame id=\"htrack\" role=\"scrollbar\" orientation=\"horizontal\" position=\"absolute\" width=\"160\" height=\"14\">"
        "<frame id=\"left\" role=\"decrement\" width=\"14\" height=\"14\"/>"
        "<frame id=\"hthumb\" role=\"thumb\" width=\"20\" height=\"14\"/>"
        "<frame id=\"right\" role=\"increment\" width=\"14\" height=\"14\"/></frame></frame></window>",
        160.0f, 80.0f);
    CHECK(test, hbar != nullptr);
    Element* hthumb = hbar->FindById("hthumb");
    Element* htrack = hbar->FindById("htrack");
    Element* wide = hbar->FindById("wide");
    CHECK(test, hthumb != nullptr && htrack != nullptr && wide != nullptr);
    CHECK(test, hthumb->Bounds().width < htrack->Bounds().width - 1.0f);
    CHECK(test, hbar->FindById("hscroll")->OnWheel(-120, true));
    hbar->Arrange(Rect{0.0f, 0.0f, 160.0f, 80.0f});
    CHECK(test, wide->Bounds().x < hbar->FindById("hscroll")->Bounds().x - 1.0f);

    // Wheel distance follows the raw delta so precision touchpads move a fraction of a notch.
    std::unique_ptr<Element> fine = Layout(
        "<window width=\"100\" height=\"50\"><frame id=\"fine\" overflow=\"scroll\" layout=\"vertical\" width=\"100\" height=\"50\">"
        "<frame id=\"fineFirst\" width=\"40\" height=\"200\"/></frame></window>",
        100.0f, 50.0f);
    CHECK(test, fine != nullptr);
    Element* fineScroll = fine->FindById("fine");
    CHECK(test, fineScroll->OnWheel(-8));
    fine->Arrange(Rect{0.0f, 0.0f, 100.0f, 50.0f});
    const float smallShift = fineScroll->Bounds().y - fine->FindById("fineFirst")->Bounds().y;
    CHECK(test, smallShift > 0.0f && smallShift < 48.0f);
    CHECK(test, fineScroll->OnWheel(-8));
    fine->Arrange(Rect{0.0f, 0.0f, 100.0f, 50.0f});
    const float secondShift = fineScroll->Bounds().y - fine->FindById("fineFirst")->Bounds().y;
    CHECK(test, secondShift > smallShift && secondShift < 48.0f);

    // A gesture moves its own axis and never the other one. A two-finger vertical swipe over
    // a sideways scroller must leave it where it is, and a sideways swipe must not move a
    // vertical scroller. An axis with nothing left to scroll hands the gesture on instead.
    std::unique_ptr<Element> sideways = Layout(
        "<window width=\"100\" height=\"50\"><frame id=\"sideways\" overflow=\"scroll\" clip=\"true\" layout=\"horizontal\" width=\"100\" height=\"50\">"
        "<frame id=\"sidewaysItem\" width=\"400\" height=\"40\"/></frame></window>",
        100.0f, 50.0f);
    CHECK(test, sideways != nullptr);
    Element* sidewaysFrame = sideways->FindById("sideways");
    CHECK(test, sidewaysFrame != nullptr);
    CHECK(test, !sidewaysFrame->OnWheel(-120));
    sideways->Arrange(Rect{0.0f, 0.0f, 100.0f, 50.0f});
    CHECK(test, std::fabs(sideways->FindById("sidewaysItem")->Bounds().x - sidewaysFrame->Bounds().x) < 0.01f);

    std::unique_ptr<Element> upright = Layout(
        "<window width=\"100\" height=\"50\"><frame id=\"upright\" overflow=\"scroll\" clip=\"true\" layout=\"vertical\" width=\"100\" height=\"50\">"
        "<frame id=\"uprightItem\" width=\"40\" height=\"200\"/></frame></window>",
        100.0f, 50.0f);
    CHECK(test, upright != nullptr);
    Element* uprightFrame = upright->FindById("upright");
    CHECK(test, uprightFrame != nullptr);
    // The delta is one that would move an upright scroller, so a refusal here is the axis
    // holding rather than nothing being left to scroll.
    CHECK(test, !uprightFrame->OnWheel(-120, false, 1));
    upright->Arrange(Rect{0.0f, 0.0f, 100.0f, 50.0f});
    CHECK(test, std::fabs(upright->FindById("uprightItem")->Bounds().y - uprightFrame->Bounds().y) < 0.01f);

    // Shift asks a vertical wheel for the X axis. That is a request for sideways motion, so an
    // upright scroller is not the one to spend it on - it declines and the gesture moves on.
    std::unique_ptr<Element> shifted = Layout(
        "<window width=\"100\" height=\"50\"><frame id=\"shifted\" overflow=\"scroll\" layout=\"vertical\" width=\"100\" height=\"50\">"
        "<frame id=\"shiftedFirst\" width=\"40\" height=\"200\"/></frame></window>",
        100.0f, 50.0f);
    CHECK(test, shifted != nullptr);
    Element* shiftedFrame = shifted->FindById("shifted");
    CHECK(test, shiftedFrame != nullptr);
    CHECK(test, !shiftedFrame->OnWheel(-120, true));
    shifted->Arrange(Rect{0.0f, 0.0f, 100.0f, 50.0f});
    CHECK(test, std::fabs(shifted->FindById("shiftedFirst")->Bounds().y - shiftedFrame->Bounds().y) < 0.01f);

    // Declining a gesture is what lets an enclosing scroller take it: content that fits inside
    // its own box leaves the wheel to the page around it, so the walk outward keeps scrolling.
    std::unique_ptr<Element> nested = Layout(
        "<window width=\"100\" height=\"50\"><frame id=\"outer\" overflow=\"scroll\" layout=\"vertical\" width=\"100\" height=\"50\">"
        "<frame id=\"inner\" overflow=\"scroll\" width=\"100\" height=\"20\"><frame id=\"fits\" width=\"40\" height=\"20\"/></frame>"
        "<frame id=\"tail\" width=\"40\" height=\"200\"/></frame></window>",
        100.0f, 50.0f);
    CHECK(test, nested != nullptr);
    Element* nestedInner = nested->FindById("inner");
    Element* nestedOuter = nested->FindById("outer");
    CHECK(test, nestedInner != nullptr && nestedOuter != nullptr);
    CHECK(test, !nestedInner->OnWheel(-120));
    CHECK(test, nestedOuter->OnWheel(-120));

    // A horizontal wheel or trackpad swipe scrolls the X axis of a horizontal scroller.
    std::unique_ptr<Element> hwheel = Layout(
        "<window width=\"80\" height=\"40\"><frame id=\"hwheel\" overflow=\"scroll\" clip=\"true\" layout=\"horizontal\" width=\"80\" height=\"40\">"
        "<frame id=\"hwheelWide\" width=\"400\" height=\"40\"/></frame></window>",
        80.0f, 40.0f);
    CHECK(test, hwheel != nullptr);
    CHECK(test, hwheel->FindById("hwheel")->OnWheel(120, false, 1));
    hwheel->Arrange(Rect{0.0f, 0.0f, 80.0f, 40.0f});
    CHECK(test, hwheel->FindById("hwheelWide")->Bounds().x < hwheel->FindById("hwheel")->Bounds().x - 1.0f);

    // A fill width capped by maxWidth gives content something to overflow against.
    std::unique_ptr<Element> capped = Layout(
        "<window width=\"800\" height=\"120\"><frame layout=\"vertical\" width=\"800\" height=\"120\">"
        "<frame id=\"capped\" overflow=\"scroll\" layout=\"horizontal\" width=\"fill\" maxWidth=\"300\" height=\"60\">"
        "<frame id=\"cappedWide\" width=\"500\" height=\"40\"/></frame></frame></window>",
        800.0f, 120.0f);
    CHECK(test, capped != nullptr);
    CHECK(test, std::fabs(capped->FindById("capped")->Bounds().width - 300.0f) < 0.01f);
    CHECK(test, capped->FindById("capped")->OnWheel(120, false, 1));
    capped->Arrange(Rect{0.0f, 0.0f, 800.0f, 120.0f});
    CHECK(test, capped->FindById("cappedWide")->Bounds().x < capped->FindById("capped")->Bounds().x - 1.0f);

    // A maxWidth-capped scroller keeps its scroll range across a real layout pass. The
    // runtime re-measures whenever the wheel moves, so clamping the scroll against the
    // unclamped fill width drew a thumb that snapped back to the start on every frame.
    std::unique_ptr<Element> cappedScroll = Layout(
        "<window width=\"800\" height=\"120\"><frame layout=\"vertical\" width=\"800\" height=\"120\">"
        "<frame id=\"cappedTrack\" overflow=\"scroll\" clip=\"true\" scrollbarVisibility=\"visible\" layout=\"horizontal\""
        " width=\"fill\" maxWidth=\"300\" height=\"60\">"
        "<frame id=\"cappedItem\" width=\"500\" height=\"40\"/>"
        "<frame id=\"cappedBar\" role=\"scrollbar\" orientation=\"horizontal\" position=\"absolute\" width=\"300\" height=\"14\">"
        "<frame id=\"cappedLeft\" role=\"decrement\" width=\"14\" height=\"14\"/>"
        "<frame id=\"cappedThumb\" role=\"thumb\" width=\"20\" height=\"14\"/>"
        "<frame id=\"cappedRight\" role=\"increment\" width=\"14\" height=\"14\"/></frame>"
        "</frame></frame></window>",
        800.0f, 120.0f);
    CHECK(test, cappedScroll != nullptr);
    Element* cappedFrame = cappedScroll->FindById("cappedTrack");
    Element* cappedItem = cappedScroll->FindById("cappedItem");
    Element* cappedThumb = cappedScroll->FindById("cappedThumb");
    CHECK(test, cappedFrame != nullptr && cappedItem != nullptr && cappedThumb != nullptr);
    CHECK(test, std::fabs(cappedFrame->Bounds().width - 300.0f) < 0.01f);
    CHECK(test, cappedFrame->OnWheel(120, false, 1));
    FakeMeasurer cappedMeasurer;
    auto relayoutCapped = [&]() {
        cappedScroll->SetViewport(800.0f, 120.0f);
        cappedScroll->Measure(cappedMeasurer, 800.0f, 120.0f, true);
        cappedScroll->Arrange(Rect{0.0f, 0.0f, 800.0f, 120.0f});
    };
    relayoutCapped();
    CHECK(test, std::fabs((cappedFrame->Bounds().x - cappedItem->Bounds().x) - 48.0f) < 0.01f);
    const float cappedThumbStart = cappedThumb->Bounds().x;
    CHECK(test, cappedThumbStart > cappedFrame->Bounds().x + 14.0f);
    for (int step = 0; step < 8; ++step) {
        cappedFrame->OnWheel(120, false, 1);
        relayoutCapped();
    }
    CHECK(test, std::fabs(cappedItem->Bounds().x - (cappedFrame->Bounds().x - 200.0f)) < 0.01f);
    CHECK(test, cappedThumb->Bounds().x > cappedThumbStart);

    // A drag that reaches the scroll frame arms a pan, then moves the content (touch/mouse drag).
    std::unique_ptr<Element> panned = Layout(
        "<window width=\"100\" height=\"50\"><frame id=\"panned\" overflow=\"scroll\" clip=\"true\" width=\"100\" height=\"50\">"
        "<frame id=\"panBlock\" width=\"40\" height=\"200\"/></frame></window>",
        100.0f, 50.0f);
    CHECK(test, panned != nullptr);
    Element* panScroll = panned->FindById("panned");
    CHECK(test, panScroll->OnDrag(Point{50.0f, 50.0f}));
    CHECK(test, panScroll->OnDrag(Point{50.0f, 20.0f}));
    panned->Arrange(Rect{0.0f, 0.0f, 100.0f, 50.0f});
    CHECK(test, panned->FindById("panBlock")->Bounds().y < panScroll->Bounds().y - 1.0f);

    // Page keys move by a page and Shift+Space pages back up.
    std::unique_ptr<Element> keyed = Layout(
        "<window width=\"100\" height=\"50\"><frame id=\"keyed\" overflow=\"scroll\" layout=\"vertical\" width=\"100\" height=\"50\">"
        "<frame id=\"keyedFirst\" width=\"40\" height=\"200\"/></frame></window>",
        100.0f, 50.0f);
    CHECK(test, keyed != nullptr);
    Element* keyScroll = keyed->FindById("keyed");
    CHECK(test, keyScroll->OnKeyDown(0x22, false));
    keyed->Arrange(Rect{0.0f, 0.0f, 100.0f, 50.0f});
    const float afterPageDown = keyScroll->Bounds().y - keyed->FindById("keyedFirst")->Bounds().y;
    CHECK(test, afterPageDown > 1.0f);
    CHECK(test, keyScroll->OnKeyDown(0x20, true));
    keyed->Arrange(Rect{0.0f, 0.0f, 100.0f, 50.0f});
    const float afterPageUp = keyScroll->Bounds().y - keyed->FindById("keyedFirst")->Bounds().y;
    CHECK(test, afterPageUp < afterPageDown);
    Finish(test, "frame layout", before);
}

void TestPrimitives(TestContext& test) {
    const int before = test.failed;
    std::unique_ptr<Element> root = Layout(
        "<window width=\"300\" height=\"200\"><frame width=\"300\" height=\"200\">"
        "<text id=\"label\" text=\"Hi\" position=\"absolute\" x=\"10\" y=\"12\" width=\"40\" height=\"20\"/>"
        "<vector id=\"dot\" shape=\"ellipse\" position=\"absolute\" x=\"20\" y=\"40\" width=\"16\" height=\"16\" fill=\"#000000\"/>"
        "<line id=\"rule\" x2=\"80\" position=\"absolute\" x=\"0\" y=\"70\" width=\"80\" height=\"8\"/>"
        "<component name=\"Badge\"><text text=\"Ok\"/></component>"
        "<instance id=\"badge\" component=\"Badge\" position=\"absolute\" x=\"60\" y=\"20\" width=\"40\" height=\"20\"/>"
        "<scroll id=\"scroller\" width=\"40\" height=\"40\"/>"
        "</frame></window>",
        300.0f, 200.0f);
    CHECK(test, root != nullptr);
    CHECK(test, root->FindById("label") != nullptr);
    CHECK(test, root->FindById("label")->Kind() == NodeKind::TEXT);
    CHECK(test, std::fabs(root->FindById("label")->Bounds().x - 10.0f) < 0.01f);
    CHECK(test, root->FindById("dot")->Kind() == NodeKind::VECTOR);
    CHECK(test, root->FindById("rule")->Kind() == NodeKind::LINE);
    CHECK(test, root->FindById("badge")->Kind() == NodeKind::INSTANCE);
    CHECK(test, root->FindById("badge")->Children().size() == 1);
    CHECK(test, root->FindById("scroller")->Kind() == NodeKind::SCROLL);
    Finish(test, "primitives", before);
}

void TestGroup(TestContext& test) {
    const int before = test.failed;

    // A group is one item in the parent's flow even though it holds several children, and
    // it wraps exactly those children.
    std::unique_ptr<Element> row = Layout(
        "<window width=\"400\" height=\"200\"><frame id=\"row\" layout=\"horizontal\" gap=\"10\" width=\"400\" height=\"200\">"
        "<frame id=\"lead\" width=\"40\" height=\"40\" background=\"#010203\"/>"
        "<group id=\"pair\" opacity=\"0.5\" rotation=\"30\" clip=\"true\">"
        "<frame id=\"one\" x=\"0\" y=\"0\" width=\"20\" height=\"30\" background=\"#111111\"/>"
        "<frame id=\"two\" x=\"30\" y=\"10\" width=\"20\" height=\"30\" background=\"#222222\"/>"
        "</group>"
        "<frame id=\"tail\" width=\"40\" height=\"40\" background=\"#333333\"/>"
        "</frame></window>",
        400.0f, 200.0f);
    CHECK(test, row != nullptr);
    Element* pair = row != nullptr ? row->FindById("pair") : nullptr;
    Element* one = row != nullptr ? row->FindById("one") : nullptr;
    Element* two = row != nullptr ? row->FindById("two") : nullptr;
    Element* tail = row != nullptr ? row->FindById("tail") : nullptr;
    CHECK(test, pair != nullptr && one != nullptr && two != nullptr && tail != nullptr);
    if (pair != nullptr && one != nullptr && two != nullptr && tail != nullptr) {
        CHECK(test, pair->Kind() == NodeKind::GROUP);
        CHECK(test, std::fabs(pair->Bounds().x - 50.0f) < 0.01f);
        CHECK(test, std::fabs(pair->Bounds().width - 50.0f) < 0.01f);
        CHECK(test, std::fabs(pair->Bounds().height - 40.0f) < 0.01f);
        CHECK(test, std::fabs(tail->Bounds().x - 110.0f) < 0.01f);
        CHECK(test, std::fabs(one->Bounds().x - pair->Bounds().x) < 0.01f);
        CHECK(test, std::fabs(two->Bounds().x - (pair->Bounds().x + 30.0f)) < 0.01f);
        CHECK(test, std::fabs(two->Bounds().y - (pair->Bounds().y + 10.0f)) < 0.01f);

        // One paint pass proves the group hands its opacity, its clip, and its rotation to
        // every child, paints nothing of its own box, and leaves its siblings alone.
        RecordingRenderer renderer;
        row->Paint(renderer);
        const PaintRecord* leadPaint = PaintRecordFor(renderer, row->FindById("lead")->Bounds());
        const PaintRecord* onePaint = PaintRecordFor(renderer, one->Bounds());
        const PaintRecord* twoPaint = PaintRecordFor(renderer, two->Bounds());
        const PaintRecord* tailPaint = PaintRecordFor(renderer, tail->Bounds());
        CHECK(test, PaintRecordFor(renderer, pair->Bounds()) == nullptr);
        CHECK(test, leadPaint != nullptr && onePaint != nullptr && twoPaint != nullptr && tailPaint != nullptr);
        if (leadPaint != nullptr && onePaint != nullptr && twoPaint != nullptr && tailPaint != nullptr) {
            CHECK(test, std::fabs(leadPaint->opacity - 1.0f) < 0.001f && leadPaint->clipDepth == 0 && leadPaint->rotationDepth == 0);
            CHECK(test, std::fabs(tailPaint->opacity - 1.0f) < 0.001f && tailPaint->clipDepth == 0 && tailPaint->rotationDepth == 0);
            CHECK(test, std::fabs(onePaint->opacity - 0.5f) < 0.001f);
            CHECK(test, onePaint->clipDepth == 1 && onePaint->rotationDepth == 1);
            CHECK(test, std::fabs(onePaint->rotation - 30.0f) < 0.01f);
            CHECK(test, std::fabs(twoPaint->opacity - 0.5f) < 0.001f && twoPaint->clipDepth == 1 && twoPaint->rotationDepth == 1);
        }
    }

    // Nested groups multiply, so a child inside a group inside a group sees both opacities.
    std::unique_ptr<Element> nested = Layout(
        "<window width=\"100\" height=\"100\"><frame width=\"100\" height=\"100\">"
        "<group id=\"outer\" opacity=\"0.5\"><group id=\"inner\" opacity=\"0.5\">"
        "<frame id=\"deep\" width=\"20\" height=\"20\" background=\"#010101\"/></group></group>"
        "</frame></window>",
        100.0f, 100.0f);
    CHECK(test, nested != nullptr && nested->FindById("deep") != nullptr);
    if (nested != nullptr && nested->FindById("deep") != nullptr) {
        CHECK(test, nested->FindById("outer")->Kind() == NodeKind::GROUP);
        CHECK(test, nested->FindById("inner")->Kind() == NodeKind::GROUP);
        RecordingRenderer renderer;
        nested->Paint(renderer);
        const PaintRecord* deepPaint = PaintRecordFor(renderer, nested->FindById("deep")->Bounds());
        CHECK(test, deepPaint != nullptr && std::fabs(deepPaint->opacity - 0.25f) < 0.001f);
    }

    // An explicit box moves and resizes every child with it.
    std::unique_ptr<Element> scaled = Layout(
        "<window width=\"400\" height=\"200\"><frame width=\"400\" height=\"200\">"
        "<group id=\"scaled\" x=\"0\" y=\"0\" width=\"100\" height=\"80\">"
        "<frame id=\"inner\" x=\"10\" y=\"20\" width=\"20\" height=\"20\"/></group>"
        "</frame></window>",
        400.0f, 200.0f);
    CHECK(test, scaled != nullptr && scaled->FindById("scaled") != nullptr && scaled->FindById("inner") != nullptr);
    if (scaled != nullptr && scaled->FindById("scaled") != nullptr && scaled->FindById("inner") != nullptr) {
        Element* box = scaled->FindById("scaled");
        Element* inner = scaled->FindById("inner");
        CHECK(test, std::fabs(box->Bounds().width - 100.0f) < 0.01f && std::fabs(box->Bounds().height - 80.0f) < 0.01f);
        CHECK(test, std::fabs(inner->Bounds().x - (box->Bounds().x + 10.0f * 100.0f / 30.0f)) < 0.01f);
        CHECK(test, std::fabs(inner->Bounds().y - (box->Bounds().y + 40.0f)) < 0.01f);
        CHECK(test, std::fabs(inner->Bounds().width - 20.0f * 100.0f / 30.0f) < 0.01f);
        CHECK(test, std::fabs(inner->Bounds().height - 40.0f) < 0.01f);
    }

    // Min and max caps limit the group box and the children follow it.
    std::unique_ptr<Element> capped = Layout(
        "<window width=\"400\" height=\"200\"><frame width=\"400\" height=\"200\">"
        "<group id=\"capped\" width=\"300\" maxWidth=\"120\"><frame id=\"capKid\" width=\"40\" height=\"40\"/></group>"
        "</frame></window>",
        400.0f, 200.0f);
    CHECK(test, capped != nullptr && capped->FindById("capped") != nullptr && capped->FindById("capKid") != nullptr);
    if (capped != nullptr && capped->FindById("capped") != nullptr && capped->FindById("capKid") != nullptr) {
        CHECK(test, std::fabs(capped->FindById("capped")->Bounds().width - 120.0f) < 0.01f);
        CHECK(test, std::fabs(capped->FindById("capKid")->Bounds().width - 120.0f) < 0.01f);
    }

    // Scale, translate, and the transform shorthand reach the children as geometry.
    std::unique_ptr<Element> moved = Layout(
        "<window width=\"200\" height=\"200\"><frame width=\"200\" height=\"200\">"
        "<group id=\"moved\" transform=\"translate(6, 4) scale(2) rotate(15)\">"
        "<frame id=\"mark\" x=\"0\" y=\"0\" width=\"10\" height=\"10\"/></group>"
        "</frame></window>",
        200.0f, 200.0f);
    CHECK(test, moved != nullptr && moved->FindById("moved") != nullptr);
    if (moved != nullptr && moved->FindById("moved") != nullptr) {
        GroupElement* typed = dynamic_cast<GroupElement*>(moved->FindById("moved"));
        Element* mark = moved->FindById("mark");
        CHECK(test, typed != nullptr && mark != nullptr);
        if (typed != nullptr && mark != nullptr) {
            CHECK(test, typed->Transform() == "translate(6, 4) scale(2) rotate(15)");
            CHECK(test, std::fabs(typed->Rotation() - 15.0f) < 0.01f);
            CHECK(test, std::fabs(typed->Bounds().x - 6.0f) < 0.01f);
            CHECK(test, std::fabs(typed->Bounds().y - 4.0f) < 0.01f);
            CHECK(test, std::fabs(typed->Bounds().width - 20.0f) < 0.01f);
            CHECK(test, std::fabs(mark->Bounds().x - typed->Bounds().x) < 0.01f);
            CHECK(test, std::fabs(mark->Bounds().width - 20.0f) < 0.01f);
        }
    }

    // Clicking a child keeps the child, clicking the empty part of the group returns the
    // group so the whole group is selected and moved as one, and the outside matches
    // nothing.
    std::unique_ptr<Element> hits = Layout(
        "<window width=\"200\" height=\"100\"><frame id=\"stage\" width=\"200\" height=\"100\">"
        "<group id=\"hitBox\" x=\"10\" y=\"10\">"
        "<frame id=\"pad\" x=\"0\" y=\"0\" width=\"10\" height=\"10\" background=\"#040506\"/>"
        "<frame id=\"corner\" x=\"30\" y=\"10\" width=\"10\" height=\"10\" background=\"#010203\"/>"
        "</group></frame></window>",
        200.0f, 100.0f);
    CHECK(test, hits != nullptr && hits->FindById("hitBox") != nullptr);
    if (hits != nullptr && hits->FindById("hitBox") != nullptr) {
        Element* hitBox = hits->FindById("hitBox");
        CHECK(test, std::fabs(hitBox->Bounds().width - 40.0f) < 0.01f && std::fabs(hitBox->Bounds().height - 20.0f) < 0.01f);
        Element* pad = hits->HitTest(Point{15.0f, 15.0f});
        Element* corner = hits->HitTest(Point{45.0f, 25.0f});
        Element* empty = hits->HitTest(Point{35.0f, 15.0f});
        Element* outside = hits->HitTest(Point{90.0f, 60.0f});
        CHECK(test, pad != nullptr && pad->Id() == "pad");
        CHECK(test, corner != nullptr && corner->Id() == "corner");
        CHECK(test, empty == hitBox);
        CHECK(test, outside != nullptr && outside->Id() == "stage");
    }

    // Children keep the offsets they were written with, so a group does not move what it
    // wraps and a child at a negative offset simply hangs outside the box.
    std::unique_ptr<Element> negative = Layout(
        "<window width=\"200\" height=\"100\"><frame width=\"200\" height=\"100\">"
        "<group id=\"negative\" x=\"50\" y=\"50\">"
        "<frame id=\"left\" x=\"-20\" y=\"0\" width=\"20\" height=\"20\"/>"
        "<frame id=\"right\" x=\"10\" y=\"0\" width=\"20\" height=\"20\"/>"
        "</group></frame></window>",
        200.0f, 100.0f);
    CHECK(test, negative != nullptr && negative->FindById("negative") != nullptr && negative->FindById("left") != nullptr);
    if (negative != nullptr && negative->FindById("negative") != nullptr && negative->FindById("left") != nullptr) {
        Element* negativeGroup = negative->FindById("negative");
        Element* left = negative->FindById("left");
        Element* right = negative->FindById("right");
        CHECK(test, std::fabs(negativeGroup->Bounds().x - 50.0f) < 0.01f);
        CHECK(test, std::fabs(negativeGroup->Bounds().width - 30.0f) < 0.01f);
        CHECK(test, std::fabs(left->Bounds().x - (negativeGroup->Bounds().x - 20.0f)) < 0.01f);
        CHECK(test, std::fabs(right->Bounds().x - (negativeGroup->Bounds().x + 10.0f)) < 0.01f);
    }

    // A clipped group keeps a child that hangs outside the box from taking the click.
    const char* spilled =
        "<window width=\"200\" height=\"100\"><frame id=\"stage\" width=\"200\" height=\"100\">"
        "<group id=\"clipBox\"%s><frame id=\"poke\" translateX=\"30\" width=\"20\" height=\"20\" background=\"#010203\"/></group>"
        "</frame></window>";
    char clippedXml[512];
    char openXml[512];
    std::snprintf(clippedXml, sizeof(clippedXml), spilled, " clip=\"true\"");
    std::snprintf(openXml, sizeof(openXml), spilled, "");
    std::unique_ptr<Element> clipped = Layout(clippedXml, 200.0f, 100.0f);
    std::unique_ptr<Element> open = Layout(openXml, 200.0f, 100.0f);
    CHECK(test, clipped != nullptr && open != nullptr);
    if (clipped != nullptr && open != nullptr && clipped->FindById("poke") != nullptr && open->FindById("poke") != nullptr) {
        const Rect pokeBounds = open->FindById("poke")->Bounds();
        CHECK(test, std::fabs(pokeBounds.x - 30.0f) < 0.01f);
        Element* openHit = open->HitTest(Point{40.0f, 10.0f});
        Element* clippedHit = clipped->HitTest(Point{40.0f, 10.0f});
        CHECK(test, openHit != nullptr && openHit->Id() == "poke");
        CHECK(test, clippedHit != nullptr && clippedHit->Id() == "stage");
        RecordingRenderer renderer;
        clipped->Paint(renderer);
        const PaintRecord* pokePaint = PaintRecordFor(renderer, clipped->FindById("poke")->Bounds());
        CHECK(test, pokePaint != nullptr && pokePaint->clipDepth == 1);
    }

    // Hiding the group hides every child, and a group leaves hit testing silent too.
    std::unique_ptr<Element> hidden = Layout(
        "<window width=\"200\" height=\"100\"><frame id=\"stage\" width=\"200\" height=\"100\">"
        "<group id=\"gone\" visible=\"false\" x=\"10\" y=\"10\"><frame id=\"ghost\" width=\"40\" height=\"20\" background=\"#010203\"/></group>"
        "</frame></window>",
        200.0f, 100.0f);
    CHECK(test, hidden != nullptr && hidden->FindById("gone") != nullptr);
    if (hidden != nullptr && hidden->FindById("gone") != nullptr) {
        Element* gone = hidden->FindById("gone");
        Element* ghost = hidden->FindById("ghost");
        CHECK(test, std::fabs(gone->Measured().width) < 0.01f && std::fabs(gone->Measured().height) < 0.01f);
        Element* hiddenHit = hidden->HitTest(Point{20.0f, 20.0f});
        CHECK(test, hiddenHit != nullptr && hiddenHit->Id() == "stage");
        RecordingRenderer renderer;
        hidden->Paint(renderer);
        CHECK(test, ghost != nullptr && PaintRecordFor(renderer, ghost->Bounds()) == nullptr);
    }

    // The cursor of a group belongs to the whole group, so a child without one still shows it.
    std::unique_ptr<Element> pointerGroup = Layout(
        "<window width=\"200\" height=\"100\"><frame width=\"200\" height=\"100\">"
        "<group id=\"hand\" cursor=\"pointer\"><frame id=\"quietChild\" x=\"0\" y=\"0\" width=\"40\" height=\"40\"/></group>"
        "</frame></window>",
        200.0f, 100.0f);
    std::unique_ptr<Element> plainGroup = Layout(
        "<window width=\"200\" height=\"100\"><frame width=\"200\" height=\"100\">"
        "<group id=\"plain\"><frame id=\"plainChild\" x=\"0\" y=\"0\" width=\"40\" height=\"40\"/></group>"
        "</frame></window>",
        200.0f, 100.0f);
    CHECK(test, pointerGroup != nullptr && plainGroup != nullptr);
    if (pointerGroup != nullptr && plainGroup != nullptr) {
        PointerInput pointer;
        pointer.OnMove(*pointerGroup, Point{20.0f, 20.0f});
        CHECK(test, pointer.Hovered() != nullptr && pointer.Hovered()->Id() == "quietChild");
        CHECK(test, pointer.HoverWantsHand());
        PointerInput plain;
        plain.OnMove(*plainGroup, Point{20.0f, 20.0f});
        CHECK(test, !plain.HoverWantsHand());
    }

    // The group takes every property it can hand to a child, and the compiler refuses the
    // ones that belong to layout instead of accepting them and ignoring them.
    CHECK(test, CompileUi(Source(
                  "<window><group transform=\"scale(2)\" rotation=\"5\" scaleX=\"1.5\" translateY=\"3\" visibility=\"visible\" "
                  "clip=\"true\" cursor=\"pointer\" display=\"normal\" locked=\"true\" zIndex=\"2\" grow=\"1\" minWidth=\"10\" "
                  "maxWidth=\"20\" minHeight=\"4\" maxHeight=\"8\" margin=\"4\" centerX=\"parent.50%\"/>"
                  "</window>"))
                  .Ok());
    CHECK(test, !CompileUi(Source("<window><group layout=\"horizontal\"/></window>")).Ok());
    CHECK(test, !CompileUi(Source("<window><group padding=\"8\"/></window>")).Ok());
    CHECK(test, !CompileUi(Source("<window><group gap=\"8\"/></window>")).Ok());
    CHECK(test, !CompileUi(Source("<window><group background=\"#000000\"/></window>")).Ok());
    CHECK(test, !CompileUi(Source("<window><group overflow=\"scroll\"/></window>")).Ok());

    Finish(test, "group", before);
}

void TestRectangle(TestContext& test) {
    const int before = test.failed;

    // A rectangle that never got a size is the default square, and it is a shape rather than
    // a box of content: it reports itself as a rectangle and holds nothing.
    std::unique_ptr<Element> plain = Layout(
        "<window width=\"400\" height=\"200\"><frame layout=\"none\" width=\"400\" height=\"200\">"
        "<rectangle id=\"box\"/>"
        "</frame></window>",
        400.0f, 200.0f);
    Element* box = plain != nullptr ? plain->FindById("box") : nullptr;
    CHECK(test, box != nullptr && box->Kind() == NodeKind::RECTANGLE);
    if (box != nullptr) {
        CHECK(test, std::fabs(box->Bounds().width - 100.0f) < 0.01f);
        CHECK(test, std::fabs(box->Bounds().height - 100.0f) < 0.01f);
        CHECK(test, box->Children().empty());
    }

    // A fixed, filled, or percentage length is the only thing that moves a rectangle off the
    // square, and min and max still cap it.
    std::unique_ptr<Element> sized = Layout(
        "<window width=\"400\" height=\"200\"><frame layout=\"none\" width=\"400\" height=\"200\">"
        "<rectangle id=\"wide\" x=\"10\" y=\"20\" width=\"240\" height=\"140\"/>"
        "<rectangle id=\"capped\" x=\"10\" y=\"160\" width=\"300\" maxWidth=\"120\" height=\"30\"/>"
        "<rectangle id=\"half\" x=\"10\" y=\"10\" width=\"50%\" height=\"20\"/>"
        "</frame></window>",
        400.0f, 200.0f);
    Element* wide = sized != nullptr ? sized->FindById("wide") : nullptr;
    Element* capped = sized != nullptr ? sized->FindById("capped") : nullptr;
    Element* half = sized != nullptr ? sized->FindById("half") : nullptr;
    CHECK(test, wide != nullptr && capped != nullptr && half != nullptr);
    if (wide != nullptr && capped != nullptr && half != nullptr) {
        CHECK(test, std::fabs(wide->Bounds().x - 10.0f) < 0.01f && std::fabs(wide->Bounds().y - 20.0f) < 0.01f);
        CHECK(test, std::fabs(wide->Bounds().width - 240.0f) < 0.01f && std::fabs(wide->Bounds().height - 140.0f) < 0.01f);
        CHECK(test, std::fabs(capped->Bounds().width - 120.0f) < 0.01f);
        CHECK(test, std::fabs(half->Bounds().width - 200.0f) < 0.01f);
    }

    // A uniform radius keeps the platform rectangle primitive. A rectangle whose corners
    // differ is drawn from a path, so each corner keeps the radius it was given.
    std::unique_ptr<Element> corners = Layout(
        "<window width=\"300\" height=\"200\"><frame layout=\"none\" width=\"300\" height=\"200\">"
        "<rectangle id=\"even\" x=\"10\" y=\"10\" width=\"120\" height=\"60\" radius=\"12\"/>"
        "<rectangle id=\"mixed\" x=\"10\" y=\"100\" width=\"120\" height=\"60\" radius=\"12\" radiusBottomLeft=\"0\"/>"
        "</frame></window>",
        300.0f, 200.0f);
    Element* even = corners != nullptr ? corners->FindById("even") : nullptr;
    Element* mixed = corners != nullptr ? corners->FindById("mixed") : nullptr;
    CHECK(test, even != nullptr && mixed != nullptr);
    if (even != nullptr && mixed != nullptr) {
        RectangleElement* evenShape = dynamic_cast<RectangleElement*>(even);
        RectangleElement* mixedShape = dynamic_cast<RectangleElement*>(mixed);
        CHECK(test, evenShape != nullptr && mixedShape != nullptr);
        if (evenShape != nullptr && mixedShape != nullptr) {
            CHECK(test, std::fabs(evenShape->CornerRadius(0) - 12.0f) < 0.01f);
            CHECK(test, std::fabs(evenShape->CornerRadius(2) - 12.0f) < 0.01f);
            CHECK(test, std::fabs(mixedShape->CornerRadius(0) - 12.0f) < 0.01f);
            CHECK(test, std::fabs(mixedShape->CornerRadius(3) - 0.0f) < 0.01f);
        }
        RecordingRenderer renderer;
        corners->Paint(renderer);
        CHECK(test, PaintRecordFor(renderer, even->Bounds()) != nullptr);
        CHECK(test, PaintRecordFor(renderer, mixed->Bounds()) != nullptr);
    }

    // A binding or an animation drives the rectangle's own values, not just the ones every
    // element carries.
    std::unique_ptr<Element> driven = Layout(
        "<window width=\"200\" height=\"200\"><frame layout=\"none\" width=\"200\" height=\"200\">"
        "<rectangle id=\"driven\" width=\"60\" height=\"60\" fill=\"#000000\" radius=\"0\"/>"
        "</frame></window>",
        200.0f, 200.0f);
    RectangleElement* drivenShape = driven != nullptr ? dynamic_cast<RectangleElement*>(driven->FindById("driven")) : nullptr;
    CHECK(test, drivenShape != nullptr);
    if (drivenShape != nullptr) {
        drivenShape->ApplyBound("fill", "#FF0000");
        CHECK(test, drivenShape->Fill().r > 0.9f && drivenShape->Fill().g < 0.1f);
        drivenShape->ApplyBound("stroke", "#00FF00");
        drivenShape->ApplyBound("strokeWidth", "4");
        CHECK(test, drivenShape->StrokeColor().g > 0.9f && drivenShape->StrokeColor().r < 0.1f);
        CHECK(test, std::fabs(drivenShape->StrokeWidth() - 4.0f) < 0.01f);
        drivenShape->ApplyBound("radius", "8");
        CHECK(test, std::fabs(drivenShape->CornerRadius(0) - 8.0f) < 0.01f);
        CHECK(test, std::fabs(drivenShape->CornerRadius(3) - 8.0f) < 0.01f);
        drivenShape->ApplyBound("rotation", "90");
        CHECK(test, std::fabs(drivenShape->Rotation() - 90.0f) < 0.01f);
    }

    // Fill, stroke, opacity, and rotation all reach the paint pass: the fill and the stroke
    // of the rectangle itself, and the rotation of the node it belongs to.
    std::unique_ptr<Element> painted = Layout(
        "<window width=\"300\" height=\"200\"><frame layout=\"none\" width=\"300\" height=\"200\">"
        "<rectangle id=\"solid\" x=\"10\" y=\"10\" width=\"80\" height=\"40\" fill=\"#112233\" opacity=\"0.5\"/>"
        "<rectangle id=\"outline\" x=\"10\" y=\"60\" width=\"60\" height=\"30\" stroke=\"#445566\" strokeWidth=\"3\"/>"
        "<rectangle id=\"turned\" x=\"150\" y=\"10\" width=\"40\" height=\"40\" background=\"#778899\" rotation=\"30\"/>"
        "</frame></window>",
        300.0f, 200.0f);
    Element* solid = painted != nullptr ? painted->FindById("solid") : nullptr;
    Element* outline = painted != nullptr ? painted->FindById("outline") : nullptr;
    Element* turned = painted != nullptr ? painted->FindById("turned") : nullptr;
    CHECK(test, solid != nullptr && outline != nullptr && turned != nullptr);
    if (solid != nullptr && outline != nullptr && turned != nullptr) {
        RecordingRenderer renderer;
        painted->Paint(renderer);
        const PaintRecord* solidPaint = PaintRecordFor(renderer, solid->Bounds());
        const PaintRecord* outlinePaint = PaintRecordFor(renderer, outline->Bounds());
        const PaintRecord* turnedPaint = PaintRecordFor(renderer, turned->Bounds());
        CHECK(test, solidPaint != nullptr && outlinePaint != nullptr && turnedPaint != nullptr);
        if (solidPaint != nullptr) {
            CHECK(test, std::fabs(solidPaint->opacity - 0.5f) < 0.001f);
            CHECK(test, solidPaint->rotationDepth == 0 && solidPaint->clipDepth == 0);
        }
        if (outlinePaint != nullptr) {
            CHECK(test, outlinePaint->rotationDepth == 0);
        }
        if (turnedPaint != nullptr) {
            CHECK(test, std::fabs(turnedPaint->rotation - 30.0f) < 0.01f);
            CHECK(test, turnedPaint->rotationDepth == 1);
        }
    }

    // A rounded corner is outside the shape, so a click there selects what is behind the
    // rectangle instead of the rectangle.
    std::unique_ptr<Element> hits = Layout(
        "<window width=\"200\" height=\"200\"><frame layout=\"none\" width=\"200\" height=\"200\">"
        "<rectangle id=\"pill\" x=\"40\" y=\"40\" width=\"100\" height=\"40\" radius=\"20\"/>"
        "<rectangle id=\"spin\" x=\"20\" y=\"120\" width=\"60\" height=\"60\" radius=\"30\" rotation=\"45\"/>"
        "</frame></window>",
        200.0f, 200.0f);
    Element* pill = hits != nullptr ? hits->FindById("pill") : nullptr;
    Element* spin = hits != nullptr ? hits->FindById("spin") : nullptr;
    CHECK(test, pill != nullptr && spin != nullptr);
    if (pill != nullptr && spin != nullptr) {
        const Rect pillBox = pill->Bounds();
        CHECK(test, pill->HitTest(Point{pillBox.x + pillBox.width * 0.5f, pillBox.y + pillBox.height * 0.5f}) == pill);
        CHECK(test, pill->HitTest(Point{pillBox.x + 2.0f, pillBox.y + 2.0f}) == nullptr);
        CHECK(test, pill->HitTest(Point{pillBox.x + 24.0f, pillBox.y + 2.0f}) == pill);
        CHECK(test, pill->HitTest(Point{pillBox.x - 4.0f, pillBox.y + pillBox.height * 0.5f}) == nullptr);
        // A turned rectangle keeps the box the renderer turned with it.
        const Rect spun = spin->Bounds();
        CHECK(test, spin->HitTest(Point{spun.x + 2.0f, spun.y + 2.0f}) == spin);
    }

    // Hidden and display="none" rectangles measure to nothing and paint nothing.
    std::unique_ptr<Element> hidden = Layout(
        "<window width=\"200\" height=\"200\"><frame layout=\"none\" width=\"200\" height=\"200\">"
        "<rectangle id=\"gone\" width=\"80\" height=\"80\" display=\"none\"/>"
        "<rectangle id=\"away\" x=\"90\" y=\"0\" width=\"80\" height=\"80\" visible=\"false\"/>"
        "</frame></window>",
        200.0f, 200.0f);
    Element* gone = hidden != nullptr ? hidden->FindById("gone") : nullptr;
    Element* away = hidden != nullptr ? hidden->FindById("away") : nullptr;
    CHECK(test, gone != nullptr && away != nullptr);
    if (gone != nullptr && away != nullptr) {
        CHECK(test, !gone->Visible() && !away->Visible());
        CHECK(test, gone->Measured().width == 0.0f && gone->Measured().height == 0.0f);
        CHECK(test, away->Measured().width == 0.0f && away->Measured().height == 0.0f);
        RecordingRenderer renderer;
        hidden->Paint(renderer);
        bool paintedSquare = false;
        for (const PaintRecord& record : renderer.records) {
            if (std::fabs(record.rect.width - 80.0f) < 0.01f && std::fabs(record.rect.height - 80.0f) < 0.01f) {
                paintedSquare = true;
            }
        }
        CHECK(test, !paintedSquare);
    }

    // The outline is stroked with the pattern the document gave it. A pattern is drawn only along
    // a path, so a patterned box takes the path even when a uniform radius would have kept the
    // platform primitive, and the box with no pattern still does not.
    std::unique_ptr<Element> patterns = Layout(
        "<window width=\"300\" height=\"200\"><frame layout=\"none\" width=\"300\" height=\"200\">"
        "<rectangle id=\"pat\" x=\"10\" y=\"10\" width=\"120\" height=\"60\" radius=\"12\" fill=\"#00000000\" stroke=\"#1D1D1F\" strokeWidth=\"2\" dash=\"6 4\"/>"
        "<rectangle id=\"gap\" x=\"10\" y=\"90\" width=\"120\" height=\"40\" fill=\"#00000000\" stroke=\"#1D1D1F\" strokeWidth=\"2\" dash=\"6\" dashGap=\"3\" dashOffset=\"2\"/>"
        "<rectangle id=\"plain\" x=\"160\" y=\"10\" width=\"120\" height=\"60\" radius=\"12\" fill=\"#00000000\" stroke=\"#1D1D1F\" strokeWidth=\"2\"/>"
        "</frame></window>",
        300.0f, 200.0f);
    Element* pat = patterns != nullptr ? patterns->FindById("pat") : nullptr;
    Element* gap = patterns != nullptr ? patterns->FindById("gap") : nullptr;
    Element* plainBox = patterns != nullptr ? patterns->FindById("plain") : nullptr;
    CHECK(test, pat != nullptr && gap != nullptr && plainBox != nullptr);
    if (pat != nullptr && gap != nullptr && plainBox != nullptr) {
        RecordingRenderer renderer;
        patterns->Paint(renderer);
        const PaintRecord* patPaint = PaintRecordFor(renderer, pat->Bounds());
        const PaintRecord* gapPaint = PaintRecordFor(renderer, gap->Bounds());
        const PaintRecord* plainPaint = PaintRecordFor(renderer, plainBox->Bounds());
        CHECK(test, patPaint != nullptr && patPaint->vector);
        if (patPaint != nullptr) {
            // The pattern is read as written, and the box is still the box: the same rounded
            // outline, broken into dashes rather than drawn another way.
            CHECK(test, static_cast<int>(patPaint->dashes.size()) == 2);
            CHECK(test, patPaint->dashes.size() == 2 && std::fabs(patPaint->dashes[0] - 6.0f) < 0.01f &&
                            std::fabs(patPaint->dashes[1] - 4.0f) < 0.01f);
            CHECK(test, std::fabs(patPaint->dashOffset) < 0.01f);
            CHECK(test, patPaint->path.find("A 12.000") != std::string::npos);
        }
        // One number is a dash length, its gap comes from dashGap, and the stroke starts wherever
        // dashOffset says it does.
        CHECK(test, gapPaint != nullptr && gapPaint->dashes.size() == 2 && std::fabs(gapPaint->dashes[0] - 6.0f) < 0.01f &&
                        std::fabs(gapPaint->dashes[1] - 3.0f) < 0.01f);
        CHECK(test, gapPaint != nullptr && std::fabs(gapPaint->dashOffset - 2.0f) < 0.01f);
        CHECK(test, plainPaint != nullptr && !plainPaint->vector && plainPaint->dashes.empty());
    }

    // The compiler rejects what a rectangle is not: no layout, no padding, no gap, no
    // background image, no overflow, no scrolling, and no children.
    CHECK(test, CompileUi(Source("<window><rectangle width=\"10\" fill=\"#FFFFFF\" radius=\"4\" dash=\"6 4\"/></window>")).Ok());
    CHECK(test, !CompileUi(Source("<window><rectangle layout=\"vertical\"/></window>")).Ok());
    CHECK(test, !CompileUi(Source("<window><rectangle padding=\"8\"/></window>")).Ok());
    CHECK(test, !CompileUi(Source("<window><rectangle gap=\"8\"/></window>")).Ok());
    CHECK(test, !CompileUi(Source("<window><rectangle overflow=\"scroll\"/></window>")).Ok());
    CHECK(test, !CompileUi(Source("<window><rectangle scrollbar=\"auto\"/></window>")).Ok());
    CHECK(test, !CompileUi(Source("<window><rectangle><frame/></rectangle></window>")).Ok());

    Finish(test, "rectangle", before);
}

void TestEllipse(TestContext& test) {
    const int before = test.failed;

    // An ellipse that never got a size is a circle, and like every shape it holds nothing.
    std::unique_ptr<Element> plain = Layout(
        "<window width=\"400\" height=\"200\"><frame layout=\"none\" width=\"400\" height=\"200\">"
        "<ellipse id=\"dot\"/>"
        "</frame></window>",
        400.0f, 200.0f);
    Element* dot = plain != nullptr ? plain->FindById("dot") : nullptr;
    CHECK(test, dot != nullptr && dot->Kind() == NodeKind::ELLIPSE);
    if (dot != nullptr) {
        CHECK(test, std::fabs(dot->Bounds().width - 100.0f) < 0.01f);
        CHECK(test, std::fabs(dot->Bounds().height - 100.0f) < 0.01f);
        CHECK(test, dot->Children().empty());
        CHECK(test, dynamic_cast<EllipseElement*>(dot) != nullptr && static_cast<EllipseElement*>(dot)->IsCircle());
    }

    // A circle takes one diameter instead of two sizes, and the two names are one component.
    std::unique_ptr<Element> round = Layout(
        "<window width=\"300\" height=\"200\"><frame layout=\"none\" width=\"300\" height=\"200\">"
        "<circle id=\"ring\" diameter=\"80\" fill=\"#000000\" />"
        "</frame></window>",
        300.0f, 200.0f);
    Element* ring = round != nullptr ? round->FindById("ring") : nullptr;
    CHECK(test, ring != nullptr && ring->Kind() == NodeKind::ELLIPSE);
    if (ring != nullptr) {
        CHECK(test, std::fabs(ring->Bounds().width - 80.0f) < 0.01f);
        CHECK(test, std::fabs(ring->Bounds().height - 80.0f) < 0.01f);
    }
    // The diameter fills in only the sizes the node did not give, so one number is round
    // while an explicit size still wins and lets the editor resize a circle.
    std::unique_ptr<Element> forced = Layout(
        "<window width=\"300\" height=\"200\"><frame layout=\"none\" width=\"300\" height=\"200\">"
        "<circle id=\"halfDia\" diameter=\"80\" width=\"120\"/>"
        "<circle id=\"squareDia\" diameter=\"80\" width=\"200\" height=\"40\"/>"
        "</frame></window>",
        300.0f, 200.0f);
    Element* halfDia = forced != nullptr ? forced->FindById("halfDia") : nullptr;
    Element* squareDia = forced != nullptr ? forced->FindById("squareDia") : nullptr;
    CHECK(test, halfDia != nullptr && squareDia != nullptr);
    if (halfDia != nullptr && squareDia != nullptr) {
        CHECK(test, std::fabs(halfDia->Bounds().width - 120.0f) < 0.01f);
        CHECK(test, std::fabs(halfDia->Bounds().height - 80.0f) < 0.01f);
        // A circle resized out of round is an ellipse, which is what a circle is made of.
        CHECK(test, std::fabs(squareDia->Bounds().width - 200.0f) < 0.01f);
        CHECK(test, std::fabs(squareDia->Bounds().height - 40.0f) < 0.01f);
    }

    // An ellipse is two radii, and a fixed, filled, or percentage length is the only thing
    // that moves it off the circle, with min and max still capping it.
    std::unique_ptr<Element> sized = Layout(
        "<window width=\"400\" height=\"200\"><frame layout=\"none\" width=\"400\" height=\"200\">"
        "<ellipse id=\"oval\" x=\"10\" y=\"20\" width=\"240\" height=\"140\"/>"
        "<ellipse id=\"capped\" x=\"10\" y=\"160\" width=\"300\" maxWidth=\"120\" height=\"30\"/>"
        "<ellipse id=\"half\" x=\"10\" y=\"10\" width=\"50%\" height=\"20\"/>"
        "</frame></window>",
        400.0f, 200.0f);
    Element* oval = sized != nullptr ? sized->FindById("oval") : nullptr;
    Element* cappedOval = sized != nullptr ? sized->FindById("capped") : nullptr;
    Element* halfOval = sized != nullptr ? sized->FindById("half") : nullptr;
    CHECK(test, oval != nullptr && cappedOval != nullptr && halfOval != nullptr);
    if (oval != nullptr && cappedOval != nullptr && halfOval != nullptr) {
        CHECK(test, std::fabs(oval->Bounds().x - 10.0f) < 0.01f && std::fabs(oval->Bounds().y - 20.0f) < 0.01f);
        CHECK(test, std::fabs(oval->Bounds().width - 240.0f) < 0.01f && std::fabs(oval->Bounds().height - 140.0f) < 0.01f);
        CHECK(test, std::fabs(cappedOval->Bounds().width - 120.0f) < 0.01f);
        CHECK(test, std::fabs(halfOval->Bounds().width - 200.0f) < 0.01f);
        // A non-square box is an ellipse rather than a circle, and a circle is not one.
        CHECK(test, dynamic_cast<EllipseElement*>(oval) != nullptr && !static_cast<EllipseElement*>(oval)->IsCircle());
    }

    // Fill, stroke, opacity, and rotation all reach the paint pass. A circle goes through the
    // platform primitive; an ellipse has to go through a path, because a rounded rectangle
    // with the same radii would come out a stadium.
    std::unique_ptr<Element> painted = Layout(
        "<window width=\"300\" height=\"200\"><frame layout=\"none\" width=\"300\" height=\"200\">"
        "<ellipse id=\"solid\" x=\"10\" y=\"10\" width=\"80\" height=\"80\" fill=\"#112233\" opacity=\"0.5\"/>"
        "<ellipse id=\"outline\" x=\"10\" y=\"60\" width=\"60\" height=\"60\" fill=\"#00000000\" stroke=\"#445566\" strokeWidth=\"3\"/>"
        "<ellipse id=\"stretched\" x=\"120\" y=\"10\" width=\"120\" height=\"40\" background=\"#778899\"/>"
        "<ellipse id=\"turned\" x=\"150\" y=\"80\" width=\"60\" height=\"30\" fill=\"#8899AA\" rotation=\"30\"/>"
        "</frame></window>",
        300.0f, 200.0f);
    Element* solidEllipse = painted != nullptr ? painted->FindById("solid") : nullptr;
    Element* outlineEllipse = painted != nullptr ? painted->FindById("outline") : nullptr;
    Element* stretchedEllipse = painted != nullptr ? painted->FindById("stretched") : nullptr;
    Element* turnedEllipse = painted != nullptr ? painted->FindById("turned") : nullptr;
    CHECK(test, solidEllipse != nullptr && outlineEllipse != nullptr && stretchedEllipse != nullptr && turnedEllipse != nullptr);
    if (solidEllipse != nullptr && outlineEllipse != nullptr && stretchedEllipse != nullptr && turnedEllipse != nullptr) {
        RecordingRenderer renderer;
        painted->Paint(renderer);
        const PaintRecord* solidPaint = PaintRecordFor(renderer, solidEllipse->Bounds());
        const PaintRecord* outlinePaint = PaintRecordFor(renderer, outlineEllipse->Bounds());
        const PaintRecord* stretchedPaint = PaintRecordFor(renderer, stretchedEllipse->Bounds());
        const PaintRecord* turnedPaint = PaintRecordFor(renderer, turnedEllipse->Bounds());
        CHECK(test, solidPaint != nullptr && outlinePaint != nullptr && stretchedPaint != nullptr && turnedPaint != nullptr);
        if (solidPaint != nullptr) {
            CHECK(test, std::fabs(solidPaint->opacity - 0.5f) < 0.001f);
            CHECK(test, solidPaint->rotationDepth == 0 && solidPaint->clipDepth == 0);
        }
        // Only the stretched box needs a path; the three square boxes are circles.
        CHECK(test, stretchedPaint != nullptr && stretchedPaint->vector);
        if (stretchedPaint != nullptr && stretchedPaint->vector) {
            // The curve is checked against its own definition: every sampled point of the
            // path sits on the two radii. A stadium would miss by a wide margin at 45 degrees.
            const Rect box = stretchedEllipse->Bounds();
            const float radiusX = box.width * 0.5f;
            const float radiusY = box.height * 0.5f;
            float worst = 0.0f;
            const std::vector<PathSample> samples = SamplePath(stretchedPaint->path);
            for (const PathSample& sample : samples) {
                const float normalizedX = (sample.x - radiusX) / radiusX;
                const float normalizedY = (sample.y - radiusY) / radiusY;
                worst = std::max(worst, std::fabs(normalizedX * normalizedX + normalizedY * normalizedY - 1.0f));
            }
            CHECK(test, samples.size() > 16);
            CHECK(test, worst < 0.02f);
        }
        // A circle stays on the platform primitive, so no path is drawn for it at all.
        const bool solidDrewVector = std::any_of(renderer.records.begin(), renderer.records.end(), [&](const PaintRecord& record) {
            return record.vector && std::fabs(record.rect.width - solidEllipse->Bounds().width) < 0.01f &&
                   std::fabs(record.rect.height - solidEllipse->Bounds().height) < 0.01f;
        });
        CHECK(test, !solidDrewVector);
        if (turnedPaint != nullptr) {
            CHECK(test, std::fabs(turnedPaint->rotation - 30.0f) < 0.01f);
            CHECK(test, turnedPaint->rotationDepth == 1);
        }
    }

    // The curve is the shape: a click in the corner of the box is outside the ellipse, and a
    // click on the far side of a turn is outside it too.
    std::unique_ptr<Element> hits = Layout(
        "<window width=\"200\" height=\"200\"><frame layout=\"none\" width=\"200\" height=\"200\">"
        "<ellipse id=\"wide\" x=\"20\" y=\"20\" width=\"160\" height=\"80\"/>"
        "<ellipse id=\"lean\" x=\"20\" y=\"120\" width=\"80\" height=\"40\" rotation=\"45\"/>"
        "</frame></window>",
        200.0f, 200.0f);
    Element* wideEllipse = hits != nullptr ? hits->FindById("wide") : nullptr;
    Element* leanEllipse = hits != nullptr ? hits->FindById("lean") : nullptr;
    CHECK(test, wideEllipse != nullptr && leanEllipse != nullptr);
    if (wideEllipse != nullptr && leanEllipse != nullptr) {
        const Rect wide = wideEllipse->Bounds();
        CHECK(test, wideEllipse->HitTest(Point{wide.x + wide.width * 0.5f, wide.y + wide.height * 0.5f}) == wideEllipse);
        CHECK(test, wideEllipse->HitTest(Point{wide.x + 2.0f, wide.y + 2.0f}) == nullptr);
        CHECK(test, wideEllipse->HitTest(Point{wide.x + wide.width * 0.5f, wide.y + 1.0f}) == wideEllipse);
        CHECK(test, wideEllipse->HitTest(Point{wide.x + 1.0f, wide.y + wide.height * 0.5f}) == wideEllipse);
        CHECK(test, wideEllipse->HitTest(Point{wide.x - 6.0f, wide.y + wide.height * 0.5f}) == nullptr);
        // The turned box is the renderer's bounds, so the same point lands inside it.
        const Rect lean = leanEllipse->Bounds();
        CHECK(test, leanEllipse->HitTest(Point{lean.x + lean.width * 0.5f, lean.y + lean.height * 0.5f}) == leanEllipse);
    }

    // A binding or an animation drives the ellipse's own values, not just the ones every
    // element carries.
    std::unique_ptr<Element> driven = Layout(
        "<window width=\"200\" height=\"200\"><frame layout=\"none\" width=\"200\" height=\"200\">"
        "<ellipse id=\"driven\" width=\"60\" height=\"60\" fill=\"#000000\"/>"
        "</frame></window>",
        200.0f, 200.0f);
    EllipseElement* drivenEllipse = driven != nullptr ? dynamic_cast<EllipseElement*>(driven->FindById("driven")) : nullptr;
    CHECK(test, drivenEllipse != nullptr);
    if (drivenEllipse != nullptr) {
        drivenEllipse->ApplyBound("fill", "#FF0000");
        CHECK(test, drivenEllipse->Fill().r > 0.9f && drivenEllipse->Fill().g < 0.1f);
        drivenEllipse->ApplyBound("stroke", "#00FF00");
        drivenEllipse->ApplyBound("strokeWidth", "4");
        CHECK(test, drivenEllipse->StrokeColor().g > 0.9f && drivenEllipse->StrokeColor().r < 0.1f);
        CHECK(test, std::fabs(drivenEllipse->StrokeWidth() - 4.0f) < 0.01f);
        drivenEllipse->ApplyBound("rotation", "90");
        CHECK(test, std::fabs(drivenEllipse->Rotation() - 90.0f) < 0.01f);
    }

    // Hidden and display="none" ellipses measure to nothing and paint nothing.
    std::unique_ptr<Element> hidden = Layout(
        "<window width=\"200\" height=\"200\"><frame layout=\"none\" width=\"200\" height=\"200\">"
        "<ellipse id=\"gone\" width=\"80\" height=\"80\" display=\"none\"/>"
        "<ellipse id=\"away\" x=\"90\" y=\"0\" width=\"80\" height=\"80\" visible=\"false\"/>"
        "</frame></window>",
        200.0f, 200.0f);
    Element* goneEllipse = hidden != nullptr ? hidden->FindById("gone") : nullptr;
    Element* awayEllipse = hidden != nullptr ? hidden->FindById("away") : nullptr;
    CHECK(test, goneEllipse != nullptr && awayEllipse != nullptr);
    if (goneEllipse != nullptr && awayEllipse != nullptr) {
        CHECK(test, !goneEllipse->Visible() && !awayEllipse->Visible());
        CHECK(test, goneEllipse->Measured().width == 0.0f && goneEllipse->Measured().height == 0.0f);
        CHECK(test, goneEllipse->HitTest(Point{goneEllipse->Bounds().x + 4.0f, goneEllipse->Bounds().y + 4.0f}) == nullptr);
    }

    // The outline is stroked with the pattern the document gave it. A pattern is drawn only along
    // a path, so a patterned circle takes the path the platform primitive would otherwise have
    // drawn, while the same circle with no pattern still goes through the primitive.
    std::unique_ptr<Element> patterns = Layout(
        "<window width=\"300\" height=\"200\"><frame layout=\"none\" width=\"300\" height=\"200\">"
        "<ellipse id=\"round\" x=\"10\" y=\"10\" width=\"80\" height=\"80\" fill=\"#00000000\" stroke=\"#1D1D1F\" strokeWidth=\"2\" dash=\"8\"/>"
        "<ellipse id=\"oval\" x=\"120\" y=\"10\" width=\"120\" height=\"40\" fill=\"#00000000\" stroke=\"#1D1D1F\" strokeWidth=\"2\" dash=\"8 4\" dashOffset=\"3\"/>"
        "<ellipse id=\"plain\" x=\"10\" y=\"110\" width=\"80\" height=\"80\" fill=\"#00000000\" stroke=\"#1D1D1F\" strokeWidth=\"2\"/>"
        "</frame></window>",
        300.0f, 200.0f);
    Element* patCircle = patterns != nullptr ? patterns->FindById("round") : nullptr;
    Element* patOval = patterns != nullptr ? patterns->FindById("oval") : nullptr;
    Element* plainEllipse = patterns != nullptr ? patterns->FindById("plain") : nullptr;
    CHECK(test, patCircle != nullptr && patOval != nullptr && plainEllipse != nullptr);
    if (patCircle != nullptr && patOval != nullptr && plainEllipse != nullptr) {
        RecordingRenderer renderer;
        patterns->Paint(renderer);
        const PaintRecord* patCirclePaint = PaintRecordFor(renderer, patCircle->Bounds());
        const PaintRecord* patOvalPaint = PaintRecordFor(renderer, patOval->Bounds());
        const PaintRecord* plainPaint = PaintRecordFor(renderer, plainEllipse->Bounds());
        CHECK(test, patCirclePaint != nullptr && patCirclePaint->vector);
        if (patCirclePaint != nullptr) {
            // One number is a dash length whose gap is the dash itself, and a patterned circle is
            // still the circle: the path is the ellipse's own outline.
            CHECK(test, patCirclePaint->dashes.size() == 2 && std::fabs(patCirclePaint->dashes[0] - 8.0f) < 0.01f &&
                            std::fabs(patCirclePaint->dashes[1] - 8.0f) < 0.01f);
            CHECK(test, patCirclePaint->path.find("A 40.000 40.000") != std::string::npos);
        }
        CHECK(test, patOvalPaint != nullptr && patOvalPaint->vector && patOvalPaint->dashes.size() == 2 &&
                        std::fabs(patOvalPaint->dashes[0] - 8.0f) < 0.01f && std::fabs(patOvalPaint->dashes[1] - 4.0f) < 0.01f);
        CHECK(test, patOvalPaint != nullptr && std::fabs(patOvalPaint->dashOffset - 3.0f) < 0.01f);
        CHECK(test, plainPaint != nullptr && !plainPaint->vector && plainPaint->dashes.empty());
    }

    // The compiler rejects what an ellipse is not: no layout, no padding, no radius (it has
    // no corners to round), no overflow, and no children.
    CHECK(test, CompileUi(Source("<window><ellipse width=\"10\" fill=\"#FFFFFF\" stroke=\"#000000\" strokeWidth=\"1\" dash=\"4 2\"/></window>")).Ok());
    CHECK(test, CompileUi(Source("<window><circle diameter=\"10\"/></window>")).Ok());
    CHECK(test, !CompileUi(Source("<window><ellipse layout=\"vertical\"/></window>")).Ok());
    CHECK(test, !CompileUi(Source("<window><ellipse padding=\"8\"/></window>")).Ok());
    CHECK(test, !CompileUi(Source("<window><ellipse gap=\"8\"/></window>")).Ok());
    CHECK(test, !CompileUi(Source("<window><ellipse radius=\"8\"/></window>")).Ok());
    CHECK(test, !CompileUi(Source("<window><ellipse overflow=\"scroll\"/></window>")).Ok());
    CHECK(test, !CompileUi(Source("<window><ellipse><frame/></ellipse></window>")).Ok());
    // A diameter is a circle's value, so an ellipse does not take one.
    CHECK(test, !CompileUi(Source("<window><ellipse diameter=\"10\"/></window>")).Ok());

    Finish(test, "ellipse", before);
}

// A polygon is a rectangle with a different outline, built on the rectangle instead of beside
// it. So most of what is checked here is the rectangle showing through the polygon: the box, the
// default size, the fill, the stroke, the radius, the turn, and the bindings.
void TestPolygon(TestContext& test) {
    const int before = test.failed;

    // The box and the defaults belong to the rectangle underneath, and a polygon is still a
    // rectangle to anything that asks for one.
    std::unique_ptr<Element> plain = Layout(
        "<window width=\"400\" height=\"200\"><frame layout=\"none\" width=\"400\" height=\"200\">"
        "<polygon id=\"ring\"/>"
        "</frame></window>",
        400.0f, 200.0f);
    Element* ring = plain != nullptr ? plain->FindById("ring") : nullptr;
    CHECK(test, ring != nullptr && ring->Kind() == NodeKind::POLYGON);
    if (ring != nullptr) {
        CHECK(test, std::fabs(ring->Bounds().width - 100.0f) < 0.01f);
        CHECK(test, std::fabs(ring->Bounds().height - 100.0f) < 0.01f);
        CHECK(test, ring->Children().empty());
        CHECK(test, dynamic_cast<RectangleElement*>(ring) != nullptr);
        PolygonElement* polygon = dynamic_cast<PolygonElement*>(ring);
        CHECK(test, polygon != nullptr);
        if (polygon != nullptr) {
            // Three sides is the fewest that can close a ring, and it is where a polygon starts.
            CHECK(test, polygon->Sides() == 3);
        }
    }

    // The number of sides comes from the document, stays inside what a ring can be, and the
    // values of the rectangle underneath still arrive on the polygon.
    std::unique_ptr<Element> counts = Layout(
        "<window width=\"400\" height=\"200\"><frame layout=\"none\" width=\"400\" height=\"200\">"
        "<polygon id=\"six\" x=\"0\" y=\"0\" width=\"80\" height=\"80\" sides=\"6\"/>"
        "<polygon id=\"sliver\" x=\"100\" y=\"0\" width=\"80\" height=\"80\" sides=\"1\"/>"
        "<polygon id=\"many\" x=\"200\" y=\"0\" width=\"80\" height=\"80\" sides=\"900\"/>"
        "</frame></window>",
        400.0f, 200.0f);
    PolygonElement* six = counts != nullptr ? dynamic_cast<PolygonElement*>(counts->FindById("six")) : nullptr;
    PolygonElement* sliver = counts != nullptr ? dynamic_cast<PolygonElement*>(counts->FindById("sliver")) : nullptr;
    PolygonElement* many = counts != nullptr ? dynamic_cast<PolygonElement*>(counts->FindById("many")) : nullptr;
    CHECK(test, six != nullptr && sliver != nullptr && many != nullptr);
    if (six != nullptr && sliver != nullptr && many != nullptr) {
        CHECK(test, six->Sides() == 6);
        CHECK(test, sliver->Sides() == 3);
        CHECK(test, many->Sides() == 64);
        six->ApplyBound("fill", "#FF0000");
        CHECK(test, six->Fill().r > 0.9f && six->Fill().g < 0.1f);
        six->ApplyBound("stroke", "#00FF00");
        six->ApplyBound("strokeWidth", "4");
        CHECK(test, six->StrokeColor().g > 0.9f && six->StrokeColor().r < 0.1f);
        CHECK(test, std::fabs(six->StrokeWidth() - 4.0f) < 0.01f);
        six->ApplyBound("radius", "8");
        CHECK(test, std::fabs(six->Radius() - 8.0f) < 0.01f);
        six->ApplyBound("rotation", "90");
        CHECK(test, std::fabs(six->Rotation() - 90.0f) < 0.01f);
        six->ApplyBound("sides", "5");
        CHECK(test, six->Sides() == 5);
    }

    // A polygon is stroked with the pattern the document gave it, because the pattern belongs to
    // the stroke the rectangle underneath already owns rather than to the outline the polygon
    // brings.
    std::unique_ptr<Element> patterned = Layout(
        "<window width=\"300\" height=\"200\"><frame layout=\"none\" width=\"300\" height=\"200\">"
        "<polygon id=\"ring\" x=\"10\" y=\"10\" width=\"80\" height=\"80\" sides=\"6\" fill=\"#00000000\" stroke=\"#1D1D1F\" strokeWidth=\"2\" dash=\"5 3\"/>"
        "</frame></window>",
        300.0f, 200.0f);
    Element* patternedRing = patterned != nullptr ? patterned->FindById("ring") : nullptr;
    CHECK(test, patternedRing != nullptr);
    if (patternedRing != nullptr) {
        RecordingRenderer renderer;
        patterned->Paint(renderer);
        const PaintRecord* patternedPaint = PaintRecordFor(renderer, patternedRing->Bounds());
        CHECK(test, patternedPaint != nullptr && patternedPaint->vector);
        CHECK(test, patternedPaint != nullptr && patternedPaint->dashes.size() == 2 &&
                        std::fabs(patternedPaint->dashes[0] - 5.0f) < 0.01f && std::fabs(patternedPaint->dashes[1] - 3.0f) < 0.01f);
    }

    // What is drawn is the ring: one corner per side, spread over the whole box. A shape left
    // inside the circle it was read from would sit away from the handles of the box it was
    // dragged out to, so a triangle reaches the middle of the top edge and both bottom corners,
    // and a four-sided ring stands on one corner and reaches the middle of every edge.
    std::unique_ptr<Element> drawn = Layout(
        "<window width=\"400\" height=\"300\"><frame layout=\"none\" width=\"400\" height=\"300\">"
        "<polygon id=\"triangle\" x=\"0\" y=\"0\" width=\"90\" height=\"60\" fill=\"#0E7AFF\"/>"
        "<polygon id=\"diamond\" x=\"120\" y=\"0\" width=\"80\" height=\"80\" sides=\"4\" fill=\"#0E7AFF\"/>"
        "<polygon id=\"hexagon\" x=\"240\" y=\"0\" width=\"100\" height=\"100\" sides=\"6\" fill=\"#0E7AFF\"/>"
        "</frame></window>",
        400.0f, 300.0f);
    Element* triangle = drawn != nullptr ? drawn->FindById("triangle") : nullptr;
    Element* diamond = drawn != nullptr ? drawn->FindById("diamond") : nullptr;
    Element* hexagon = drawn != nullptr ? drawn->FindById("hexagon") : nullptr;
    CHECK(test, triangle != nullptr && diamond != nullptr && hexagon != nullptr);
    if (triangle != nullptr && diamond != nullptr && hexagon != nullptr) {
        RecordingRenderer renderer;
        drawn->Paint(renderer);
        const PaintRecord* trianglePaint = PaintRecordFor(renderer, triangle->Bounds());
        const PaintRecord* diamondPaint = PaintRecordFor(renderer, diamond->Bounds());
        const PaintRecord* hexagonPaint = PaintRecordFor(renderer, hexagon->Bounds());
        CHECK(test, trianglePaint != nullptr && diamondPaint != nullptr && hexagonPaint != nullptr);
        if (trianglePaint != nullptr && diamondPaint != nullptr && hexagonPaint != nullptr) {
            CHECK(test, trianglePaint->vector && diamondPaint->vector && hexagonPaint->vector);
            // A triangle points up, so its corners are the middle of the top edge and both ends
            // of the bottom edge, which is the outline three sides are drawn with everywhere.
            const std::vector<PathSample> triangleRing = SampleContours(trianglePaint->path).front().points;
            CHECK(test, triangleRing.size() == 4);
            if (triangleRing.size() == 4) {
                CHECK(test, std::fabs(triangleRing[0].x - 45.0f) < 0.5f && std::fabs(triangleRing[0].y) < 0.5f);
                CHECK(test, std::fabs(triangleRing[1].x - 90.0f) < 0.5f && std::fabs(triangleRing[1].y - 60.0f) < 0.5f);
                CHECK(test, std::fabs(triangleRing[2].x) < 0.5f && std::fabs(triangleRing[2].y - 60.0f) < 0.5f);
            }
            const std::vector<PathSample> diamondRing = SampleContours(diamondPaint->path).front().points;
            CHECK(test, diamondRing.size() == 5);
            if (diamondRing.size() == 5) {
                CHECK(test, std::fabs(diamondRing[0].x - 40.0f) < 0.5f && std::fabs(diamondRing[0].y) < 0.5f);
                CHECK(test, std::fabs(diamondRing[1].x - 80.0f) < 0.5f && std::fabs(diamondRing[1].y - 40.0f) < 0.5f);
                CHECK(test, std::fabs(diamondRing[2].x - 40.0f) < 0.5f && std::fabs(diamondRing[2].y - 80.0f) < 0.5f);
                CHECK(test, std::fabs(diamondRing[3].x) < 0.5f && std::fabs(diamondRing[3].y - 40.0f) < 0.5f);
            }
            // However many sides there are, the ring reaches every edge of its box.
            const PaintRecord* boxes[3] = {trianglePaint, diamondPaint, hexagonPaint};
            for (const PaintRecord* record : boxes) {
                float minX = 100000.0f;
                float maxX = -100000.0f;
                float minY = 100000.0f;
                float maxY = -100000.0f;
                // The ring is held in a local of its own: the samples belong to a temporary that
                // a range over it would leave dangling.
                const std::vector<PathSample> spanRing = SampleContours(record->path).front().points;
                for (const PathSample& sample : spanRing) {
                    minX = std::min(minX, sample.x);
                    maxX = std::max(maxX, sample.x);
                    minY = std::min(minY, sample.y);
                    maxY = std::max(maxY, sample.y);
                }
                CHECK(test, std::fabs(minX) < 0.5f && std::fabs(minY) < 0.5f);
                CHECK(test, std::fabs(maxX - record->rect.width) < 0.5f);
                CHECK(test, std::fabs(maxY - record->rect.height) < 0.5f);
            }
        }
    }

    // A radius rounds the corners of the ring, and the corner is the only part of a ring that can
    // be cut away: the side still reaches the box, and the corner of the box no longer does.
    std::unique_ptr<Element> rounded = Layout(
        "<window width=\"300\" height=\"300\"><frame layout=\"none\" width=\"300\" height=\"300\">"
        "<polygon id=\"sharp\" x=\"0\" y=\"0\" width=\"100\" height=\"100\" sides=\"4\" fill=\"#0E7AFF\"/>"
        "<polygon id=\"soft\" x=\"120\" y=\"0\" width=\"100\" height=\"100\" sides=\"4\" radius=\"24\" fill=\"#0E7AFF\"/>"
        "</frame></window>",
        300.0f, 300.0f);
    Element* sharp = rounded != nullptr ? rounded->FindById("sharp") : nullptr;
    Element* soft = rounded != nullptr ? rounded->FindById("soft") : nullptr;
    CHECK(test, sharp != nullptr && soft != nullptr);
    if (sharp != nullptr && soft != nullptr) {
        RecordingRenderer renderer;
        rounded->Paint(renderer);
        const PaintRecord* softPaint = PaintRecordFor(renderer, soft->Bounds());
        CHECK(test, softPaint != nullptr);
        if (softPaint != nullptr) {
            // The cut corner is drawn as an arc rather than as a corner.
            CHECK(test, softPaint->path.find("A ") != std::string::npos);
            const std::vector<PathSample> roundedRing = SampleContours(softPaint->path).front().points;
            CHECK(test, roundedRing.size() > 5);
            // An arc that bulged the wrong way would leave the box it is drawn in.
            bool insideBox = true;
            for (const PathSample& sample : roundedRing) {
                if (sample.x < -0.5f || sample.y < -0.5f || sample.x > softPaint->rect.width + 0.5f || sample.y > softPaint->rect.height + 0.5f) {
                    insideBox = false;
                }
            }
            CHECK(test, insideBox);
            const Rect softBox = soft->Bounds();
            const Rect sharpBox = sharp->Bounds();
            // The top corner of the ring is exactly where a radius cuts, so the same point that
            // lands on a square corner misses the rounded one.
            CHECK(test, sharp->HitTest(Point{sharpBox.x + 50.0f, sharpBox.y}) == sharp);
            CHECK(test, soft->HitTest(Point{softBox.x + 50.0f, softBox.y}) == nullptr);
            CHECK(test, soft->HitTest(Point{softBox.x + 50.0f, softBox.y + 30.0f}) == soft);
        }
    }

    // The ring is hit where it is drawn: inside the outline, and not in the empty corner of the
    // box that a triangle leaves behind.
    std::unique_ptr<Element> hits = Layout(
        "<window width=\"200\" height=\"200\"><frame layout=\"none\" width=\"200\" height=\"200\">"
        "<polygon id=\"wedge\" x=\"0\" y=\"0\" width=\"100\" height=\"100\" sides=\"3\" fill=\"#0E7AFF\"/>"
        "<polygon id=\"spun\" x=\"0\" y=\"120\" width=\"60\" height=\"60\" sides=\"3\" rotation=\"45\" fill=\"#0E7AFF\"/>"
        "</frame></window>",
        200.0f, 200.0f);
    Element* wedge = hits != nullptr ? hits->FindById("wedge") : nullptr;
    Element* spun = hits != nullptr ? hits->FindById("spun") : nullptr;
    CHECK(test, wedge != nullptr && spun != nullptr);
    if (wedge != nullptr && spun != nullptr) {
        const Rect box = wedge->Bounds();
        CHECK(test, wedge->HitTest(Point{box.x + 50.0f, box.y + 80.0f}) == wedge);
        CHECK(test, wedge->HitTest(Point{box.x + 2.0f, box.y + 2.0f}) == nullptr);
        CHECK(test, wedge->HitTest(Point{box.x + box.width - 2.0f, box.y + 2.0f}) == nullptr);
        CHECK(test, wedge->HitTest(Point{box.x - 4.0f, box.y + 50.0f}) == nullptr);
        // A turned polygon keeps the box the renderer turned with it, as a turned rectangle does.
        const Rect spunBox = spun->Bounds();
        CHECK(test, spun->HitTest(Point{spunBox.x + 2.0f, spunBox.y + 2.0f}) == spun);
    }

    // Hidden and display="none" polygons measure to nothing, paint nothing, and are not hit.
    std::unique_ptr<Element> hidden = Layout(
        "<window width=\"200\" height=\"200\"><frame layout=\"none\" width=\"200\" height=\"200\">"
        "<polygon id=\"gone\" width=\"80\" height=\"80\" sides=\"5\" display=\"none\"/>"
        "<polygon id=\"away\" x=\"90\" y=\"0\" width=\"80\" height=\"80\" sides=\"5\" visible=\"false\"/>"
        "</frame></window>",
        200.0f, 200.0f);
    Element* gone = hidden != nullptr ? hidden->FindById("gone") : nullptr;
    Element* away = hidden != nullptr ? hidden->FindById("away") : nullptr;
    CHECK(test, gone != nullptr && away != nullptr);
    if (gone != nullptr && away != nullptr) {
        CHECK(test, !gone->Visible() && !away->Visible());
        CHECK(test, gone->Measured().width == 0.0f && gone->Measured().height == 0.0f);
        CHECK(test, away->Measured().width == 0.0f && away->Measured().height == 0.0f);
        CHECK(test, gone->HitTest(Point{gone->Bounds().x + 40.0f, gone->Bounds().y + 40.0f}) == nullptr);
        RecordingRenderer renderer;
        hidden->Paint(renderer);
        bool paintedPolygon = false;
        for (const PaintRecord& record : renderer.records) {
            if (std::fabs(record.rect.width - 80.0f) < 0.01f) {
                paintedPolygon = true;
            }
        }
        CHECK(test, !paintedPolygon);
    }

    // A polygon takes what a rectangle takes plus a number of sides. The four named corner radii
    // a rectangle offers belong to a box and not to a ring, so they are refused rather than
    // accepted and then quietly ignored, and so is everything else a shape has no use for.
    CHECK(test, CompileUi(Source("<window><polygon width=\"80\" height=\"60\" sides=\"5\" fill=\"#FFFFFF\" stroke=\"#000000\" strokeWidth=\"2\" radius=\"6\"/></window>")).Ok());
    CHECK(test, !CompileUi(Source("<window><polygon sides=\"4\" radiusTopLeft=\"8\"/></window>")).Ok());
    CHECK(test, !CompileUi(Source("<window><polygon layout=\"vertical\"/></window>")).Ok());
    CHECK(test, !CompileUi(Source("<window><polygon padding=\"8\"/></window>")).Ok());
    CHECK(test, !CompileUi(Source("<window><polygon gap=\"8\"/></window>")).Ok());
    CHECK(test, !CompileUi(Source("<window><polygon overflow=\"scroll\"/></window>")).Ok());
    CHECK(test, !CompileUi(Source("<window><polygon><frame/></polygon></window>")).Ok());

    Finish(test, "polygon", before);
}

void TestStar(TestContext& test) {
    const int before = test.failed;

    // A star is a polygon whose ring holds two radii instead of one, so it is a polygon and a
    // rectangle to anything that asks, and it measures and holds nothing the way those do.
    std::unique_ptr<Element> plain = Layout(
        "<window width=\"400\" height=\"200\"><frame layout=\"none\" width=\"400\" height=\"200\">"
        "<star id=\"mark\"/>"
        "</frame></window>",
        400.0f, 200.0f);
    Element* mark = plain != nullptr ? plain->FindById("mark") : nullptr;
    CHECK(test, mark != nullptr && mark->Kind() == NodeKind::STAR);
    if (mark != nullptr) {
        CHECK(test, std::fabs(mark->Bounds().width - 100.0f) < 0.01f);
        CHECK(test, std::fabs(mark->Bounds().height - 100.0f) < 0.01f);
        CHECK(test, mark->Children().empty());
        // Nothing was written twice: the star is the polygon, and through it the rectangle.
        CHECK(test, dynamic_cast<PolygonElement*>(mark) != nullptr);
        CHECK(test, dynamic_cast<RectangleElement*>(mark) != nullptr);
        StarElement* star = dynamic_cast<StarElement*>(mark);
        CHECK(test, star != nullptr);
        if (star != nullptr) {
            CHECK(test, star->Points() == 5);
            CHECK(test, std::fabs(star->InnerRadius() - 0.382f) < 0.001f);
            // The values of the shapes underneath still arrive on the star.
            star->ApplyBound("fill", "#FF0000");
            CHECK(test, star->Fill().r > 0.9f && star->Fill().g < 0.1f);
            star->ApplyBound("stroke", "#00FF00");
            star->ApplyBound("strokeWidth", "3");
            CHECK(test, star->StrokeColor().g > 0.9f && std::fabs(star->StrokeWidth() - 3.0f) < 0.01f);
            star->ApplyBound("radius", "6");
            CHECK(test, std::fabs(star->Radius() - 6.0f) < 0.01f);
            star->ApplyBound("rotation", "45");
            CHECK(test, std::fabs(star->Rotation() - 45.0f) < 0.01f);
            // The two values the star owns, each kept inside what a ring can be.
            star->ApplyBound("points", "7");
            CHECK(test, star->Points() == 7);
            star->ApplyBound("innerRadius", "0.5");
            CHECK(test, std::fabs(star->InnerRadius() - 0.5f) < 0.001f);
            star->ApplyBound("points", "1");
            CHECK(test, star->Points() == 3);
            star->ApplyBound("points", "900");
            CHECK(test, star->Points() == 64);
            star->ApplyBound("innerRadius", "-4");
            CHECK(test, star->InnerRadius() == 0.0f);
            star->ApplyBound("innerRadius", "9");
            CHECK(test, star->InnerRadius() == 1.0f);
        }
    }

    // A star is stroked with the pattern the document gave it, the same way the polygon and the
    // rectangle underneath it are.
    std::unique_ptr<Element> patterns = Layout(
        "<window width=\"300\" height=\"200\"><frame layout=\"none\" width=\"300\" height=\"200\">"
        "<star id=\"ring\" x=\"10\" y=\"10\" width=\"80\" height=\"80\" fill=\"#00000000\" stroke=\"#1D1D1F\" strokeWidth=\"2\" dash=\"4\"/>"
        "</frame></window>",
        300.0f, 200.0f);
    Element* patternStar = patterns != nullptr ? patterns->FindById("ring") : nullptr;
    CHECK(test, patternStar != nullptr);
    if (patternStar != nullptr) {
        RecordingRenderer renderer;
        patterns->Paint(renderer);
        const PaintRecord* patternPaint = PaintRecordFor(renderer, patternStar->Bounds());
        CHECK(test, patternPaint != nullptr && patternPaint->vector);
        // One number is a dash length whose gap is the dash itself, so the two are the same.
        CHECK(test, patternPaint != nullptr && patternPaint->dashes.size() == 2 &&
                        std::fabs(patternPaint->dashes[0] - 4.0f) < 0.01f && std::fabs(patternPaint->dashes[1] - 4.0f) < 0.01f);
    }

    // A star is a ring of two radii, so it holds one corner more than twice its points, every
    // point of the ring reaches the box the handles describe, and the ring closes on itself.
    std::unique_ptr<Element> drawn = Layout(
        "<window width=\"400\" height=\"300\"><frame layout=\"none\" width=\"400\" height=\"300\">"
        "<star id=\"five\" x=\"0\" y=\"0\" width=\"100\" height=\"100\" fill=\"#0E7AFF\"/>"
        "<star id=\"six\" x=\"120\" y=\"0\" width=\"100\" height=\"100\" points=\"6\" fill=\"#0E7AFF\"/>"
        "</frame></window>",
        400.0f, 300.0f);
    Element* five = drawn != nullptr ? drawn->FindById("five") : nullptr;
    Element* six = drawn != nullptr ? drawn->FindById("six") : nullptr;
    CHECK(test, five != nullptr && six != nullptr);
    if (five != nullptr && six != nullptr) {
        RecordingRenderer renderer;
        drawn->Paint(renderer);
        const PaintRecord* fivePaint = PaintRecordFor(renderer, five->Bounds());
        const PaintRecord* sixPaint = PaintRecordFor(renderer, six->Bounds());
        CHECK(test, fivePaint != nullptr && sixPaint != nullptr);
        if (fivePaint != nullptr && sixPaint != nullptr) {
            CHECK(test, fivePaint->vector && sixPaint->vector);
            // Five points make ten corners, and the path closes back on the first of them.
            const std::vector<PathSample> fiveRing = SampleContours(fivePaint->path).front().points;
            CHECK(test, fiveRing.size() == 11);
            if (fiveRing.size() == 11) {
                // The first corner of the ring is one of the outer points, standing on the
                // middle of the top edge.
                CHECK(test, std::fabs(fiveRing[0].x - 50.0f) < 0.5f && std::fabs(fiveRing[0].y) < 0.5f);
            }
            // However many points there are, the ring reaches every edge of its box.
            const PaintRecord* boxes[2] = {fivePaint, sixPaint};
            for (const PaintRecord* record : boxes) {
                const std::vector<PathSample> spanRing = SampleContours(record->path).front().points;
                float minX = 100000.0f;
                float maxX = -100000.0f;
                float minY = 100000.0f;
                float maxY = -100000.0f;
                for (const PathSample& sample : spanRing) {
                    minX = std::min(minX, sample.x);
                    maxX = std::max(maxX, sample.x);
                    minY = std::min(minY, sample.y);
                    maxY = std::max(maxY, sample.y);
                }
                CHECK(test, std::fabs(minX) < 0.5f && std::fabs(minY) < 0.5f);
                CHECK(test, std::fabs(maxX - record->rect.width) < 0.5f);
                CHECK(test, std::fabs(maxY - record->rect.height) < 0.5f);
            }
        }
    }

    // The inner radius is how far the valleys of the ring are pulled toward its middle, so a
    // smaller one makes a spikier star and a larger one a flatter one.
    std::unique_ptr<Element> depth = Layout(
        "<window width=\"300\" height=\"200\"><frame layout=\"none\" width=\"300\" height=\"200\">"
        "<star id=\"deep\" x=\"0\" y=\"0\" width=\"100\" height=\"100\" innerRadius=\"0.2\" fill=\"#0E7AFF\"/>"
        "<star id=\"flat\" x=\"120\" y=\"0\" width=\"100\" height=\"100\" innerRadius=\"0.9\" fill=\"#0E7AFF\"/>"
        "</frame></window>",
        300.0f, 200.0f);
    Element* deep = depth != nullptr ? depth->FindById("deep") : nullptr;
    Element* flat = depth != nullptr ? depth->FindById("flat") : nullptr;
    CHECK(test, deep != nullptr && flat != nullptr);
    if (deep != nullptr && flat != nullptr) {
        RecordingRenderer renderer;
        depth->Paint(renderer);
        const PaintRecord* deepPaint = PaintRecordFor(renderer, deep->Bounds());
        const PaintRecord* flatPaint = PaintRecordFor(renderer, flat->Bounds());
        CHECK(test, deepPaint != nullptr && flatPaint != nullptr);
        if (deepPaint != nullptr && flatPaint != nullptr) {
            CHECK(test, deepPaint->path != flatPaint->path);
        }
    }

    // A radius rounds every corner of the ring, the inward valleys included, and a corner turned
    // the wrong way would leave the box it is drawn in.
    std::unique_ptr<Element> rounded = Layout(
        "<window width=\"200\" height=\"200\"><frame layout=\"none\" width=\"200\" height=\"200\">"
        "<star id=\"soft\" x=\"0\" y=\"0\" width=\"100\" height=\"100\" radius=\"10\" fill=\"#0E7AFF\"/>"
        "</frame></window>",
        200.0f, 200.0f);
    Element* soft = rounded != nullptr ? rounded->FindById("soft") : nullptr;
    CHECK(test, soft != nullptr);
    if (soft != nullptr) {
        RecordingRenderer renderer;
        rounded->Paint(renderer);
        const PaintRecord* softPaint = PaintRecordFor(renderer, soft->Bounds());
        CHECK(test, softPaint != nullptr);
        if (softPaint != nullptr) {
            CHECK(test, softPaint->path.find("A ") != std::string::npos);
            const std::vector<PathSample> roundedRing = SampleContours(softPaint->path).front().points;
            CHECK(test, roundedRing.size() > 11);
            bool insideBox = true;
            for (const PathSample& sample : roundedRing) {
                if (sample.x < -0.5f || sample.y < -0.5f || sample.x > softPaint->rect.width + 0.5f || sample.y > softPaint->rect.height + 0.5f) {
                    insideBox = false;
                }
            }
            CHECK(test, insideBox);
        }
    }

    // The star is hit where it is drawn: the middle of the ring is inside, and the empty part of
    // the box between two points is not.
    std::unique_ptr<Element> hits = Layout(
        "<window width=\"200\" height=\"200\"><frame layout=\"none\" width=\"200\" height=\"200\">"
        "<star id=\"mark\" x=\"0\" y=\"0\" width=\"100\" height=\"100\" fill=\"#0E7AFF\"/>"
        "<star id=\"spun\" x=\"0\" y=\"120\" width=\"60\" height=\"60\" rotation=\"45\" fill=\"#0E7AFF\"/>"
        "</frame></window>",
        200.0f, 200.0f);
    Element* hitStar = hits != nullptr ? hits->FindById("mark") : nullptr;
    Element* spun = hits != nullptr ? hits->FindById("spun") : nullptr;
    CHECK(test, hitStar != nullptr && spun != nullptr);
    if (hitStar != nullptr && spun != nullptr) {
        const Rect hitBox = hitStar->Bounds();
        CHECK(test, hitStar->HitTest(Point{hitBox.x + 50.0f, hitBox.y + 50.0f}) == hitStar);
        CHECK(test, hitStar->HitTest(Point{hitBox.x + 95.0f, hitBox.y + 5.0f}) == nullptr);
        const Rect spunBox = spun->Bounds();
        CHECK(test, spun->HitTest(Point{spunBox.x + 2.0f, spunBox.y + 2.0f}) == spun);
    }

    // Hidden and display="none" stars measure to nothing, paint nothing, and are not hit.
    std::unique_ptr<Element> hidden = Layout(
        "<window width=\"200\" height=\"200\"><frame layout=\"none\" width=\"200\" height=\"200\">"
        "<star id=\"gone\" width=\"80\" height=\"80\" points=\"5\" display=\"none\"/>"
        "<star id=\"away\" x=\"90\" y=\"0\" width=\"80\" height=\"80\" points=\"5\" visible=\"false\"/>"
        "</frame></window>",
        200.0f, 200.0f);
    Element* gone = hidden != nullptr ? hidden->FindById("gone") : nullptr;
    Element* away = hidden != nullptr ? hidden->FindById("away") : nullptr;
    CHECK(test, gone != nullptr && away != nullptr);
    if (gone != nullptr && away != nullptr) {
        CHECK(test, !gone->Visible() && !away->Visible());
        CHECK(test, gone->Measured().width == 0.0f && gone->Measured().height == 0.0f);
        CHECK(test, away->Measured().width == 0.0f && away->Measured().height == 0.0f);
        CHECK(test, gone->HitTest(Point{gone->Bounds().x + 40.0f, gone->Bounds().y + 40.0f}) == nullptr);
        RecordingRenderer renderer;
        hidden->Paint(renderer);
        bool paintedStar = false;
        for (const PaintRecord& record : renderer.records) {
            if (std::fabs(record.rect.width - 80.0f) < 0.01f) {
                paintedStar = true;
            }
        }
        CHECK(test, !paintedStar);
    }

    // A star takes what a polygon takes except a number of sides, which the two radii of the ring
    // replace, plus the number of points and the inner radius. Everything a shape has no use for
    // is refused rather than accepted and then quietly ignored.
    CHECK(test, CompileUi(Source("<window><star width=\"80\" height=\"60\" points=\"6\" innerRadius=\"0.4\" fill=\"#FFFFFF\" stroke=\"#000000\" strokeWidth=\"2\" radius=\"6\"/></window>")).Ok());
    CHECK(test, !CompileUi(Source("<window><star sides=\"5\"/></window>")).Ok());
    CHECK(test, !CompileUi(Source("<window><star points=\"4\" radiusTopLeft=\"8\"/></window>")).Ok());
    CHECK(test, !CompileUi(Source("<window><star layout=\"vertical\"/></window>")).Ok());
    CHECK(test, !CompileUi(Source("<window><star padding=\"8\"/></window>")).Ok());
    CHECK(test, !CompileUi(Source("<window><star gap=\"8\"/></window>")).Ok());
    CHECK(test, !CompileUi(Source("<window><star overflow=\"scroll\"/></window>")).Ok());
    CHECK(test, !CompileUi(Source("<window><star><frame/></star></window>")).Ok());

    Finish(test, "star", before);
}

void TestVector(TestContext& test) {
    const int before = test.failed;

    // A vector is a shape, so it is a rectangle to anything that asks and it measures, turns and
    // is hit the way one is. What it brings is the geometry it draws.
    std::unique_ptr<Element> plain = Layout(
        "<window width=\"400\" height=\"200\"><frame layout=\"none\" width=\"400\" height=\"200\">"
        "<vector id=\"mark\"/>"
        "</frame></window>",
        400.0f, 200.0f);
    Element* mark = plain != nullptr ? plain->FindById("mark") : nullptr;
    CHECK(test, mark != nullptr && mark->Kind() == NodeKind::VECTOR);
    if (mark != nullptr) {
        CHECK(test, std::fabs(mark->Bounds().width - 100.0f) < 0.01f);
        CHECK(test, std::fabs(mark->Bounds().height - 100.0f) < 0.01f);
        CHECK(test, mark->Children().empty());
        // Nothing was written twice: the vector is the rectangle underneath.
        CHECK(test, dynamic_cast<RectangleElement*>(mark) != nullptr);
        VectorElement* vector = dynamic_cast<VectorElement*>(mark);
        CHECK(test, vector != nullptr);
        if (vector != nullptr) {
            // A vector with neither a path nor a named shape draws no art of its own, so a bare
            // element is not the blue box a rectangle would be.
            CHECK(test, !vector->HasOwnGeometry());
            CHECK(test, !vector->HasViewBox());
            RecordingRenderer renderer;
            plain->Paint(renderer);
            CHECK(test, PaintRecordFor(renderer, mark->Bounds()) == nullptr);
        }
    }

    // A named shape draws the outline that shape already has: the ellipse the ellipse component
    // draws, and the box the rectangle underneath draws.
    std::unique_ptr<Element> shapes = Layout(
        "<window width=\"300\" height=\"200\"><frame layout=\"none\" width=\"300\" height=\"200\">"
        "<vector id=\"oval\" x=\"0\" y=\"0\" width=\"120\" height=\"80\" shape=\"ellipse\" fill=\"#0E7AFF\"/>"
        "<vector id=\"box\" x=\"140\" y=\"0\" width=\"100\" height=\"100\" shape=\"rect\" fill=\"#30D158\" radius=\"12\"/>"
        "</frame></window>",
        300.0f, 200.0f);
    Element* oval = shapes != nullptr ? shapes->FindById("oval") : nullptr;
    Element* box = shapes != nullptr ? shapes->FindById("box") : nullptr;
    CHECK(test, oval != nullptr && box != nullptr);
    if (oval != nullptr && box != nullptr) {
        VectorElement* ovalVector = dynamic_cast<VectorElement*>(oval);
        VectorElement* boxVector = dynamic_cast<VectorElement*>(box);
        CHECK(test, ovalVector != nullptr && boxVector != nullptr);
        if (ovalVector != nullptr && boxVector != nullptr) {
            CHECK(test, ovalVector->HasOwnGeometry() && ovalVector->DrawsEllipse() && !ovalVector->DrawsBox());
            CHECK(test, boxVector->HasOwnGeometry() && boxVector->DrawsBox() && !boxVector->DrawsEllipse());
            RecordingRenderer renderer;
            shapes->Paint(renderer);
            const PaintRecord* ovalPaint = PaintRecordFor(renderer, oval->Bounds());
            CHECK(test, ovalPaint != nullptr);
            if (ovalPaint != nullptr) {
                // A box wider than it is tall is a stadium unless it is drawn as a path, so an
                // ellipse is always a path.
                CHECK(test, ovalPaint->vector);
                CHECK(test, ovalPaint->path.find("A ") != std::string::npos);
                const std::vector<PathSample> ovalRing = SampleContours(ovalPaint->path).front().points;
                bool insideBox = true;
                for (const PathSample& sample : ovalRing) {
                    if (sample.x < -0.5f || sample.y < -0.5f || sample.x > ovalPaint->rect.width + 0.5f || sample.y > ovalPaint->rect.height + 0.5f) {
                        insideBox = false;
                    }
                }
                CHECK(test, insideBox);
            }
            // A box is the primitive the platform already draws, so it does not go through a path.
            const PaintRecord* boxPaint = PaintRecordFor(renderer, box->Bounds());
            CHECK(test, boxPaint != nullptr && !boxPaint->vector);
        }
    }

    // A path of its own is written in the view box and drawn in the box, so the same path at two
    // sizes keeps the shape it was written with.
    std::unique_ptr<Element> drawn = Layout(
        "<window width=\"400\" height=\"200\"><frame layout=\"none\" width=\"400\" height=\"200\">"
        "<vector id=\"small\" x=\"0\" y=\"0\" width=\"24\" height=\"24\" viewBox=\"0 0 24 24\" d=\"M0 0 L24 0 L24 24 Z\" fill=\"#0E7AFF\"/>"
        "<vector id=\"large\" x=\"40\" y=\"0\" width=\"72\" height=\"72\" viewBox=\"0 0 24 24\" d=\"M0 0 L24 0 L24 24 Z\" fill=\"#0E7AFF\"/>"
        "</frame></window>",
        400.0f, 200.0f);
    Element* smallShape = drawn != nullptr ? drawn->FindById("small") : nullptr;
    Element* largeShape = drawn != nullptr ? drawn->FindById("large") : nullptr;
    CHECK(test, smallShape != nullptr && largeShape != nullptr);
    if (smallShape != nullptr && largeShape != nullptr) {
        VectorElement* smallVector = dynamic_cast<VectorElement*>(smallShape);
        VectorElement* largeVector = dynamic_cast<VectorElement*>(largeShape);
        CHECK(test, smallVector != nullptr && largeVector != nullptr);
        if (smallVector != nullptr && largeVector != nullptr) {
            CHECK(test, smallVector->HasOwnGeometry() && smallVector->HasViewBox());
            CHECK(test, std::fabs(smallVector->ViewBox().width - 24.0f) < 0.01f);
            CHECK(test, std::fabs(smallVector->ViewBox().height - 24.0f) < 0.01f);
            CHECK(test, smallVector->PathData() == "M0 0 L24 0 L24 24 Z");
            RecordingRenderer renderer;
            drawn->Paint(renderer);
            const PaintRecord* smallPaint = PaintRecordFor(renderer, smallShape->Bounds());
            const PaintRecord* largePaint = PaintRecordFor(renderer, largeShape->Bounds());
            CHECK(test, smallPaint != nullptr && largePaint != nullptr);
            if (smallPaint != nullptr && largePaint != nullptr) {
                // The path travels to the renderer exactly as it was written, with the view box
                // that says how to lay it into the box.
                CHECK(test, smallPaint->vector && largePaint->vector);
                CHECK(test, smallPaint->path == smallVector->PathData() && largePaint->path == smallPaint->path);
                CHECK(test, std::fabs(smallPaint->rect.width - 24.0f) < 0.01f);
                CHECK(test, std::fabs(largePaint->rect.width - 72.0f) < 0.01f);
            }
        }
    }

    // The values that shape the geometry arrive on the node, and the rectangle underneath still
    // answers for the box, the fill, the stroke, the radius and the turn.
    std::unique_ptr<Element> props = Layout(
        "<window width=\"200\" height=\"200\"><frame layout=\"none\" width=\"200\" height=\"200\">"
        "<vector id=\"mark\" x=\"0\" y=\"0\" width=\"50\" height=\"50\"/>"
        "</frame></window>",
        200.0f, 200.0f);
    Element* target = props != nullptr ? props->FindById("mark") : nullptr;
    VectorElement* vector = target != nullptr ? dynamic_cast<VectorElement*>(target) : nullptr;
    CHECK(test, vector != nullptr);
    if (vector != nullptr) {
        vector->ApplyBound("shape", " ellipse ");
        CHECK(test, vector->DrawsEllipse());
        vector->ApplyBound("d", "M0 0 L8 0 L8 8 Z");
        CHECK(test, vector->PathData() == "M0 0 L8 0 L8 8 Z");
        vector->ApplyBound("viewBox", "0 0 8 8");
        CHECK(test, vector->HasViewBox() && std::fabs(vector->ViewBox().width - 8.0f) < 0.01f);
        // A view box with no area in it says nothing about where the geometry is written, and one
        // that is not four numbers says nothing either, so both leave the box that was already set.
        vector->ApplyBound("viewBox", "0 0 0 8");
        CHECK(test, std::fabs(vector->ViewBox().width - 8.0f) < 0.01f);
        vector->ApplyBound("viewBox", "0 0 8 nope");
        CHECK(test, std::fabs(vector->ViewBox().width - 8.0f) < 0.01f);
        vector->ApplyBound("fill", "#FF0000");
        CHECK(test, vector->Fill().r > 0.9f && vector->Fill().g < 0.1f);
        vector->ApplyBound("stroke", "#00FF00");
        vector->ApplyBound("strokeWidth", "3");
        CHECK(test, vector->StrokeColor().g > 0.9f && std::fabs(vector->StrokeWidth() - 3.0f) < 0.01f);
        vector->ApplyBound("radius", "6");
        CHECK(test, std::fabs(vector->Radius() - 6.0f) < 0.01f);
        vector->ApplyBound("rotation", "45");
        CHECK(test, std::fabs(vector->Rotation() - 45.0f) < 0.01f);
    }

    // The rule the fill follows and the way the ends and joins are drawn reach the path that is
    // handed to the renderer, and each one can arrive on its own.
    std::unique_ptr<Element> styled = Layout(
        "<window width=\"200\" height=\"200\"><frame layout=\"none\" width=\"200\" height=\"200\">"
        "<vector id=\"cut\" x=\"0\" y=\"0\" width=\"80\" height=\"80\" d=\"M0 0 L80 0 L80 80 Z\" fill=\"#0E7AFF\"/>"
        "</frame></window>",
        200.0f, 200.0f);
    Element* cut = styled != nullptr ? styled->FindById("cut") : nullptr;
    VectorElement* cutVector = cut != nullptr ? dynamic_cast<VectorElement*>(cut) : nullptr;
    CHECK(test, cutVector != nullptr);
    if (cutVector != nullptr) {
        cutVector->ApplyBound("fillRule", "evenodd");
        cutVector->ApplyBound("lineCap", "round");
        cutVector->ApplyBound("lineJoin", "bevel");
        RecordingRenderer renderer;
        styled->Paint(renderer);
        const PaintRecord* cutPaint = PaintRecordFor(renderer, cut->Bounds());
        CHECK(test, cutPaint != nullptr);
        if (cutPaint != nullptr) {
            CHECK(test, cutPaint->evenOdd);
            CHECK(test, cutPaint->lineCap == 'r');
            CHECK(test, cutPaint->lineJoin == 'b');
        }
    }

    // A path of its own is stroked with the pattern the document gave it, the way a path node and
    // a line are, so a vector can draw a dashed outline as well as a solid one. The geometry the
    // stroke follows does not change: it is the same path, broken into dashes.
    std::unique_ptr<Element> patterned = Layout(
        "<window width=\"200\" height=\"200\"><frame layout=\"none\" width=\"200\" height=\"200\">"
        "<vector id=\"dashes\" x=\"0\" y=\"0\" width=\"60\" height=\"60\" viewBox=\"0 0 24 24\" d=\"M0 0 L24 0 L24 24 Z\" fill=\"#00000000\" "
        "stroke=\"#1D1D1F\" strokeWidth=\"2\" dash=\"4\" dashGap=\"2\" dashOffset=\"1\"/>"
        "</frame></window>",
        200.0f, 200.0f);
    Element* patternedVector = patterned != nullptr ? patterned->FindById("dashes") : nullptr;
    CHECK(test, patternedVector != nullptr);
    if (patternedVector != nullptr) {
        RecordingRenderer renderer;
        patterned->Paint(renderer);
        const PaintRecord* patternedPaint = PaintRecordFor(renderer, patternedVector->Bounds());
        CHECK(test, patternedPaint != nullptr && patternedPaint->vector);
        CHECK(test, patternedPaint != nullptr && patternedPaint->path == "M0 0 L24 0 L24 24 Z");
        CHECK(test, patternedPaint != nullptr && patternedPaint->dashes.size() == 2 &&
                        std::fabs(patternedPaint->dashes[0] - 4.0f) < 0.01f && std::fabs(patternedPaint->dashes[1] - 2.0f) < 0.01f);
        CHECK(test, patternedPaint != nullptr && std::fabs(patternedPaint->dashOffset - 1.0f) < 0.01f);
    }

    // The art a vector holds is scaled with the box: the same child written at 8 x 8 is drawn at
    // 24 x 24 in a 24 x 24 vector and at 72 x 72 in a 72 x 72 one, so one icon is drawn at any
    // size instead of the art sitting at one size inside a box that grew around it.
    std::unique_ptr<Element> scaled = Layout(
        "<window width=\"400\" height=\"200\"><frame layout=\"none\" width=\"400\" height=\"200\">"
        "<vector id=\"small\" x=\"0\" y=\"0\" width=\"24\" height=\"24\">"
        "<path id=\"smallArt\" d=\"M3 12 L12 12\" width=\"8\" height=\"8\" stroke=\"#F5F5F7\" strokeWidth=\"2\"/>"
        "</vector>"
        "<vector id=\"large\" x=\"40\" y=\"0\" width=\"72\" height=\"72\">"
        "<path id=\"largeArt\" d=\"M3 12 L12 12\" width=\"8\" height=\"8\" stroke=\"#F5F5F7\" strokeWidth=\"2\"/>"
        "</vector>"
        "</frame></window>",
        400.0f, 200.0f);
    Element* smallVector = scaled != nullptr ? scaled->FindById("small") : nullptr;
    Element* largeVector = scaled != nullptr ? scaled->FindById("large") : nullptr;
    Element* smallArt = scaled != nullptr ? scaled->FindById("smallArt") : nullptr;
    Element* largeArt = scaled != nullptr ? scaled->FindById("largeArt") : nullptr;
    CHECK(test, smallVector != nullptr && largeVector != nullptr && smallArt != nullptr && largeArt != nullptr);
    if (smallVector != nullptr && largeVector != nullptr && smallArt != nullptr && largeArt != nullptr) {
        CHECK(test, std::fabs(smallArt->Bounds().width - smallVector->Bounds().width) < 0.01f);
        CHECK(test, std::fabs(smallArt->Bounds().height - smallVector->Bounds().height) < 0.01f);
        CHECK(test, std::fabs(largeArt->Bounds().width - largeVector->Bounds().width) < 0.01f);
        CHECK(test, std::fabs(largeArt->Bounds().height - largeVector->Bounds().height) < 0.01f);
        // Both children were written at the same size, so the ratio of the sizes they were drawn
        // at is the ratio of the boxes they were drawn in.
        CHECK(test, std::fabs((largeArt->Bounds().width / smallArt->Bounds().width) -
                              (largeVector->Bounds().width / smallVector->Bounds().width)) < 0.01f);
    }

    // A vector left at the size its art was written for places that art exactly where it was
    // declared, so an icon that was not resized draws no differently than before.
    std::unique_ptr<Element> authored = Layout(
        "<window width=\"200\" height=\"200\"><frame layout=\"none\" width=\"200\" height=\"200\">"
        "<vector id=\"icon\" x=\"0\" y=\"0\" width=\"8\" height=\"8\">"
        "<path id=\"art\" d=\"M3 12 L12 12\" width=\"8\" height=\"8\" stroke=\"#F5F5F7\" strokeWidth=\"2\"/>"
        "</vector>"
        "</frame></window>",
        200.0f, 200.0f);
    Element* authoredVector = authored != nullptr ? authored->FindById("icon") : nullptr;
    Element* authoredArt = authored != nullptr ? authored->FindById("art") : nullptr;
    CHECK(test, authoredVector != nullptr && authoredArt != nullptr);
    if (authoredVector != nullptr && authoredArt != nullptr) {
        CHECK(test, std::fabs(authoredArt->Bounds().width - 8.0f) < 0.01f);
        CHECK(test, std::fabs(authoredArt->Bounds().height - 8.0f) < 0.01f);
    }

    // A path is stroked the way a line is stroked, so a dash pattern written in the dash attribute
    // is read, held, and handed to the paint pass instead of being accepted and then ignored.
    std::unique_ptr<Element> dashed = Layout(
        "<window width=\"300\" height=\"200\"><frame layout=\"none\" width=\"300\" height=\"200\">"
        "<vector id=\"cut\" x=\"0\" y=\"0\" width=\"80\" height=\"80\">"
        "<path id=\"cutArt\" d=\"M0 0 L80 0\" width=\"80\" height=\"80\" stroke=\"#0E7AFF\" strokeWidth=\"2\" dash=\"6 4\" dashOffset=\"3\"/>"
        "</vector>"
        "<vector id=\"solid\" x=\"100\" y=\"0\" width=\"80\" height=\"80\">"
        "<path id=\"solidArt\" d=\"M0 0 L80 0\" width=\"80\" height=\"80\" stroke=\"#0E7AFF\" strokeWidth=\"2\"/>"
        "</vector>"
        "<vector id=\"even\" x=\"200\" y=\"0\" width=\"80\" height=\"80\">"
        "<path id=\"evenArt\" d=\"M0 0 L80 0\" width=\"80\" height=\"80\" stroke=\"#0E7AFF\" strokeWidth=\"2\" dash=\"5\"/>"
        "</vector>"
        "</frame></window>",
        300.0f, 200.0f);
    Element* dashedArt = dashed != nullptr ? dashed->FindById("cutArt") : nullptr;
    Element* solidArt = dashed != nullptr ? dashed->FindById("solidArt") : nullptr;
    Element* evenArt = dashed != nullptr ? dashed->FindById("evenArt") : nullptr;
    CHECK(test, dashedArt != nullptr && solidArt != nullptr && evenArt != nullptr);
    if (dashedArt != nullptr && solidArt != nullptr && evenArt != nullptr) {
        ShapeElement* cutPath = dynamic_cast<ShapeElement*>(dashedArt);
        ShapeElement* solidPath = dynamic_cast<ShapeElement*>(solidArt);
        ShapeElement* evenPath = dynamic_cast<ShapeElement*>(evenArt);
        CHECK(test, cutPath != nullptr && solidPath != nullptr && evenPath != nullptr);
        if (cutPath != nullptr && solidPath != nullptr && evenPath != nullptr) {
            // Several numbers alternate on and off, and the offset is where in the pattern the
            // stroke begins.
            CHECK(test, cutPath->Dashes().size() == 2);
            CHECK(test, cutPath->Dashes().size() == 2 && std::fabs(cutPath->Dashes()[0] - 6.0f) < 0.01f &&
                        std::fabs(cutPath->Dashes()[1] - 4.0f) < 0.01f);
            CHECK(test, std::fabs(cutPath->DashOffset() - 3.0f) < 0.01f);
            // One number is a dash length whose gap is the dash itself, and no pattern at all is
            // the solid stroke a path always had.
            CHECK(test, evenPath->Dashes().size() == 2 && std::fabs(evenPath->Dashes()[0] - 5.0f) < 0.01f &&
                        std::fabs(evenPath->Dashes()[1] - 5.0f) < 0.01f);
            CHECK(test, solidPath->Dashes().empty() && std::fabs(solidPath->DashOffset()) < 0.01f);
            RecordingRenderer renderer;
            dashed->Paint(renderer);
            const PaintRecord* cutPaint = PaintRecordFor(renderer, dashedArt->Bounds());
            const PaintRecord* solidPaint = PaintRecordFor(renderer, solidArt->Bounds());
            const PaintRecord* evenPaint = PaintRecordFor(renderer, evenArt->Bounds());
            CHECK(test, cutPaint != nullptr && solidPaint != nullptr && evenPaint != nullptr);
            if (cutPaint != nullptr && solidPaint != nullptr && evenPaint != nullptr) {
                // The pattern and the offset reach the paint pass rather than being dropped on the
                // way, so a dashed path is drawn dashed and a solid one is drawn solid.
                CHECK(test, cutPaint->vector && cutPaint->dashes.size() == 2 &&
                            std::fabs(cutPaint->dashes[0] - 6.0f) < 0.01f && std::fabs(cutPaint->dashes[1] - 4.0f) < 0.01f);
                CHECK(test, std::fabs(cutPaint->dashOffset - 3.0f) < 0.01f);
                CHECK(test, evenPaint->dashes.size() == 2 && std::fabs(evenPaint->dashes[0] - 5.0f) < 0.01f);
                CHECK(test, solidPaint->dashes.empty() && std::fabs(solidPaint->dashOffset) < 0.01f);
            }
            // The same values driven at the node reach the same places, so a binding that writes
            // them changes the stroke the way a written attribute does.
            cutPath->ApplyBound("dash", "2 8");
            cutPath->ApplyBound("dashOffset", "1");
            CHECK(test, cutPath->Dashes().size() == 2 && std::fabs(cutPath->Dashes()[0] - 2.0f) < 0.01f &&
                        std::fabs(cutPath->Dashes()[1] - 8.0f) < 0.01f);
            CHECK(test, std::fabs(cutPath->DashOffset() - 1.0f) < 0.01f);
            // A pattern with nothing readable in it leaves the stroke solid rather than asking the
            // renderer for a dash of no length.
            cutPath->ApplyBound("dash", "solid");
            CHECK(test, cutPath->Dashes().empty());
        }
    }

    // The path children of an imported icon are measured, placed, scaled, drawn and hit. A vector
    // holding children paints no box of its own, so the fill an imported icon carries does not
    // become a slab of colour behind its art.
    std::unique_ptr<Element> icon = Layout(
        "<window width=\"200\" height=\"200\"><frame layout=\"none\" width=\"200\" height=\"200\">"
        "<vector id=\"icon\" x=\"20\" y=\"20\" width=\"24\" height=\"24\" viewBox=\"0 0 24 24\" fill=\"#18181A\">"
        "<path id=\"stem\" d=\"M3 12 L12 12\" width=\"8\" height=\"8\" viewBox=\"0 0 24 24\" stroke=\"#F5F5F7\" strokeWidth=\"2\"/>"
        "</vector>"
        "</frame></window>",
        200.0f, 200.0f);
    Element* iconElement = icon != nullptr ? icon->FindById("icon") : nullptr;
    Element* stem = icon != nullptr ? icon->FindById("stem") : nullptr;
    CHECK(test, iconElement != nullptr && stem != nullptr);
    if (iconElement != nullptr && stem != nullptr) {
        VectorElement* iconVector = dynamic_cast<VectorElement*>(iconElement);
        CHECK(test, iconVector != nullptr);
        if (iconVector != nullptr) {
            CHECK(test, !iconVector->HasOwnGeometry());
            CHECK(test, iconElement->Children().size() == 1);
            // The art is drawn at the size of the box: the path was written at 8 x 8 and the
            // icon is 24 x 24, so the art is drawn at three times the size it was written at.
            CHECK(test, std::fabs(stem->Bounds().x - iconElement->Bounds().x) < 0.01f);
            CHECK(test, std::fabs(stem->Bounds().y - iconElement->Bounds().y) < 0.01f);
            CHECK(test, std::fabs(stem->Bounds().width - iconElement->Bounds().width) < 0.01f);
            CHECK(test, std::fabs(stem->Bounds().height - iconElement->Bounds().height) < 0.01f);
            RecordingRenderer renderer;
            icon->Paint(renderer);
            // The art is the first thing drawn in the icon box, and it is all that is drawn there:
            // nothing is laid down behind it, so the fill an imported icon carries does not become
            // a slab of colour back there.
            const PaintRecord* art = PaintRecordFor(renderer, iconElement->Bounds());
            CHECK(test, art != nullptr && art->vector);
            CHECK(test, art != nullptr && art->path == "M3 12 L12 12");
            CHECK(test, PaintRecordFor(renderer, stem->Bounds()) == art);
            // A click on the art lands on the path that was drawn, and the art owns the box it was
            // drawn into, so the icon is moved by grabbing its art.
            CHECK(test, iconElement->HitTest(Point{iconElement->Bounds().x + 2.0f, iconElement->Bounds().y + 2.0f}) == stem);
            CHECK(test, iconElement->HitTest(Point{iconElement->Bounds().x + 20.0f, iconElement->Bounds().y + 20.0f}) == stem);
            // The art reaches no further than the box it was drawn in, so a click beside the icon
            // lands on neither the art nor the vector.
            CHECK(test, iconElement->HitTest(Point{iconElement->Bounds().x - 1.0f, iconElement->Bounds().y - 1.0f}) == nullptr);
        }
    }

    // Hidden and display="none" vectors measure to nothing, paint nothing, and are not hit.
    std::unique_ptr<Element> hidden = Layout(
        "<window width=\"200\" height=\"200\"><frame layout=\"none\" width=\"200\" height=\"200\">"
        "<vector id=\"gone\" width=\"80\" height=\"80\" shape=\"ellipse\" fill=\"#0E7AFF\" display=\"none\"/>"
        "<vector id=\"away\" x=\"90\" y=\"0\" width=\"80\" height=\"80\" shape=\"ellipse\" fill=\"#0E7AFF\" visible=\"false\"/>"
        "</frame></window>",
        200.0f, 200.0f);
    Element* gone = hidden != nullptr ? hidden->FindById("gone") : nullptr;
    Element* away = hidden != nullptr ? hidden->FindById("away") : nullptr;
    CHECK(test, gone != nullptr && away != nullptr);
    if (gone != nullptr && away != nullptr) {
        CHECK(test, !gone->Visible() && !away->Visible());
        CHECK(test, gone->Measured().width == 0.0f && gone->Measured().height == 0.0f);
        CHECK(test, away->Measured().width == 0.0f && away->Measured().height == 0.0f);
        CHECK(test, gone->HitTest(Point{gone->Bounds().x + 40.0f, gone->Bounds().y + 40.0f}) == nullptr);
        RecordingRenderer renderer;
        hidden->Paint(renderer);
        CHECK(test, PaintRecordFor(renderer, gone->Bounds()) == nullptr);
    }

    // A vector takes what a rectangle takes plus the geometry it draws. The values that belong to
    // a box of content, and the two that a shape has no use for, are refused rather than accepted
    // and then quietly ignored, and so are children on a path, which draws no children at all.
    CHECK(test, CompileUi(Source("<window><vector width=\"80\" height=\"80\" shape=\"ellipse\" fill=\"#FFFFFF\" stroke=\"#000000\" strokeWidth=\"2\" radius=\"6\" d=\"M0 0 L16 16\" viewBox=\"0 0 16 16\" fillRule=\"evenodd\" lineCap=\"round\" lineJoin=\"bevel\"/></window>")).Ok());
    CHECK(test, CompileUi(Source("<window><vector><path d=\"M0 0 L8 8\"/></vector></window>")).Ok());
    CHECK(test, !CompileUi(Source("<window><vector layout=\"vertical\"/></window>")).Ok());
    CHECK(test, !CompileUi(Source("<window><vector padding=\"8\"/></window>")).Ok());
    CHECK(test, !CompileUi(Source("<window><vector gap=\"8\"/></window>")).Ok());
    CHECK(test, !CompileUi(Source("<window><vector overflow=\"scroll\"/></window>")).Ok());
    CHECK(test, !CompileUi(Source("<window><vector transform=\"scale(2)\"/></window>")).Ok());
    CHECK(test, !CompileUi(Source("<window><vector strokeOpacity=\"0.5\"/></window>")).Ok());
    CHECK(test, !CompileUi(Source("<window><path d=\"M0 0 L8 8\"><frame/></path></window>")).Ok());

    Finish(test, "vector", before);
}

void TestSvg(TestContext& test) {
    const int before = test.failed;

    // The art of an SVG is the art of the file it names, read once into the paths of the document.
    // A file of two strokes is written where a temporary file belongs, so the test reads a file
    // the way a document does.
    const std::filesystem::path fixture = std::filesystem::temp_directory_path() / "runtime_svg_artwork.svg";
    {
        std::ofstream file(fixture, std::ios::binary);
        file << "<svg viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"2\">"
                "<path d=\"M3.5 13h6\"/><path d=\"M18 7v9\"/></svg>";
    }

    // An SVG is a shape whose art comes from a file, so it is a rectangle to anything that asks,
    // and it draws the paths the file holds. The size the art was drawn for is the size of the
    // node when the document does not give one.
    const std::string source = fixture.string();
    const std::string document = "<window width=\"200\" height=\"200\"><frame layout=\"none\" width=\"200\" height=\"200\">"
                                 "<svg id=\"art\" source=\"" + source + "\"/>"
                                 "</frame></window>";
    std::unique_ptr<Element> plain = Layout(document.c_str(), 200.0f, 200.0f);
    Element* art = plain != nullptr ? plain->FindById("art") : nullptr;
    CHECK(test, art != nullptr && art->Kind() == NodeKind::SVG);
    if (art != nullptr) {
        SvgElement* svg = dynamic_cast<SvgElement*>(art);
        // Nothing was written twice: an SVG is the vector underneath, which is the rectangle.
        CHECK(test, svg != nullptr && dynamic_cast<VectorElement*>(art) != nullptr && dynamic_cast<RectangleElement*>(art) != nullptr);
        if (svg != nullptr) {
            CHECK(test, svg->Source() == source);
            CHECK(test, svg->HasViewBox() && std::fabs(svg->ViewBox().width - 24.0f) < 0.01f && std::fabs(svg->ViewBox().height - 24.0f) < 0.01f);
            CHECK(test, std::fabs(art->Bounds().width - 24.0f) < 0.01f && std::fabs(art->Bounds().height - 24.0f) < 0.01f);
            CHECK(test, art->Children().size() == 2);
            for (const std::unique_ptr<Element>& child : art->Children()) {
                CHECK(test, child->Kind() == NodeKind::PATH);
            }
            Element* stroke = art->Children().empty() ? nullptr : art->Children().front().get();
            // The art was authored at the size of the view box, so a node left at that size draws
            // every path exactly where the file placed it.
            CHECK(test, stroke != nullptr && std::fabs(stroke->Bounds().width - 24.0f) < 0.01f && std::fabs(stroke->Bounds().height - 24.0f) < 0.01f);
            ShapeElement* shape = stroke == nullptr ? nullptr : dynamic_cast<ShapeElement*>(stroke);
            CHECK(test, shape != nullptr);
            if (shape != nullptr) {
                // The stroke width of the file reaches the path, and the colour the artwork calls
                // its current colour is the fill the node draws in.
                CHECK(test, std::fabs(shape->StrokeWidth() - 2.0f) < 0.01f);
                CHECK(test, std::fabs(shape->StrokeColor().r - svg->Fill().r) < 0.01f && std::fabs(shape->StrokeColor().g - svg->Fill().g) < 0.01f &&
                            std::fabs(shape->StrokeColor().b - svg->Fill().b) < 0.01f);
            }
            // The art is what is drawn: an SVG paints no slab of its own behind it.
            RecordingRenderer renderer;
            plain->Paint(renderer);
            const PaintRecord* drawn = PaintRecordFor(renderer, art->Bounds());
            CHECK(test, drawn != nullptr && drawn->vector);
            CHECK(test, drawn != nullptr && drawn->path == "M3.5 13h6");
            // A click on the art lands on a path that was drawn rather than on the SVG around it.
            // The paths of a file are each written in the box of the file, so they all cover it,
            // and the one drawn last is the one a click inside that box lands on.
            Element* top = art->Children().empty() ? nullptr : art->Children().back().get();
            CHECK(test, top != nullptr && top != stroke);
            CHECK(test, art->HitTest(Point{art->Bounds().x + 4.0f, art->Bounds().y + 13.0f}) == top);
        }
    }

    // A size the document writes is the size the art is scaled into, so one file draws at any
    // size rather than sitting at the size it was written at inside a box that grew around it.
    std::unique_ptr<Element> scaled = Layout(
        ("<window width=\"200\" height=\"200\"><frame layout=\"none\" width=\"200\" height=\"200\">"
         "<svg id=\"big\" source=\"" + source + "\" width=\"48\" height=\"48\"/>"
         "</frame></window>").c_str(),
        200.0f, 200.0f);
    Element* big = scaled != nullptr ? scaled->FindById("big") : nullptr;
    CHECK(test, big != nullptr);
    if (big != nullptr) {
        CHECK(test, std::fabs(big->Bounds().width - 48.0f) < 0.01f && std::fabs(big->Bounds().height - 48.0f) < 0.01f);
        Element* first = big->Children().empty() ? nullptr : big->Children().front().get();
        CHECK(test, first != nullptr && std::fabs(first->Bounds().width - 48.0f) < 0.01f && std::fabs(first->Bounds().height - 48.0f) < 0.01f);
    }

    // The colour of the art is the colour the node draws in, so a document can recolour a file
    // that leaves its colour open. The view box the art belongs in is the one the file was written
    // in, whether or not the colour changed.
    std::unique_ptr<Element> recoloured = Layout(
        ("<window width=\"200\" height=\"200\"><frame layout=\"none\" width=\"200\" height=\"200\">"
         "<svg id=\"record\" source=\"" + source + "\" fill=\"#FF0000\"/>"
         "</frame></window>").c_str(),
        200.0f, 200.0f);
    Element* record = recoloured != nullptr ? recoloured->FindById("record") : nullptr;
    CHECK(test, record != nullptr);
    if (record != nullptr) {
        SvgElement* svg = dynamic_cast<SvgElement*>(record);
        CHECK(test, svg != nullptr && svg->HasViewBox() && std::fabs(svg->ViewBox().width - 24.0f) < 0.01f);
        Element* first = record->Children().empty() ? nullptr : record->Children().front().get();
        ShapeElement* shape = first == nullptr ? nullptr : dynamic_cast<ShapeElement*>(first);
        CHECK(test, shape != nullptr);
        if (shape != nullptr) {
            CHECK(test, shape->StrokeColor().r > 0.9f && shape->StrokeColor().g < 0.1f && shape->StrokeColor().b < 0.1f);
        }
    }

    // A turn on the node turns the art with it, the way a turn on a group turns what the group
    // holds, and a size that fits the art is the size the art was drawn for.
    std::unique_ptr<Element> turned = Layout(
        ("<window width=\"200\" height=\"200\"><frame layout=\"none\" width=\"200\" height=\"200\">"
         "<svg id=\"turn\" source=\"" + source + "\" rotation=\"30\"/>"
         "<svg id=\"fit\" y=\"60\" source=\"" + source + "\" width=\"fit\" height=\"fit\"/>"
         "</frame></window>").c_str(),
        200.0f, 200.0f);
    Element* turning = turned != nullptr ? turned->FindById("turn") : nullptr;
    Element* fitting = turned != nullptr ? turned->FindById("fit") : nullptr;
    CHECK(test, turning != nullptr && fitting != nullptr);
    if (turning != nullptr && fitting != nullptr) {
        SvgElement* turnedSvg = dynamic_cast<SvgElement*>(turning);
        CHECK(test, turnedSvg != nullptr && std::fabs(turnedSvg->Rotation() - 30.0f) < 0.01f);
        Element* turnArt = turning->Children().empty() ? nullptr : turning->Children().front().get();
        RecordingRenderer renderer;
        turned->Paint(renderer);
        const PaintRecord* turnPaint = turnArt == nullptr ? nullptr : PaintRecordFor(renderer, turnArt->Bounds());
        // The art is drawn inside the turn the node was given rather than left unturned.
        CHECK(test, turnPaint != nullptr && turnPaint->vector && std::fabs(turnPaint->rotation - 30.0f) < 0.01f);
        CHECK(test, std::fabs(fitting->Bounds().width - 24.0f) < 0.01f && std::fabs(fitting->Bounds().height - 24.0f) < 0.01f);
    }

    // Art written into the document is the markup itself, so a document can carry an icon with no
    // file beside it, and art that cannot be read leaves the node with no paths rather than with
    // an art of its own.
    std::unique_ptr<Element> carried = Layout(
        "<window width=\"200\" height=\"200\"><frame layout=\"none\" width=\"200\" height=\"200\">"
        "<svg id=\"markup\" source=\"&lt;svg viewBox=&quot;0 0 8 8&quot;&gt;&lt;rect width=&quot;8&quot; height=&quot;8&quot; fill=&quot;#00FF00&quot;/&gt;&lt;/svg&gt;\"/>"
        "<svg id=\"missing\" source=\"no/such/file.svg\" width=\"40\" height=\"40\"/>"
        "</frame></window>",
        200.0f, 200.0f);
    Element* markup = carried != nullptr ? carried->FindById("markup") : nullptr;
    Element* missing = carried != nullptr ? carried->FindById("missing") : nullptr;
    CHECK(test, markup != nullptr && missing != nullptr);
    if (markup != nullptr && missing != nullptr) {
        CHECK(test, markup->Children().size() == 1 && markup->Children().front()->Kind() == NodeKind::PATH);
        // The size of art written in the document is the view box the document wrote it in.
        CHECK(test, std::fabs(markup->Bounds().width - 8.0f) < 0.01f && std::fabs(markup->Bounds().height - 8.0f) < 0.01f);
        CHECK(test, missing->Children().empty());
        CHECK(test, std::fabs(missing->Bounds().width - 40.0f) < 0.01f && std::fabs(missing->Bounds().height - 40.0f) < 0.01f);
    }

    // Art written into the node is the art of the node. An SVG that carries its own paths draws
    // them and reads no file, so every path of it is a node of the document tree that can be picked
    // and edited the way any other path is. The art is written in the view box of the node, and the
    // size the art was authored at is the size of the node when the document gives no size.
    std::unique_ptr<Element> written = Layout(
        "<window width=\"200\" height=\"200\"><frame layout=\"none\" width=\"200\" height=\"200\">"
        "<svg id=\"drawn\" x=\"10\" y=\"10\" viewBox=\"0 0 24 24\">"
        "<path id=\"stem\" d=\"M4 12 H20\" width=\"24\" height=\"24\" viewBox=\"0 0 24 24\" stroke=\"#0E7AFF\" strokeWidth=\"2\"/>"
        "<path id=\"leg\" d=\"M12 4 V20\" width=\"24\" height=\"24\" viewBox=\"0 0 24 24\" stroke=\"#FF375F\" strokeWidth=\"2\"/>"
        "</svg>"
        "<svg id=\"turned\" y=\"60\" viewBox=\"0 0 24 24\" rotation=\"30\">"
        "<path id=\"turnArt\" d=\"M4 12 H20\" width=\"24\" height=\"24\" viewBox=\"0 0 24 24\" stroke=\"#30D158\"/>"
        "</svg>"
        "</frame></window>",
        200.0f, 200.0f);
    Element* drawn = written != nullptr ? written->FindById("drawn") : nullptr;
    Element* turnedArt = written != nullptr ? written->FindById("turned") : nullptr;
    CHECK(test, drawn != nullptr && turnedArt != nullptr);
    if (drawn != nullptr && turnedArt != nullptr) {
        CHECK(test, drawn->Kind() == NodeKind::SVG && dynamic_cast<SvgElement*>(drawn) != nullptr);
        CHECK(test, drawn->Children().size() == 2);
        for (const std::unique_ptr<Element>& child : drawn->Children()) {
            CHECK(test, child->Kind() == NodeKind::PATH);
        }
        // The art was authored at the size of its view box, so a node the document gave no size is
        // the size of that art rather than the square a shape falls back to.
        CHECK(test, std::fabs(drawn->Bounds().width - 24.0f) < 0.01f && std::fabs(drawn->Bounds().height - 24.0f) < 0.01f);
        Element* bottom = drawn->Children().empty() ? nullptr : drawn->Children().front().get();
        Element* top = drawn->Children().empty() ? nullptr : drawn->Children().back().get();
        CHECK(test, bottom != nullptr && top != nullptr && top != bottom);
        RecordingRenderer renderer;
        written->Paint(renderer);
        // Both paths the document wrote are drawn, each one as the path it was written as. Both
        // cover the box of the art, so each is looked for by the path it draws rather than by the
        // box it was drawn into.
        const auto paintOfPath = [](const RecordingRenderer& painted, const char* path) {
            for (const PaintRecord& record : painted.records) {
                if (record.vector && record.path == path) {
                    return &record;
                }
            }
            return static_cast<const PaintRecord*>(nullptr);
        };
        CHECK(test, paintOfPath(renderer, "M4 12 H20") != nullptr);
        CHECK(test, paintOfPath(renderer, "M12 4 V20") != nullptr);
        // A click inside the art lands on a path the document wrote, which is what makes each path
        // of the art editable on its own, and the area the art does not reach still belongs to the
        // SVG, so a click beside the art moves the whole node.
        CHECK(test, drawn->HitTest(Point{drawn->Bounds().x + 12.0f, drawn->Bounds().y + 12.0f}) == top);
        // A turn on the node turns the art written in it the way it turns the art of a file.
        Element* turnArt = turnedArt->Children().empty() ? nullptr : turnedArt->Children().front().get();
        const PaintRecord* turnPaint = turnArt == nullptr ? nullptr : PaintRecordFor(renderer, turnArt->Bounds());
        CHECK(test, turnPaint != nullptr && turnPaint->vector && std::fabs(turnPaint->rotation - 30.0f) < 0.01f);
    }

    // A node that writes its art inside it draws that art, and a node that names a file and writes
    // no art of its own draws the file. A node that does both draws what the document wrote: the art
    // a document carries is the art a document can edit, so a file named beside it is never read for
    // a node that already holds art.
    std::unique_ptr<Element> both = Layout(
        ("<window width=\"200\" height=\"200\"><frame layout=\"none\" width=\"200\" height=\"200\">"
         "<svg id=\"pick\" source=\"" + source + "\" viewBox=\"0 0 24 24\">"
         "<path id=\"mine\" d=\"M0 0 H24\" width=\"24\" height=\"24\" viewBox=\"0 0 24 24\" stroke=\"#0E7AFF\"/>"
         "</svg>"
         "</frame></window>").c_str(),
        200.0f, 200.0f);
    Element* pick = both != nullptr ? both->FindById("pick") : nullptr;
    CHECK(test, pick != nullptr);
    if (pick != nullptr) {
        CHECK(test, pick->Children().size() == 1 && pick->FindById("mine") != nullptr);
        CHECK(test, pick->Children().front()->Kind() == NodeKind::PATH);
        // The art the document wrote is drawn, so the file's two paths are not the two paths here.
        RecordingRenderer renderer;
        both->Paint(renderer);
        const PaintRecord* mine = PaintRecordFor(renderer, pick->Children().front()->Bounds());
        CHECK(test, mine != nullptr && mine->vector && mine->path == "M0 0 H24");
    }

    // Where a document's art is looked for is not where the program happens to have been started
    // from. A document names its files the way the project around it is laid out, and a document
    // that keeps its art beside itself names it that way, so both are read from a directory that
    // holds neither of them. The program is moved to a directory of its own for the reading,
    // because the directory it was started from is the one place these files are not: reading art
    // from wherever the program was launched is art that shows in one place and vanishes in another.
    const std::filesystem::path elsewhere = std::filesystem::temp_directory_path() / "runtime_svg_elsewhere";
    const std::filesystem::path besideItself = std::filesystem::temp_directory_path() / "runtime_svg_beside";
    std::error_code error;
    std::filesystem::create_directories(elsewhere, error);
    std::filesystem::create_directories(besideItself, error);
    {
        std::ofstream file(besideItself / "Press.svg", std::ios::binary);
        file << "<svg viewBox=\"0 0 12 12\"><path d=\"M1 1 L11 1 L11 11 Z\"/></svg>";
    }
    const std::filesystem::path started = std::filesystem::current_path(error);
    std::filesystem::current_path(elsewhere, error);
    const auto readArt = [&](const SourceFile& source) {
        const CompileResult compiled = CompileUi(source);
        CHECK(test, compiled.Ok());
        return compiled.root.has_value() ? Instantiate(*compiled.root, {}) : nullptr;
    };
    std::unique_ptr<Element> fromProject =
        readArt(Source("<window><svg id=\"art\" source=\"examples/Hello/Logo.svg\"/></window>"));
    std::unique_ptr<Element> fromBeside = readArt(
        SourceFile{(besideItself / "Doc.xml").string(), "<window><svg id=\"art\" source=\"Press.svg\"/></window>"});
    std::unique_ptr<Element> fromNowhere =
        readArt(Source("<window><svg id=\"art\" source=\"Nowhere.svg\"/></window>"));
    std::filesystem::current_path(started, error);
    const auto layersOf = [](const std::unique_ptr<Element>& root) {
        Element* node = root == nullptr ? nullptr : root->FindById("art");
        return node == nullptr ? -1 : static_cast<int>(node->Children().size());
    };
    // The mark the project keeps is two paths, the press the document keeps beside itself is one,
    // and a file nobody keeps is no art at all.
    CHECK(test, layersOf(fromProject) == 2);
    CHECK(test, layersOf(fromBeside) == 1);
    CHECK(test, layersOf(fromNowhere) == 0);
    // A file that was found nowhere leaves the node saying the name the document wrote, so the node
    // still answers for the file it was asked to draw.
    Element* unwritten = fromNowhere == nullptr ? nullptr : fromNowhere->FindById("art");
    const SvgElement* said = dynamic_cast<const SvgElement*>(unwritten);
    CHECK(test, said != nullptr && said->Source() == "Nowhere.svg");

    // An SVG takes everything a vector takes, because an SVG the document carries is a vector: the
    // box, the fill, the stroke, the pattern to stroke it with, the corners, the geometry it draws of
    // its own, the view box that geometry is written in, and the rule its fill follows. It adds the
    // file it can draw instead, and it takes the art written inside it. What it still refuses is a
    // box of content: no layout, no padding, no gap, no overflow to scroll.
    CHECK(test, CompileUi(Source("<window><svg source=\"logo.svg\" width=\"24\" height=\"24\" fill=\"#18181A\" background=\"#FFFFFF\" "
                                  "rotation=\"10\" cursor=\"pointer\" margin=\"4\" grow=\"1\" zIndex=\"2\"/></window>"))
                    .Ok());
    CHECK(test, CompileUi(Source("<window><svg stroke=\"#FFFFFF\" strokeWidth=\"2\" dash=\"4 2\" dashGap=\"2\" dashOffset=\"1\" "
                                  "radius=\"8\" radiusTopLeft=\"8\" viewBox=\"0 0 12 12\" d=\"M0 0 L8 8\" shape=\"ellipse\" "
                                  "fillRule=\"evenodd\"/></window>"))
                    .Ok());
    CHECK(test, CompileUi(Source("<window><svg viewBox=\"0 0 24 24\"><path d=\"M0 0 L8 8\"/><group><path d=\"M1 1 L2 2\"/></group></svg></window>")).Ok());
    CHECK(test, !CompileUi(Source("<window><svg layout=\"vertical\"/></window>")).Ok());
    CHECK(test, !CompileUi(Source("<window><svg gap=\"8\"/></window>")).Ok());
    CHECK(test, !CompileUi(Source("<window><svg padding=\"8\"/></window>")).Ok());
    CHECK(test, !CompileUi(Source("<window><svg overflow=\"scroll\"/></window>")).Ok());

    Finish(test, "svg", before);
}

void TestRichText(TestContext& test) {
    const int before = test.failed;

    // A block of rich text is a block of text whose content is made of runs, so it is a text block to
    // anything that asks, and the runs it holds are the spans of the document. Nothing is written
    // twice: a span is the text component underneath, so it is a block of text and a frame and an
    // element, and a rich text block is a block of text.
    std::unique_ptr<Element> plain = Layout(
        "<window width=\"400\" height=\"200\"><frame layout=\"none\" width=\"400\" height=\"200\">"
        "<richtext id=\"para\" x=\"10\" y=\"10\" text=\"Hello \" fontSize=\"20\" lineHeight=\"1.42\">"
        "<span id=\"run\" text=\"world\" fontWeight=\"700\" textColor=\"#FF375F\"/>"
        "</richtext>"
        "</frame></window>",
        400.0f, 200.0f);
    Element* para = plain != nullptr ? plain->FindById("para") : nullptr;
    Element* run = plain != nullptr ? plain->FindById("run") : nullptr;
    CHECK(test, para != nullptr && run != nullptr);
    CHECK(test, para != nullptr && para->Kind() == NodeKind::RICHTEXT && dynamic_cast<TextElement*>(para) != nullptr);
    CHECK(test, run != nullptr && run->Kind() == NodeKind::SPAN && dynamic_cast<TextElement*>(run) != nullptr);
    if (para != nullptr && run != nullptr) {
        RichTextElement* rich = dynamic_cast<RichTextElement*>(para);
        SpanElement* span = dynamic_cast<SpanElement*>(run);
        CHECK(test, rich != nullptr && span != nullptr);
        CHECK(test, para->Children().size() == 1 && para->Children().front().get() == run);
        if (rich != nullptr && span != nullptr) {
            // The paragraph is the text the block carries and then the span it holds, in the order
            // they are set, and every run takes the values of the block it did not write itself.
            const std::vector<TextRun> runs = rich->Runs();
            CHECK(test, runs.size() == 2);
            if (runs.size() == 2) {
                // The space the block ends its text with is part of the run, which is what keeps
                // the words of a paragraph apart when they are written as runs of their own.
                CHECK(test, runs[0].text == "Hello " && runs[1].text == "world");
                CHECK(test, std::fabs(runs[0].style.fontSize - 20.0f) < 0.01f && std::fabs(runs[1].style.fontSize - 20.0f) < 0.01f);
                CHECK(test, runs[0].style.fontWeight == 400 && runs[1].style.fontWeight == 700);
                CHECK(test, std::fabs(runs[1].style.color.r - 1.0f) < 0.01f && runs[1].style.color.g < 0.4f);
            }
            // The box the block covers is the box the runs cover together: a character is half a size
            // wide in this measurer, so eleven characters of twenty are one hundred and ten across,
            // and the line they are set on is the line height of the block over the size of a run.
            const float line = 20.0f * 1.42f;
            CHECK(test, std::fabs(para->Bounds().width - 110.0f) < 0.01f);
            CHECK(test, std::fabs(para->Bounds().height - line) < 0.01f);
            CHECK(test, rich->RunBoxes().size() == 2);
            if (rich->RunBoxes().size() == 2) {
                CHECK(test, std::fabs(rich->RunBoxes()[0].x) < 0.01f && std::fabs(rich->RunBoxes()[0].width - 60.0f) < 0.01f);
                CHECK(test, std::fabs(rich->RunBoxes()[1].x - 60.0f) < 0.01f && std::fabs(rich->RunBoxes()[1].width - 50.0f) < 0.01f);
            }
            // A run is placed where it was drawn: the span covers the stretch of the line its own
            // text took, which is what lets the editor pick that word out of the paragraph.
            CHECK(test, std::fabs(run->Bounds().x - (para->Bounds().x + 60.0f)) < 0.01f);
            CHECK(test, std::fabs(run->Bounds().y - para->Bounds().y) < 0.01f);
            CHECK(test, std::fabs(run->Bounds().width - 50.0f) < 0.01f);
            CHECK(test, std::fabs(run->Bounds().height - line) < 0.01f);
            // The whole paragraph is one draw, and every run reached it with its own values.
            RecordingRenderer renderer;
            plain->Paint(renderer);
            const PaintRecord* drawn = PaintRecordFor(renderer, para->Bounds());
            CHECK(test, drawn != nullptr && drawn->runs.size() == 2);
            if (drawn != nullptr && drawn->runs.size() == 2) {
                CHECK(test, drawn->runs[0].text == "Hello " && drawn->runs[1].text == "world");
                CHECK(test, drawn->runs[0].fontWeight == 400 && drawn->runs[1].fontWeight == 700);
                CHECK(test, drawn->runs[1].color.r > 0.9f && drawn->runs[1].color.g < 0.4f);
            }
            // A click on a run picks that run out of the block, and a click inside the block beside
            // its text still belongs to the block.
            CHECK(test, para->HitTest(Point{run->Bounds().x + 4.0f, run->Bounds().y + 4.0f}) == run);
            CHECK(test, para->HitTest(Point{para->Bounds().x + 4.0f, para->Bounds().y + 4.0f}) == para);
            CHECK(test, para->HitTest(Point{para->Bounds().x - 4.0f, para->Bounds().y}) == nullptr);
        }
    }

    // A width the document wrote is the width the lines are held in, so the runs of a paragraph wrap
    // together: a run that does not fit on the line it is on begins the next one, and the box it is
    // given is the box it reaches on that line.
    std::unique_ptr<Element> wrapped = Layout(
        "<window width=\"400\" height=\"200\"><frame layout=\"none\" width=\"400\" height=\"200\">"
        "<richtext id=\"para\" x=\"10\" y=\"10\" width=\"60\" text=\"Hello \" fontSize=\"20\" lineHeight=\"1.42\">"
        "<span id=\"run\" text=\"world\" textColor=\"#30D158\"/>"
        "</richtext>"
        "</frame></window>",
        400.0f, 200.0f);
    Element* wrapping = wrapped != nullptr ? wrapped->FindById("para") : nullptr;
    Element* nextLine = wrapped != nullptr ? wrapped->FindById("run") : nullptr;
    CHECK(test, wrapping != nullptr && nextLine != nullptr);
    if (wrapping != nullptr && nextLine != nullptr) {
        const float line = 20.0f * 1.42f;
        CHECK(test, std::fabs(wrapping->Bounds().width - 60.0f) < 0.01f);
        CHECK(test, std::fabs(wrapping->Bounds().height - line * 2.0f) < 0.01f);
        // The run that did not fit is on the second line, at the left edge of the block.
        CHECK(test, std::fabs(nextLine->Bounds().x - wrapping->Bounds().x) < 0.01f);
        CHECK(test, std::fabs(nextLine->Bounds().y - (wrapping->Bounds().y + line)) < 0.01f);
    }

    // A span that writes nothing is drawn with the values of its block, and a span that writes one
    // value is drawn with that value and the block's for everything else. The case a block asks for
    // reaches the runs that did not ask for one of their own, and a run that asks for another case
    // is drawn in the case it asked for.
    std::unique_ptr<Element> inherited = Layout(
        "<window width=\"400\" height=\"200\"><frame layout=\"none\" width=\"400\" height=\"200\">"
        "<richtext id=\"para\" x=\"10\" y=\"10\" text=\"Lead \" textTransform=\"uppercase\" fontSize=\"20\" lineHeight=\"1.42\">"
        "<span id=\"said\" text=\"kept\"/>"
        "<span id=\"chose\" text=\"QUIET\" textCase=\"lowercase\" fontSize=\"12\"/>"
        "</richtext>"
        "</frame></window>",
        400.0f, 200.0f);
    Element* cases = inherited != nullptr ? inherited->FindById("para") : nullptr;
    if (Element* said = inherited != nullptr ? inherited->FindById("said") : nullptr) {
        RichTextElement* rich = dynamic_cast<RichTextElement*>(cases);
        CHECK(test, rich != nullptr);
        if (rich != nullptr) {
            const std::vector<TextRun> runs = rich->Runs();
            CHECK(test, runs.size() == 3);
            if (runs.size() == 3) {
                CHECK(test, runs[1].text == "KEPT");
                CHECK(test, runs[2].text == "quiet");
                // The run that named a size is drawn at that size and the run that named none at the
                // size of the block, and the line of the block holds the taller of them.
                CHECK(test, std::fabs(runs[1].style.fontSize - 20.0f) < 0.01f && std::fabs(runs[2].style.fontSize - 12.0f) < 0.01f);
                CHECK(test, std::fabs(cases->Bounds().height - (20.0f * 1.42f)) < 0.01f);
                // The tracking of a run is a fraction of its own size, so a run the block sized and a
                // run that sized itself are tracked by the same fraction rather than by one distance.
                CHECK(test, std::fabs(runs[1].style.letterSpacing - runs[2].style.letterSpacing) < 0.01f);
            }
        }
    }

    // The size the block covers is decided by the values a block of text reads, so a rich text block
    // hugs its width, grows with the lines its runs wrap into, and keeps a size the document fixed.
    std::unique_ptr<Element> sized = Layout(
        "<window width=\"400\" height=\"200\"><frame layout=\"none\" width=\"400\" height=\"200\">"
        "<richtext id=\"hug\" x=\"10\" y=\"10\" sizing=\"autoWidth\" fontSize=\"20\" lineHeight=\"1.42\" text=\"Hell\">"
        "<span id=\"tail\" text=\"o!\"/>"
        "</richtext>"
        "<richtext id=\"tall\" x=\"10\" y=\"80\" width=\"40\" sizing=\"autoHeight\" fontSize=\"20\" lineHeight=\"1.42\" text=\"one \">"
        "<span id=\"more\" text=\"two\"/>"
        "</richtext>"
        "<richtext id=\"fixed\" x=\"200\" y=\"80\" width=\"120\" height=\"90\" fontSize=\"20\" text=\"kept\"/>"
        "</frame></window>",
        400.0f, 200.0f);
    const auto boxOf = [&](const char* id) {
        Element* found = sized != nullptr ? sized->FindById(id) : nullptr;
        return found == nullptr ? Rect{} : found->Bounds();
    };
    const float hugLine = 20.0f * 1.42f;
    // Six characters of twenty at half a size each is sixty across and one line down.
    CHECK(test, std::fabs(boxOf("hug").width - 60.0f) < 0.01f && std::fabs(boxOf("hug").height - hugLine) < 0.01f);
    // Two runs that do not fit on one line of forty are two lines down and as wide as they were given.
    CHECK(test, std::fabs(boxOf("tall").width - 40.0f) < 0.01f && std::fabs(boxOf("tall").height - hugLine * 2.0f) < 0.01f);
    // A fixed block is the size it was given, whatever its runs cover.
    CHECK(test, std::fabs(boxOf("fixed").width - 120.0f) < 0.01f && std::fabs(boxOf("fixed").height - 90.0f) < 0.01f);

    // A paragraph that is hidden measures to nothing, paints nothing, and is not hit.
    std::unique_ptr<Element> hidden = Layout(
        "<window width=\"400\" height=\"200\"><frame layout=\"none\" width=\"400\" height=\"200\">"
        "<richtext id=\"gone\" x=\"10\" y=\"10\" display=\"none\" text=\"seen\"><span id=\"lit\" text=\"too\"/></richtext>"
        "</frame></window>",
        400.0f, 200.0f);
    Element* gone = hidden != nullptr ? hidden->FindById("gone") : nullptr;
    Element* lit = hidden != nullptr ? hidden->FindById("lit") : nullptr;
    CHECK(test, gone != nullptr && lit != nullptr);
    if (gone != nullptr && lit != nullptr) {
        CHECK(test, gone->Measured().width == 0.0f && gone->Measured().height == 0.0f);
        CHECK(test, gone->HitTest(Point{gone->Bounds().x, gone->Bounds().y}) == nullptr);
        // The runs a hidden paragraph holds are taken out with it: a word of a paragraph that was
        // taken out of the layout is a box of no size, and a click cannot land on it.
        CHECK(test, lit->Bounds().width == 0.0f && lit->Bounds().height == 0.0f);
        CHECK(test, gone->FindById("lit") == nullptr || gone->FindById("lit")->HitTest(Point{lit->Bounds().x, lit->Bounds().y}) == nullptr);
        RecordingRenderer renderer;
        hidden->Paint(renderer);
        CHECK(test, PaintRecordFor(renderer, Rect{10.0f, 10.0f, 8.0f, 8.0f}) == nullptr);
    }

    // The text a document writes is the text the document gets, spaces at its ends included: what
    // separates one run of a paragraph from the next is that space, and the compiler is the only
    // place it could be lost. A value chosen out of a set is a keyword of the set rather than text,
    // so it is the word of the set that reaches the element whatever spacing it was written with.
    std::unique_ptr<Element> padded =
        Layout("<window width=\"200\" height=\"80\"><text id=\"kept\" text=\" said \" fontSize=\"20\"/></window>", 200.0f, 80.0f);
    const TextElement* spaced = padded != nullptr ? dynamic_cast<const TextElement*>(padded->FindById("kept")) : nullptr;
    CHECK(test, spaced != nullptr && spaced->Text() == " said ");
    std::unique_ptr<Element> keyword =
        Layout("<window width=\"200\" height=\"80\"><frame id=\"column\" layout=\" vertical \"/></window>", 200.0f, 80.0f);
    const Frame* column = keyword != nullptr ? dynamic_cast<const Frame*>(keyword->FindById("column")) : nullptr;
    CHECK(test, column != nullptr && column->Layout() == FrameLayout::VERTICAL);

    // A block of rich text takes everything a block of text takes, because it is one, and the runs
    // inside it take the values that set a run of text. What a run refuses is everything that
    // describes a box or the lines of a block: a run has no size, no place, no padding, no layout,
    // and no line height of its own, and a name only the whole block answers for is refused too.
    CHECK(test, CompileUi(Source("<window><richtext fontSize=\"20\"><span text=\"hi\" fontWeight=\"700\" tone=\"danger\" underline=\"true\"/></richtext></window>")).Ok());
    CHECK(test, CompileUi(Source("<window><richtext text=\"Hello \" width=\"200\" height=\"80\" lineHeight=\"1.2\" textAlign=\"center\" sizing=\"autoHeight\" maxLines=\"2\" overline=\"true\" fontFile=\"assets/fonts/Inter.ttf\"><span id=\"run\" name=\"Run\" text=\"world\" fontFamily=\"Inter\"/></richtext></window>"))
                    .Ok());
    // A run names a face of the collection the block is drawn in, and refusing the file a block
    // loads its collection from is what keeps the paragraph one draw rather than one per run.
    CHECK(test, !CompileUi(Source("<window><span fontFile=\"x.ttf\"/></window>")).Ok());
    CHECK(test, CompileUi(Source("<window><span text=\"alone\"/></window>")).Ok());
    CHECK(test, !CompileUi(Source("<window><span width=\"40\"/></window>")).Ok());
    CHECK(test, !CompileUi(Source("<window><span height=\"40\"/></window>")).Ok());
    CHECK(test, !CompileUi(Source("<window><span x=\"4\"/></window>")).Ok());
    CHECK(test, !CompileUi(Source("<window><span y=\"4\"/></window>")).Ok());
    CHECK(test, !CompileUi(Source("<window><span position=\"absolute\"/></window>")).Ok());
    CHECK(test, !CompileUi(Source("<window><span padding=\"4\"/></window>")).Ok());
    CHECK(test, !CompileUi(Source("<window><span layout=\"vertical\"/></window>")).Ok());
    CHECK(test, !CompileUi(Source("<window><span gap=\"4\"/></window>")).Ok());
    CHECK(test, !CompileUi(Source("<window><span lineHeight=\"1.2\"/></window>")).Ok());
    CHECK(test, !CompileUi(Source("<window><span sizing=\"autoWidth\"/></window>")).Ok());
    CHECK(test, !CompileUi(Source("<window><span maxLines=\"2\"/></window>")).Ok());
    CHECK(test, !CompileUi(Source("<window><span textAlign=\"center\"/></window>")).Ok());
    CHECK(test, !CompileUi(Source("<window><span verticalAlign=\"center\"/></window>")).Ok());
    CHECK(test, !CompileUi(Source("<window><span overline=\"true\"/></window>")).Ok());
    CHECK(test, !CompileUi(Source("<window><span writingMode=\"vertical\"/></window>")).Ok());
    CHECK(test, !CompileUi(Source("<window><span level=\"2\"/></window>")).Ok());
    CHECK(test, !CompileUi(Source("<window><span size=\"sm\"/></window>")).Ok());
    CHECK(test, !CompileUi(Source("<window><span direction=\"rtl\"/></window>")).Ok());
    CHECK(test, !CompileUi(Source("<window><span background=\"#FFFFFF\"/></window>")).Ok());
    CHECK(test, !CompileUi(Source("<window><span radius=\"4\"/></window>")).Ok());
    CHECK(test, !CompileUi(Source("<window><span visible=\"false\"/></window>")).Ok());
    // A run holds no children: a run is a stretch of a line, and a line holds no boxes.
    CHECK(test, !CompileUi(Source("<window><span text=\"hi\"><span text=\"there\"/></span></window>")).Ok());
    // A block of rich text draws no box of its own, so what describes a shape is refused.
    CHECK(test, !CompileUi(Source("<window><richtext d=\"M0 0 L8 8\"/></window>")).Ok());
    CHECK(test, !CompileUi(Source("<window><richtext source=\"logo.svg\"/></window>")).Ok());

    Finish(test, "rich text", before);
}

void TestLine(TestContext& test) {
    const int before = test.failed;

    // A line is two endpoints, so its box is their span and it holds nothing.
    std::unique_ptr<Element> plain = Layout(
        "<window width=\"400\" height=\"200\"><frame layout=\"none\" width=\"400\" height=\"200\">"
        "<line id=\"span\" x1=\"0\" y1=\"0\" x2=\"120\" y2=\"40\"/>"
        "</frame></window>",
        400.0f, 200.0f);
    Element* span = plain != nullptr ? plain->FindById("span") : nullptr;
    CHECK(test, span != nullptr && span->Kind() == NodeKind::LINE);
    if (span != nullptr) {
        CHECK(test, std::fabs(span->Bounds().width - 120.0f) < 0.01f);
        CHECK(test, std::fabs(span->Bounds().height - 40.0f) < 0.01f);
        CHECK(test, span->Children().empty());
    }

    // An explicit box stretches the endpoints with it, so dragging a line's handle lengthens
    // the line instead of only growing the area that catches clicks.
    std::unique_ptr<Element> stretched = Layout(
        "<window width=\"400\" height=\"200\"><frame layout=\"none\" width=\"400\" height=\"200\">"
        "<line id=\"long\" x1=\"0\" y1=\"0\" x2=\"60\" y2=\"20\" width=\"180\" height=\"60\"/>"
        "</frame></window>",
        400.0f, 200.0f);
    Element* longLine = stretched != nullptr ? stretched->FindById("long") : nullptr;
    CHECK(test, longLine != nullptr);
    if (longLine != nullptr) {
        CHECK(test, std::fabs(longLine->Bounds().width - 180.0f) < 0.01f);
        CHECK(test, std::fabs(longLine->Bounds().height - 60.0f) < 0.01f);
        // The stroke now reaches the far corner of the box, three times as far as it did.
        const Rect box = longLine->Bounds();
        CHECK(test, longLine->HitTest(Point{box.x + box.width - 1.0f, box.y + box.height - 1.0f}) == longLine);
        CHECK(test, longLine->HitTest(Point{box.x + box.width * 0.5f, box.y + 1.0f}) == nullptr);
    }

    // A line with no run on an axis is drawn across the middle of its box rather than hugging
    // one edge, which is what makes a horizontal line with a height look right.
    std::unique_ptr<Element> flat = Layout(
        "<window width=\"200\" height=\"100\"><frame layout=\"none\" width=\"200\" height=\"100\">"
        "<line id=\"flat\" x1=\"0\" y1=\"0\" x2=\"80\" y2=\"0\" width=\"80\" height=\"20\"/>"
        "</frame></window>",
        200.0f, 100.0f);
    Element* flatLine = flat != nullptr ? flat->FindById("flat") : nullptr;
    CHECK(test, flatLine != nullptr);
    if (flatLine != nullptr) {
        const Rect box = flatLine->Bounds();
        CHECK(test, flatLine->HitTest(Point{box.x + 40.0f, box.y + 10.0f}) == flatLine);
        CHECK(test, flatLine->HitTest(Point{box.x + 40.0f, box.y + 1.0f}) == nullptr);
        CHECK(test, flatLine->HitTest(Point{box.x + 20.0f, box.y + 19.0f}) == nullptr);
    }

    // The stroke, its width, and its cap reach the paint pass, and only the requested dash
    // pattern turns a solid stroke into a dashed one.
    std::unique_ptr<Element> painted = Layout(
        "<window width=\"300\" height=\"200\"><frame layout=\"none\" width=\"300\" height=\"200\">"
        "<line id=\"solid\" x1=\"0\" y1=\"0\" x2=\"80\" y2=\"0\" width=\"80\" height=\"20\" stroke=\"#FF0000\" strokeWidth=\"4\" opacity=\"0.5\"/>"
        "<line id=\"dashed\" x1=\"0\" y1=\"0\" x2=\"80\" y2=\"0\" width=\"80\" height=\"20\" stroke=\"#00FF00\" strokeWidth=\"2\" dash=\"6\" dashGap=\"4\" lineCap=\"round\"/>"
        "<line id=\"dotted\" x1=\"0\" y1=\"0\" x2=\"80\" y2=\"0\" width=\"80\" height=\"20\" stroke=\"#0000FF\" strokeWidth=\"2\" dash=\"2 6\"/>"
        "</frame></window>",
        300.0f, 200.0f);
    Element* solidLine = painted != nullptr ? painted->FindById("solid") : nullptr;
    Element* dashedLine = painted != nullptr ? painted->FindById("dashed") : nullptr;
    Element* dottedLine = painted != nullptr ? painted->FindById("dotted") : nullptr;
    CHECK(test, solidLine != nullptr && dashedLine != nullptr && dottedLine != nullptr);
    if (solidLine != nullptr && dashedLine != nullptr && dottedLine != nullptr) {
        LineElement* solid = dynamic_cast<LineElement*>(solidLine);
        LineElement* dashed = dynamic_cast<LineElement*>(dashedLine);
        LineElement* dotted = dynamic_cast<LineElement*>(dottedLine);
        CHECK(test, solid != nullptr && dashed != nullptr && dotted != nullptr);
        if (solid != nullptr && dashed != nullptr && dotted != nullptr) {
            CHECK(test, solid->Dashes().empty());
            CHECK(test, solid->LineCap() == "butt");
            CHECK(test, dashed->Dashes().size() == 2 && std::fabs(dashed->Dashes()[0] - 6.0f) < 0.01f && std::fabs(dashed->Dashes()[1] - 4.0f) < 0.01f);
            CHECK(test, dashed->LineCap() == "round");
            // Two numbers alternate on and off by themselves, with no gap property needed.
            CHECK(test, dotted->Dashes().size() == 2 && std::fabs(dotted->Dashes()[0] - 2.0f) < 0.01f && std::fabs(dotted->Dashes()[1] - 6.0f) < 0.01f);
        }
        RecordingRenderer renderer;
        painted->Paint(renderer);
        const PaintRecord* solidPaint = PaintRecordFor(renderer, solidLine->Bounds());
        CHECK(test, solidPaint != nullptr);
        if (solidPaint != nullptr) {
            CHECK(test, std::fabs(solidPaint->opacity - 0.5f) < 0.001f);
            CHECK(test, solidPaint->rotationDepth == 0 && solidPaint->clipDepth == 0);
            // The stroke is one path, not a stamp of squares, so a long line costs one draw.
            CHECK(test, solidPaint->vector);
            if (solidPaint->vector) {
                // The path runs straight from one end of the box to the other, across the
                // middle, which is the line the endpoints ask for.
                const Rect box = solidLine->Bounds();
                const std::vector<PathSample> samples = SamplePath(solidPaint->path);
                CHECK(test, samples.size() >= 2);
                if (samples.size() >= 2) {
                    const PathSample& first = samples.front();
                    const PathSample& last = samples.back();
                    CHECK(test, std::fabs(first.x) < 1.0f && std::fabs(first.y - box.height * 0.5f) < 1.0f);
                    CHECK(test, std::fabs(last.x - box.width) < 1.0f && std::fabs(last.y - box.height * 0.5f) < 1.0f);
                }
            }
        }
    }

    // A line is hit along its stroke and not in the empty half of its box.
    std::unique_ptr<Element> hits = Layout(
        "<window width=\"200\" height=\"200\"><frame layout=\"none\" width=\"200\" height=\"200\">"
        "<line id=\"diagonal\" x=\"20\" y=\"20\" x1=\"0\" y1=\"0\" x2=\"100\" y2=\"100\" width=\"100\" height=\"100\" strokeWidth=\"2\"/>"
        "<line id=\"turned\" x=\"0\" y=\"140\" x1=\"0\" y1=\"0\" x2=\"80\" y2=\"0\" width=\"80\" height=\"20\" rotation=\"90\"/>"
        "</frame></window>",
        200.0f, 200.0f);
    Element* diagonal = hits != nullptr ? hits->FindById("diagonal") : nullptr;
    Element* turned = hits != nullptr ? hits->FindById("turned") : nullptr;
    CHECK(test, diagonal != nullptr && turned != nullptr);
    if (diagonal != nullptr && turned != nullptr) {
        const Rect box = diagonal->Bounds();
        CHECK(test, diagonal->HitTest(Point{box.x + 50.0f, box.y + 50.0f}) == diagonal);
        CHECK(test, diagonal->HitTest(Point{box.x + 50.0f, box.y + 2.0f}) == nullptr);
        CHECK(test, diagonal->HitTest(Point{box.x + 2.0f, box.y + 50.0f}) == nullptr);
        // Undoing the turn is what keeps the stroke hit along the line rather than across it.
        const Rect turnedBox = turned->Bounds();
        CHECK(test, turned->HitTest(Point{turnedBox.x + turnedBox.width * 0.5f, turnedBox.y + turnedBox.height * 0.5f}) == turned);
        CHECK(test, turned->HitTest(Point{turnedBox.x + 1.0f, turnedBox.y + 1.0f}) == nullptr);
    }

    // A binding or an animation drives the line's own values, endpoints included.
    std::unique_ptr<Element> driven = Layout(
        "<window width=\"200\" height=\"200\"><frame layout=\"none\" width=\"200\" height=\"200\">"
        "<line id=\"driven\" x1=\"0\" y1=\"0\" x2=\"60\" y2=\"0\" width=\"60\" height=\"10\" stroke=\"#000000\" strokeWidth=\"1\"/>"
        "</frame></window>",
        200.0f, 200.0f);
    LineElement* drivenLine = driven != nullptr ? dynamic_cast<LineElement*>(driven->FindById("driven")) : nullptr;
    CHECK(test, drivenLine != nullptr);
    if (drivenLine != nullptr) {
        drivenLine->ApplyBound("stroke", "#FF0000");
        CHECK(test, drivenLine->StrokeColor().r > 0.9f && drivenLine->StrokeColor().g < 0.1f);
        drivenLine->ApplyBound("strokeWidth", "5");
        CHECK(test, std::fabs(drivenLine->StrokeWidth() - 5.0f) < 0.01f);
        drivenLine->ApplyBound("lineCap", "round");
        CHECK(test, drivenLine->LineCap() == "round");
        drivenLine->ApplyBound("dash", "8");
        CHECK(test, drivenLine->Dashes().size() == 2 && std::fabs(drivenLine->Dashes()[0] - 8.0f) < 0.01f && std::fabs(drivenLine->Dashes()[1] - 8.0f) < 0.01f);
        drivenLine->ApplyBound("dashGap", "3");
        CHECK(test, drivenLine->Dashes().size() == 2 && std::fabs(drivenLine->Dashes()[1] - 3.0f) < 0.01f);
        drivenLine->ApplyBound("y2", "40");
        CHECK(test, std::fabs(drivenLine->EndY() - 40.0f) < 0.01f);
        drivenLine->ApplyBound("rotation", "90");
        CHECK(test, std::fabs(drivenLine->Rotation() - 90.0f) < 0.01f);
        // An unreadable pattern leaves the stroke solid instead of clearing it.
        drivenLine->ApplyBound("dash", "none");
        CHECK(test, drivenLine->Dashes().empty());
    }

    // Hidden and display="none" lines measure to nothing and are not hit.
    std::unique_ptr<Element> hidden = Layout(
        "<window width=\"200\" height=\"200\"><frame layout=\"none\" width=\"200\" height=\"200\">"
        "<line id=\"gone\" x1=\"0\" y1=\"0\" x2=\"80\" y2=\"0\" width=\"80\" height=\"10\" display=\"none\"/>"
        "<line id=\"away\" x=\"0\" y=\"20\" x1=\"0\" y1=\"0\" x2=\"80\" y2=\"0\" width=\"80\" height=\"10\" visible=\"false\"/>"
        "</frame></window>",
        200.0f, 200.0f);
    Element* goneLine = hidden != nullptr ? hidden->FindById("gone") : nullptr;
    Element* awayLine = hidden != nullptr ? hidden->FindById("away") : nullptr;
    CHECK(test, goneLine != nullptr && awayLine != nullptr);
    if (goneLine != nullptr && awayLine != nullptr) {
        CHECK(test, !goneLine->Visible() && !awayLine->Visible());
        const Rect box = goneLine->Bounds();
        CHECK(test, goneLine->HitTest(Point{box.x + 4.0f, box.y + 4.0f}) == nullptr);
    }

    // The compiler rejects what a line is not: no layout, no fill (it has no area to cover),
    // no radius, no overflow, and no children.
    CHECK(test, CompileUi(Source("<window><line x1=\"0\" y1=\"0\" x2=\"10\" y2=\"10\" stroke=\"#000000\" strokeWidth=\"1\"/></window>")).Ok());
    CHECK(test, CompileUi(Source("<window><line lineCap=\"round\" dash=\"4 2\"/></window>")).Ok());
    CHECK(test, !CompileUi(Source("<window><line layout=\"vertical\"/></window>")).Ok());
    CHECK(test, !CompileUi(Source("<window><line padding=\"8\"/></window>")).Ok());
    CHECK(test, !CompileUi(Source("<window><line gap=\"8\"/></window>")).Ok());
    CHECK(test, !CompileUi(Source("<window><line radius=\"8\"/></window>")).Ok());
    CHECK(test, !CompileUi(Source("<window><line overflow=\"scroll\"/></window>")).Ok());
    CHECK(test, !CompileUi(Source("<window><line fill=\"#FFFFFF\"/></window>")).Ok());
    CHECK(test, !CompileUi(Source("<window><line><frame/></line></window>")).Ok());

    Finish(test, "line", before);
}

// An arrow is a line with a head, built on the line instead of beside it. So most of what is
// checked here is the line showing through the arrow: the box, the stroke, the cap, the dash,
// the bindings, and the hit band all come from underneath, and only the head is new.
void TestArrow(TestContext& test) {
    const int before = test.failed;

    // The geometry belongs to the line: the box is the span of the endpoints, the arrow holds
    // nothing, and the line's own type still answers for it.
    std::unique_ptr<Element> plain = Layout(
        "<window width=\"400\" height=\"200\"><frame layout=\"none\" width=\"400\" height=\"200\">"
        "<arrow id=\"shaft\" x1=\"0\" y1=\"0\" x2=\"120\" y2=\"40\"/>"
        "</frame></window>",
        400.0f, 200.0f);
    Element* shaft = plain != nullptr ? plain->FindById("shaft") : nullptr;
    CHECK(test, shaft != nullptr && shaft->Kind() == NodeKind::ARROW);
    if (shaft != nullptr) {
        CHECK(test, std::fabs(shaft->Bounds().width - 120.0f) < 0.01f);
        CHECK(test, std::fabs(shaft->Bounds().height - 40.0f) < 0.01f);
        CHECK(test, shaft->Children().empty());
        // An arrow is a line, so anything that treats a line as a line still works on it.
        CHECK(test, dynamic_cast<LineElement*>(shaft) != nullptr);
        ArrowElement* arrow = dynamic_cast<ArrowElement*>(shaft);
        CHECK(test, arrow != nullptr);
        if (arrow != nullptr) {
            CHECK(test, std::fabs(arrow->HeadLength() - 12.0f) < 0.01f);
            CHECK(test, std::fabs(arrow->HeadWidth() - 12.0f) < 0.01f);
            CHECK(test, arrow->Head() == "end");
        }
    }

    // A binding or an animation aimed at the line lands on the arrow, because the arrow keeps
    // no second copy of the line that could fall out of step with it.
    std::unique_ptr<Element> driven = Layout(
        "<window width=\"200\" height=\"200\"><frame layout=\"none\" width=\"200\" height=\"200\">"
        "<arrow id=\"driven\" x1=\"0\" y1=\"0\" x2=\"60\" y2=\"0\" width=\"60\" height=\"10\" stroke=\"#000000\" strokeWidth=\"1\"/>"
        "</frame></window>",
        200.0f, 200.0f);
    ArrowElement* drivenArrow = driven != nullptr ? dynamic_cast<ArrowElement*>(driven->FindById("driven")) : nullptr;
    CHECK(test, drivenArrow != nullptr);
    if (drivenArrow != nullptr) {
        drivenArrow->ApplyBound("stroke", "#FF0000");
        CHECK(test, drivenArrow->StrokeColor().r > 0.9f && drivenArrow->StrokeColor().g < 0.1f);
        drivenArrow->ApplyBound("strokeWidth", "5");
        CHECK(test, std::fabs(drivenArrow->StrokeWidth() - 5.0f) < 0.01f);
        drivenArrow->ApplyBound("lineCap", "round");
        CHECK(test, drivenArrow->LineCap() == "round");
        drivenArrow->ApplyBound("dash", "8");
        CHECK(test, drivenArrow->Dashes().size() == 2 && std::fabs(drivenArrow->Dashes()[0] - 8.0f) < 0.01f);
        drivenArrow->ApplyBound("y2", "40");
        CHECK(test, std::fabs(drivenArrow->EndY() - 40.0f) < 0.01f);
        drivenArrow->ApplyBound("rotation", "90");
        CHECK(test, std::fabs(drivenArrow->Rotation() - 90.0f) < 0.01f);
        // The head is the only part the arrow owns, and an unreadable head value falls back
        // to the one-end head rather than to nothing at all.
        drivenArrow->ApplyBound("headLength", "20");
        CHECK(test, std::fabs(drivenArrow->HeadLength() - 20.0f) < 0.01f);
        drivenArrow->ApplyBound("headWidth", "8");
        CHECK(test, std::fabs(drivenArrow->HeadWidth() - 8.0f) < 0.01f);
        drivenArrow->ApplyBound("head", "both");
        CHECK(test, drivenArrow->Head() == "both");
        drivenArrow->ApplyBound("head", "nonsense");
        CHECK(test, drivenArrow->Head() == "end");
    }

    // The paint pass is the line's stroke plus the head laid over one of its ends: the head is
    // a closed triangle whose tip is the far endpoint, and the shaft stops at the neck so the
    // line does not blunt the point it is pointing with.
    std::unique_ptr<Element> painted = Layout(
        "<window width=\"400\" height=\"220\"><frame layout=\"none\" width=\"400\" height=\"220\">"
        "<arrow id=\"one\" x=\"10\" y=\"10\" x1=\"0\" y1=\"0\" x2=\"100\" y2=\"0\" width=\"100\" height=\"20\" stroke=\"#112233\" strokeWidth=\"2\" headLength=\"16\" headWidth=\"12\"/>"
        "<arrow id=\"two\" x=\"10\" y=\"80\" x1=\"0\" y1=\"0\" x2=\"100\" y2=\"0\" width=\"100\" height=\"20\" head=\"both\" headLength=\"10\" headWidth=\"10\"/>"
        "<arrow id=\"back\" x=\"10\" y=\"150\" x1=\"0\" y1=\"0\" x2=\"100\" y2=\"0\" width=\"100\" height=\"20\" head=\"start\" headLength=\"12\" headWidth=\"12\"/>"
        "</frame></window>",
        400.0f, 220.0f);
    Element* one = painted != nullptr ? painted->FindById("one") : nullptr;
    Element* two = painted != nullptr ? painted->FindById("two") : nullptr;
    Element* back = painted != nullptr ? painted->FindById("back") : nullptr;
    CHECK(test, one != nullptr && two != nullptr && back != nullptr);
    if (one != nullptr && two != nullptr && back != nullptr) {
        RecordingRenderer renderer;
        painted->Paint(renderer);
        const auto recordsFor = [&](const Rect& box) {
            std::vector<const PaintRecord*> found;
            for (const PaintRecord& record : renderer.records) {
                if (std::fabs(record.rect.x - box.x) < 0.01f && std::fabs(record.rect.y - box.y) < 0.01f &&
                    std::fabs(record.rect.width - box.width) < 0.01f && std::fabs(record.rect.height - box.height) < 0.01f) {
                    found.push_back(&record);
                }
            }
            return found;
        };
        // One head, one shaft under it, and the shaft is the line's stroke reaching the neck.
        const std::vector<const PaintRecord*> single = recordsFor(one->Bounds());
        CHECK(test, single.size() == 2);
        if (single.size() == 2) {
            CHECK(test, single[0]->vector && single[1]->vector);
            const std::vector<PathSample> shaftPath = SamplePath(single[0]->path);
            const std::vector<PathSample> headPath = SamplePath(single[1]->path);
            CHECK(test, shaftPath.size() >= 2 && headPath.size() >= 3);
            if (shaftPath.size() >= 2) {
                // The stroke runs from the near end to 16 short of the far end.
                CHECK(test, std::fabs(shaftPath.front().x) < 0.5f);
                CHECK(test, std::fabs(shaftPath.back().x - 84.0f) < 0.5f);
                CHECK(test, std::fabs(shaftPath.back().y - 10.0f) < 0.5f);
            }
            if (headPath.size() >= 3) {
                // The head reaches the far end the shaft deliberately stopped short of.
                float headReach = 0.0f;
                for (const PathSample& sample : headPath) {
                    headReach = std::max(headReach, sample.x);
                }
                CHECK(test, std::fabs(headReach - 100.0f) < 0.5f);
            }
        }
        // A head at each end is one stroke and two heads.
        CHECK(test, recordsFor(two->Bounds()).size() == 3);
        // A head at the start points the other way: the head is at the near end and the stroke
        // begins where its neck left off.
        const std::vector<const PaintRecord*> backwards = recordsFor(back->Bounds());
        CHECK(test, backwards.size() == 2);
        if (backwards.size() == 2) {
            const std::vector<PathSample> shaftPath = SamplePath(backwards[0]->path);
            float headReach = 100.0f;
            for (const PathSample& sample : SamplePath(backwards[1]->path)) {
                headReach = std::min(headReach, sample.x);
            }
            CHECK(test, std::fabs(headReach) < 0.5f);
            CHECK(test, shaftPath.size() >= 2 && std::fabs(shaftPath.front().x - 12.0f) < 0.5f);
        }
    }

    // An arrow is hit along the line it is built from, not in the empty half of its box, and
    // turning it turns the head with it.
    std::unique_ptr<Element> hits = Layout(
        "<window width=\"200\" height=\"200\"><frame layout=\"none\" width=\"200\" height=\"200\">"
        "<arrow id=\"diagonal\" x=\"20\" y=\"20\" x1=\"0\" y1=\"0\" x2=\"100\" y2=\"100\" width=\"100\" height=\"100\" strokeWidth=\"2\"/>"
        "<arrow id=\"turned\" x=\"0\" y=\"150\" x1=\"0\" y1=\"0\" x2=\"80\" y2=\"0\" width=\"80\" height=\"20\" rotation=\"90\"/>"
        "</frame></window>",
        200.0f, 200.0f);
    Element* diagonal = hits != nullptr ? hits->FindById("diagonal") : nullptr;
    Element* turned = hits != nullptr ? hits->FindById("turned") : nullptr;
    CHECK(test, diagonal != nullptr && turned != nullptr);
    if (diagonal != nullptr && turned != nullptr) {
        const Rect box = diagonal->Bounds();
        CHECK(test, diagonal->HitTest(Point{box.x + 50.0f, box.y + 50.0f}) == diagonal);
        CHECK(test, diagonal->HitTest(Point{box.x + 50.0f, box.y + 2.0f}) == nullptr);
        const Rect turnedBox = turned->Bounds();
        CHECK(test, turned->HitTest(Point{turnedBox.x + turnedBox.width * 0.5f, turnedBox.y + turnedBox.height * 0.5f}) == turned);
        CHECK(test, turned->HitTest(Point{turnedBox.x + 1.0f, turnedBox.y + 1.0f}) == nullptr);
    }

    // Hidden and display="none" arrows measure to nothing and paint nothing.
    std::unique_ptr<Element> hidden = Layout(
        "<window width=\"200\" height=\"200\"><frame layout=\"none\" width=\"200\" height=\"200\">"
        "<arrow id=\"gone\" x1=\"0\" y1=\"0\" x2=\"80\" y2=\"0\" width=\"80\" height=\"10\" display=\"none\"/>"
        "<arrow id=\"away\" x=\"0\" y=\"20\" x1=\"0\" y1=\"0\" x2=\"80\" y2=\"0\" width=\"80\" height=\"10\" visible=\"false\"/>"
        "</frame></window>",
        200.0f, 200.0f);
    Element* goneArrow = hidden != nullptr ? hidden->FindById("gone") : nullptr;
    Element* awayArrow = hidden != nullptr ? hidden->FindById("away") : nullptr;
    CHECK(test, goneArrow != nullptr && awayArrow != nullptr);
    if (goneArrow != nullptr && awayArrow != nullptr) {
        CHECK(test, !goneArrow->Visible() && !awayArrow->Visible());
        CHECK(test, goneArrow->HitTest(Point{goneArrow->Bounds().x + 4.0f, goneArrow->Bounds().y + 4.0f}) == nullptr);
        RecordingRenderer renderer;
        hidden->Paint(renderer);
        bool paintedArrow = false;
        for (const PaintRecord& record : renderer.records) {
            if (std::fabs(record.rect.width - 80.0f) < 0.01f) {
                paintedArrow = true;
            }
        }
        CHECK(test, !paintedArrow);
    }

    // An arrow takes everything a line takes plus the shape of its head, and it stands for a
    // line so it is refused everything a line is refused: no layout, no padding, no gap, no
    // radius, no fill, no overflow, and no children.
    CHECK(test, CompileUi(Source("<window><arrow x1=\"0\" y1=\"0\" x2=\"10\" y2=\"10\" stroke=\"#000000\" strokeWidth=\"1\"/></window>")).Ok());
    CHECK(test, CompileUi(Source("<window><arrow head=\"both\" headLength=\"14\" headWidth=\"10\" lineCap=\"round\" dash=\"4 2\"/></window>")).Ok());
    CHECK(test, !CompileUi(Source("<window><arrow layout=\"vertical\"/></window>")).Ok());
    CHECK(test, !CompileUi(Source("<window><arrow padding=\"8\"/></window>")).Ok());
    CHECK(test, !CompileUi(Source("<window><arrow gap=\"8\"/></window>")).Ok());
    CHECK(test, !CompileUi(Source("<window><arrow radius=\"8\"/></window>")).Ok());
    CHECK(test, !CompileUi(Source("<window><arrow fill=\"#FFFFFF\"/></window>")).Ok());
    CHECK(test, !CompileUi(Source("<window><arrow overflow=\"scroll\"/></window>")).Ok());
    CHECK(test, !CompileUi(Source("<window><arrow><frame/></arrow></window>")).Ok());

    Finish(test, "arrow", before);
}

// A boolean group is one shape computed from the shapes it holds, so it is checked from both ends:
// the combination itself, which is geometry and is read off areas and membership, and the element,
// which is a rectangle that draws the combined outline in its own fill instead of its own box.
void TestBoolean(TestContext& test) {
    const int before = test.failed;

    // The word a document names: the spellings that mean each of the four, and a word that means
    // nothing, which is read as a union because a union is the operation that takes nothing away.
    CHECK(test, ParseBooleanOperation("union") == BooleanOperation::UNION);
    CHECK(test, ParseBooleanOperation("UNION") == BooleanOperation::UNION);
    CHECK(test, ParseBooleanOperation("subtract") == BooleanOperation::SUBTRACT);
    CHECK(test, ParseBooleanOperation("difference") == BooleanOperation::SUBTRACT);
    CHECK(test, ParseBooleanOperation("minus") == BooleanOperation::SUBTRACT);
    CHECK(test, ParseBooleanOperation("intersect") == BooleanOperation::INTERSECT);
    CHECK(test, ParseBooleanOperation("  Intersection  ") == BooleanOperation::INTERSECT);
    CHECK(test, ParseBooleanOperation("xor") == BooleanOperation::EXCLUDE);
    CHECK(test, ParseBooleanOperation("exclusion") == BooleanOperation::EXCLUDE);
    CHECK(test, ParseBooleanOperation("") == BooleanOperation::UNION);
    CHECK(test, ParseBooleanOperation("blend") == BooleanOperation::UNION);
    CHECK(test, std::string(BooleanOperationName(BooleanOperation::UNION)) == "union");
    CHECK(test, std::string(BooleanOperationName(BooleanOperation::SUBTRACT)) == "subtract");
    CHECK(test, std::string(BooleanOperationName(BooleanOperation::INTERSECT)) == "intersect");
    CHECK(test, std::string(BooleanOperationName(BooleanOperation::EXCLUDE)) == "exclude");

    // Each shape is handed over the way it paints itself: one closed loop of corners. The two
    // squares overlap by a quarter, which is the pair every operation is read off.
    const auto square = [](float left, float top, float right, float bottom) {
        PathContour loop;
        loop.closed = true;
        loop.points = {PathSample{left, top}, PathSample{right, top}, PathSample{right, bottom}, PathSample{left, bottom}};
        return std::vector<PathContour>{loop};
    };
    const std::vector<std::vector<PathContour>> pair{square(0.0f, 0.0f, 10.0f, 10.0f), square(5.0f, 5.0f, 15.0f, 15.0f)};
    // The area the outlines cover, counted on a grid: the only oracle a computed outline has is
    // which points it says are inside it.
    const auto coveredArea = [](const std::vector<PathContour>& outlines, float extent) {
        const int steps = 400;
        const float cell = extent / static_cast<float>(steps);
        float area = 0.0f;
        for (int row = 0; row < steps; ++row) {
            for (int column = 0; column < steps; ++column) {
                const Point centre{(static_cast<float>(column) + 0.5f) * cell, (static_cast<float>(row) + 0.5f) * cell};
                if (OutlinesContain(outlines, centre)) {
                    area += cell * cell;
                }
            }
        }
        return area;
    };

    // Union keeps everything either square covers: 100 + 100, less the 25 they share.
    const std::vector<PathContour> unioned = SampleContours(CombineOutlines(pair, BooleanOperation::UNION));
    CHECK(test, unioned.size() == 1);
    CHECK(test, std::fabs(coveredArea(unioned, 20.0f) - 175.0f) < 0.5f);
    CHECK(test, OutlinesContain(unioned, Point{2.0f, 2.0f}));
    CHECK(test, OutlinesContain(unioned, Point{7.0f, 7.0f}));
    CHECK(test, OutlinesContain(unioned, Point{12.0f, 12.0f}));
    CHECK(test, !OutlinesContain(unioned, Point{12.0f, 2.0f}));
    CHECK(test, !OutlinesContain(unioned, Point{2.0f, 12.0f}));

    // Subtract takes the second square away from the first, so what is left is the part of the
    // first the second does not reach.
    const std::vector<PathContour> subtracted = SampleContours(CombineOutlines(pair, BooleanOperation::SUBTRACT));
    CHECK(test, subtracted.size() == 1);
    CHECK(test, std::fabs(coveredArea(subtracted, 20.0f) - 75.0f) < 0.5f);
    CHECK(test, OutlinesContain(subtracted, Point{2.0f, 2.0f}));
    CHECK(test, OutlinesContain(subtracted, Point{2.0f, 7.0f}));
    CHECK(test, !OutlinesContain(subtracted, Point{7.0f, 7.0f}));
    CHECK(test, !OutlinesContain(subtracted, Point{12.0f, 12.0f}));

    // Intersect keeps only what both cover, which is the quarter they share.
    const std::vector<PathContour> intersected = SampleContours(CombineOutlines(pair, BooleanOperation::INTERSECT));
    CHECK(test, intersected.size() == 1);
    CHECK(test, std::fabs(coveredArea(intersected, 20.0f) - 25.0f) < 0.5f);
    CHECK(test, OutlinesContain(intersected, Point{7.0f, 7.0f}));
    CHECK(test, !OutlinesContain(intersected, Point{2.0f, 2.0f}));
    CHECK(test, !OutlinesContain(intersected, Point{12.0f, 12.0f}));

    // Exclude keeps what an odd number of them cover, so the shared quarter drops out and the two
    // halves that are left come back as one shape with the shared part taken out of it.
    const std::vector<PathContour> excluded = SampleContours(CombineOutlines(pair, BooleanOperation::EXCLUDE));
    CHECK(test, excluded.size() == 2);
    CHECK(test, std::fabs(coveredArea(excluded, 20.0f) - 150.0f) < 1.0f);
    CHECK(test, OutlinesContain(excluded, Point{2.0f, 2.0f}));
    CHECK(test, OutlinesContain(excluded, Point{12.0f, 12.0f}));
    CHECK(test, !OutlinesContain(excluded, Point{7.0f, 7.0f}));

    // A ring drawn as two loops is a shape with a hole in it, and combining it keeps the hole: the
    // loop nested inside the other is read as the part the shape does not cover.
    std::vector<PathContour> ring = square(0.0f, 0.0f, 20.0f, 20.0f);
    ring.push_back(square(6.0f, 6.0f, 14.0f, 14.0f).front());
    const std::vector<PathContour> holed = SampleContours(CombineOutlines({ring}, BooleanOperation::UNION));
    CHECK(test, holed.size() == 2);
    CHECK(test, std::fabs(coveredArea(holed, 20.0f) - 336.0f) < 1.0f);
    CHECK(test, OutlinesContain(holed, Point{2.0f, 10.0f}));
    CHECK(test, !OutlinesContain(holed, Point{10.0f, 10.0f}));

    // A shape with no area, such as a line, covers nothing and takes no part, and a combination
    // with nothing in it is nothing at all rather than an empty loop.
    PathContour flat;
    flat.closed = true;
    flat.points = {PathSample{0.0f, 0.0f}, PathSample{10.0f, 0.0f}};
    CHECK(test, CombineOutlines({std::vector<PathContour>{flat}}, BooleanOperation::UNION).empty());
    CHECK(test, CombineOutlines({}, BooleanOperation::INTERSECT).empty());

    // The compiler knows the component, lets it hold the shapes it is made of, and refuses the
    // values that describe an outline it was not given.
    CHECK(test, FindComponent("boolean") != nullptr);
    CHECK(test, FindComponent("boolean") != nullptr && FindComponent("boolean")->acceptsChildren);
    CHECK(test, ComponentAcceptsProperty("boolean", "operation"));
    CHECK(test, ComponentAcceptsProperty("boolean", "fill"));
    CHECK(test, ComponentAcceptsProperty("boolean", "stroke"));
    CHECK(test, ComponentAcceptsProperty("boolean", "dash"));
    CHECK(test, ComponentAcceptsProperty("boolean", "width"));
    CHECK(test, !ComponentAcceptsProperty("boolean", "sides"));
    CHECK(test, !ComponentAcceptsProperty("boolean", "radius"));
    CHECK(test, !ComponentAcceptsProperty("boolean", "radiusTopLeft"));
    CHECK(test, !ComponentAcceptsProperty("boolean", "layout"));
    CHECK(test, CompileUi(Source("<window><boolean operation=\"subtract\"><rectangle width=\"40\" height=\"40\"/>"
                                "<rectangle x=\"20\" y=\"20\" width=\"40\" height=\"40\"/></boolean></window>"))
                    .Ok());
    CHECK(test, !CompileUi(Source("<window><boolean operation=\"subtract\" sides=\"5\"/></window>")).Ok());
    CHECK(test, !CompileUi(Source("<window><boolean radius=\"8\"/></window>")).Ok());

    // Every operation through a real document, so the shape that reaches the canvas is the shape
    // the operation says. Each group is a rectangle underneath, and holds two boxes that overlap.
    std::unique_ptr<Element> shapes = Layout(
        "<window width=\"400\" height=\"300\"><frame layout=\"none\" width=\"400\" height=\"300\">"
        "<boolean id=\"united\" x=\"20\" y=\"20\" width=\"100\" height=\"100\" fill=\"#0E7AFF\" operation=\"union\">"
        "<rectangle id=\"ua\" x=\"0\" y=\"0\" width=\"60\" height=\"60\"/>"
        "<rectangle id=\"ub\" x=\"40\" y=\"40\" width=\"60\" height=\"60\"/></boolean>"
        "<boolean id=\"cut\" x=\"140\" y=\"20\" width=\"100\" height=\"100\" fill=\"#FF3B30\" operation=\"subtract\">"
        "<rectangle id=\"ca\" x=\"0\" y=\"0\" width=\"100\" height=\"100\"/>"
        "<rectangle id=\"cb\" x=\"50\" y=\"50\" width=\"100\" height=\"100\"/></boolean>"
        "<boolean id=\"overlap\" x=\"260\" y=\"20\" width=\"100\" height=\"100\" fill=\"#30D158\" operation=\"intersect\">"
        "<rectangle id=\"ia\" x=\"0\" y=\"0\" width=\"100\" height=\"100\"/>"
        "<rectangle id=\"ib\" x=\"50\" y=\"50\" width=\"100\" height=\"100\"/></boolean>"
        "<boolean id=\"apart\" x=\"20\" y=\"140\" width=\"100\" height=\"100\" fill=\"#FF9F0A\" operation=\"exclude\">"
        "<rectangle id=\"ea\" x=\"0\" y=\"0\" width=\"60\" height=\"60\"/>"
        "<rectangle id=\"eb\" x=\"40\" y=\"40\" width=\"60\" height=\"60\"/></boolean>"
        "<boolean id=\"holed\" x=\"140\" y=\"140\" width=\"100\" height=\"100\" fill=\"#1D1D1F\" stroke=\"#0E7AFF\" "
        "strokeWidth=\"2\" dash=\"6 3\" operation=\"subtract\">"
        "<rectangle id=\"ha\" x=\"0\" y=\"0\" width=\"100\" height=\"100\" radius=\"12\"/>"
        "<ellipse id=\"hb\" x=\"30\" y=\"30\" width=\"40\" height=\"40\"/></boolean>"
        "<boolean id=\"empty\" x=\"260\" y=\"140\" width=\"80\" height=\"80\" fill=\"#FF3B30\"/>"
        "<boolean id=\"unknown\" x=\"20\" y=\"260\" operation=\"blend\">"
        "<rectangle id=\"xa\" width=\"50\" height=\"50\"/>"
        "<rectangle id=\"xb\" x=\"30\" y=\"30\" width=\"50\" height=\"50\"/></boolean>"
        "</frame></window>",
        400.0f, 300.0f);
    CHECK(test, shapes != nullptr);
    if (shapes != nullptr) {
        Element* united = shapes->FindById("united");
        Element* cut = shapes->FindById("cut");
        Element* empty = shapes->FindById("empty");
        Element* unknown = shapes->FindById("unknown");
        Element* holedNode = shapes->FindById("holed");
        CHECK(test, united != nullptr && cut != nullptr && empty != nullptr && unknown != nullptr && holedNode != nullptr);
        CHECK(test, united->Kind() == NodeKind::BOOLEAN);
        CHECK(test, dynamic_cast<RectangleElement*>(united) != nullptr);
        BooleanElement* typed = dynamic_cast<BooleanElement*>(united);
        CHECK(test, typed != nullptr && typed->Operation() == BooleanOperation::UNION);
        // A group that was given no size of its own is exactly as big as the shapes it holds.
        CHECK(test, std::fabs(united->Bounds().width - 100.0f) < 0.01f && std::fabs(united->Bounds().height - 100.0f) < 0.01f);
        CHECK(test, std::fabs(united->Bounds().x - 20.0f) < 0.01f && std::fabs(united->Bounds().y - 20.0f) < 0.01f);
        CHECK(test, dynamic_cast<BooleanElement*>(cut)->Operation() == BooleanOperation::SUBTRACT);
        CHECK(test, dynamic_cast<BooleanElement*>(shapes->FindById("overlap"))->Operation() == BooleanOperation::INTERSECT);
        CHECK(test, dynamic_cast<BooleanElement*>(shapes->FindById("apart"))->Operation() == BooleanOperation::EXCLUDE);
        CHECK(test, dynamic_cast<BooleanElement*>(holedNode)->Operation() == BooleanOperation::SUBTRACT);
        // A word that means nothing reads as a union rather than as no operation at all.
        CHECK(test, dynamic_cast<BooleanElement*>(unknown)->Operation() == BooleanOperation::UNION);
        CHECK(test, dynamic_cast<BooleanElement*>(empty)->Operation() == BooleanOperation::UNION);

        // One paint pass draws each group once, as the outline it computed, and draws none of the
        // shapes it holds: their fills are what they looked like before they were combined.
        RecordingRenderer renderer;
        shapes->Paint(renderer);
        const auto drawnPath = [&](const char* id) {
            Element* node = shapes->FindById(id);
            if (node == nullptr) {
                return std::string();
            }
            const PaintRecord* record = PaintRecordFor(renderer, node->Bounds());
            return record != nullptr && record->vector ? record->path : std::string();
        };
        const auto covers = [&](const char* id, float x, float y) {
            return OutlinesContain(SampleContours(drawnPath(id)), Point{x, y});
        };
        CHECK(test, !drawnPath("united").empty());
        CHECK(test, SampleContours(drawnPath("united")).size() == 1);
        CHECK(test, covers("united", 2.0f, 2.0f));
        CHECK(test, covers("united", 95.0f, 95.0f));
        CHECK(test, !covers("united", 95.0f, 5.0f));
        CHECK(test, !covers("united", 5.0f, 95.0f));
        CHECK(test, covers("cut", 20.0f, 20.0f));
        CHECK(test, !covers("cut", 80.0f, 80.0f));
        CHECK(test, covers("overlap", 60.0f, 60.0f));
        CHECK(test, !covers("overlap", 20.0f, 20.0f));
        // Exclude leaves a hole where the two overlapped, so it comes back as two loops.
        CHECK(test, SampleContours(drawnPath("apart")).size() == 2);
        CHECK(test, covers("apart", 20.0f, 20.0f));
        CHECK(test, covers("apart", 85.0f, 85.0f));
        CHECK(test, !covers("apart", 50.0f, 50.0f));
        CHECK(test, covers("unknown", 2.0f, 2.0f));
        CHECK(test, !covers("unknown", 95.0f, 95.0f));
        CHECK(test, !covers("unknown", 95.0f, 5.0f));
        // The shapes a group holds are read for their outlines and nothing else, so no draw says
        // it is one of them, and a group holding nothing draws nothing at all.
        CHECK(test, PaintRecordFor(renderer, shapes->FindById("ua")->Bounds()) == nullptr);
        CHECK(test, PaintRecordFor(renderer, shapes->FindById("ca")->Bounds()) == nullptr);
        CHECK(test, drawnPath("empty").empty());
        CHECK(test, PaintRecordFor(renderer, empty->Bounds()) == nullptr);
        // A stroke and a pattern reach the combined outline the way they reach any other shape.
        const PaintRecord* holedPaint = PaintRecordFor(renderer, holedNode->Bounds());
        CHECK(test, holedPaint != nullptr);
        if (holedPaint != nullptr) {
            CHECK(test, holedPaint->vector && holedPaint->dashes.size() == 2);
            if (holedPaint->dashes.size() == 2) {
                CHECK(test, std::fabs(holedPaint->dashes[0] - 6.0f) < 0.01f && std::fabs(holedPaint->dashes[1] - 3.0f) < 0.01f);
            }
        }

        // A part of a shape the operation took away is not part of the group, so a click there
        // belongs to whatever stands behind it, and a group holding nothing is never hit.
        CHECK(test, united->HitTest(Point{25.0f, 25.0f}) != nullptr);
        CHECK(test, united->HitTest(Point{135.0f, 25.0f}) == nullptr);
        CHECK(test, cut->HitTest(Point{160.0f, 40.0f}) != nullptr);
        CHECK(test, cut->HitTest(Point{210.0f, 90.0f}) == nullptr);
        CHECK(test, empty->HitTest(Point{300.0f, 180.0f}) == nullptr);
    }

    // The editor's default is a group with two shapes already in it, because a group holding
    // nothing is a shape with nothing to draw, and the defaults it writes have to compile.
    DesignDocument design;
    CHECK(test, design.LoadText("<window width=\"800\" height=\"600\"><frame name=\"Lead\"/></window>", "memory.xml"));
    std::vector<int> made;
    CHECK(test, design.InsertChild({0}, "boolean", made));
    CHECK(test, design.SetAttribute(made, "id", "bornGroup"));
    const XmlNode* node = design.NodeAt(made);
    CHECK(test, node != nullptr && node->name == "boolean");
    if (node != nullptr) {
        CHECK(test, node->children.size() == 2);
        CHECK(test, node->children[0].name == "rectangle" && node->children[1].name == "rectangle");
    }
    const CompileResult madeResult = CompileUi(Source(design.PreviewText()));
    CHECK(test, madeResult.Ok());
    if (madeResult.Ok()) {
        std::unique_ptr<Element> born = Instantiate(*madeResult.root, {});
        CHECK(test, born != nullptr);
        if (born != nullptr) {
            FakeMeasurer measurer;
            born->SetViewport(800.0f, 600.0f);
            born->Measure(measurer, 800.0f, 600.0f, true);
            born->Arrange(Rect{0.0f, 0.0f, 800.0f, 600.0f});
            Element* group = born->FindById("bornGroup");
            CHECK(test, group != nullptr && group->Kind() == NodeKind::BOOLEAN);
            if (group != nullptr) {
                BooleanElement* typed = dynamic_cast<BooleanElement*>(group);
                CHECK(test, typed != nullptr && typed->Operation() == BooleanOperation::UNION);
                // The two shapes a new group comes with cover a 100 by 100 box, which is the box
                // the group declares, so a group drawn at its own size is drawn where it says.
                CHECK(test, std::fabs(group->Bounds().width - 100.0f) < 0.01f);
                CHECK(test, std::fabs(group->Bounds().height - 100.0f) < 0.01f);
            }
        }
    }

    // The canvas writes the size the drag ended at, and a group given a size of its own keeps it.
    CHECK(test, design.SetAttribute(made, "id", "drawnShape"));
    CHECK(test, design.PlaceNode(made, 40.0f, 60.0f, 180.0f, 90.0f));
    const CompileResult placedResult = CompileUi(Source(design.PreviewText()));
    CHECK(test, placedResult.Ok());
    if (placedResult.Ok()) {
        std::unique_ptr<Element> placed = Instantiate(*placedResult.root, {});
        CHECK(test, placed != nullptr);
        if (placed != nullptr) {
            FakeMeasurer measurer;
            placed->SetViewport(800.0f, 600.0f);
            placed->Measure(measurer, 800.0f, 600.0f, true);
            placed->Arrange(Rect{0.0f, 0.0f, 800.0f, 600.0f});
            Element* group = placed->FindById("drawnShape");
            CHECK(test, group != nullptr);
            if (group != nullptr) {
                CHECK(test, std::fabs(group->Bounds().width - 180.0f) < 0.01f);
                CHECK(test, std::fabs(group->Bounds().height - 90.0f) < 0.01f);
            }
        }
    }

    Finish(test, "boolean", before);
}

void TestBoards(TestContext& test) {
    const int before = test.failed;
    std::unique_ptr<Element> root = Layout(
        "<window width=\"400\" height=\"300\">"
        "<frame id=\"first\" width=\"100\" height=\"80\"/>"
        "<frame id=\"board\" position=\"absolute\" x=\"220\" y=\"40\" width=\"90\" height=\"50\"/>"
        "<frame id=\"next\" width=\"70\" height=\"30\"/>"
        "</window>",
        400.0f, 300.0f);
    CHECK(test, root != nullptr);
    CHECK(test, std::fabs(root->FindById("first")->Bounds().x) < 0.01f);
    CHECK(test, std::fabs(root->FindById("first")->Bounds().y) < 0.01f);
    CHECK(test, std::fabs(root->FindById("board")->Bounds().x - 220.0f) < 0.01f);
    CHECK(test, std::fabs(root->FindById("board")->Bounds().y - 40.0f) < 0.01f);
    CHECK(test, std::fabs(root->FindById("next")->Bounds().y - 80.0f) < 0.01f);
    Finish(test, "boards", before);
}

void TestDesignDocumentEditsXml(TestContext& test) {
    const int before = test.failed;
    DesignDocument document;
    CHECK(test, document.LoadText("<window width=\"800\" height=\"600\"><frame name=\"Hello\"/></window>", "memory.xml"));
    CHECK(test, document.TextSynced());
    CHECK(test, document.Root() != nullptr && document.Root()->name == "screens");
    std::vector<int> path{0, 0};
    CHECK(test, document.SetAttribute(path, "name", "Designed"));
    CHECK(test, document.Text().find("Designed") != std::string::npos);
    CHECK(test, document.Text().find("<screens") == std::string::npos);
    std::vector<int> inserted;
    CHECK(test, document.InsertChild({0}, "frame", inserted));
    CHECK(test, document.NodeAt(inserted) != nullptr);
    CHECK(test, document.NodeAt(inserted)->name == "frame");
    CHECK(test, document.DeleteNode(inserted));

    // A component the editor adds is born in the shape the tool that draws it needs, and an SVG is
    // born with its art written inside it rather than naming a file. That is what makes every path
    // of a new SVG a layer of the document, and it keeps the art of a document the editor wrote
    // editable path by path. A node that names a file instead is one attribute away.
    std::vector<int> svgPath;
    CHECK(test, document.InsertChild({0}, "svg", svgPath));
    const XmlNode* svg = document.NodeAt(svgPath);
    CHECK(test, svg != nullptr && svg->name == "svg");
    if (svg != nullptr) {
        CHECK(test, svg->children.size() == 1 && svg->children.front().name == "path");
    }
    CHECK(test, document.Text().find("source=") == std::string::npos);
    // The path of the art is a node of the tree like any other, so the editor can name it, move it,
    // and recolour it without knowing anything about where the art came from.
    std::vector<int> artPath = svgPath;
    artPath.push_back(0);
    CHECK(test, document.SetAttribute(artPath, "name", "Artwork"));
    CHECK(test, document.SetAttribute(artPath, "stroke", "#FF375F"));
    CHECK(test, document.Text().find("Artwork") != std::string::npos);
    // What the editor produced is a document the compiler accepts, so a new SVG is drawable the
    // moment it is added.
    CHECK(test, CompileUi(Source(document.Text())).Ok());
    CHECK(test, document.DeleteNode(svgPath));
    Finish(test, "design document", before);
}

void TestDesignDocumentGroups(TestContext& test) {
    const int before = test.failed;
    DesignDocument document;
    CHECK(test, document.LoadText("<window width=\"800\" height=\"600\"><frame name=\"Lead\"/></window>", "memory.xml"));
    CHECK(test, document.TextSynced());

    // Three siblings under the window, so a group has something to combine and something to
    // leave alone. The group takes the slot of the first node it wraps.
    std::vector<int> first;
    std::vector<int> second;
    CHECK(test, document.InsertChild({0}, "text", first));
    CHECK(test, document.InsertChild({0}, "frame", second));
    const XmlNode* window = document.NodeAt({0});
    CHECK(test, window != nullptr && window->children.size() == 3);

    std::vector<int> groupPath;
    CHECK(test, document.GroupNodes({first, second}, groupPath));
    CHECK(test, groupPath.size() == 2);
    const XmlNode* group = document.NodeAt(groupPath);
    CHECK(test, group != nullptr && group->name == "group");
    if (group != nullptr) {
        CHECK(test, group->children.size() == 2);
        CHECK(test, group->children[0].name == "text");
        CHECK(test, group->children[1].name == "frame");
    }
    window = document.NodeAt({0});
    CHECK(test, window != nullptr && window->children.size() == 2);
    CHECK(test, document.Text().find("<group") != std::string::npos);

    // Grouping one node is allowed, and it still produces a real group.
    std::vector<int> singlePath;
    CHECK(test, document.GroupNodes({{0, 0}}, singlePath));
    const XmlNode* single = document.NodeAt(singlePath);
    CHECK(test, single != nullptr && single->name == "group" && single->children.size() == 1);
    CHECK(test, document.UngroupNode(singlePath));

    // Ungroup lifts the children back into the slot the group occupied, in order.
    CHECK(test, document.UngroupNode(groupPath));
    const XmlNode* lifted = document.NodeAt(groupPath);
    CHECK(test, lifted != nullptr && lifted->name == "text");
    window = document.NodeAt({0});
    CHECK(test, window != nullptr && window->children.size() == 3);
    CHECK(test, document.Text().find("<group") == std::string::npos);

    // Only siblings can be combined, because a group has to take one parent slot.
    std::vector<int> rejected;
    CHECK(test, !document.GroupNodes({{0, 0}, {0}}, rejected));
    CHECK(test, !document.GroupNodes({}, rejected));
    CHECK(test, !document.GroupNodes({{0, 9}}, rejected));
    CHECK(test, document.Text().find("<group") == std::string::npos);

    // Ungrouping only lifts a group. Every other node stays where it is.
    CHECK(test, !document.UngroupNode({0, 0}));
    CHECK(test, !document.UngroupNode({}));
    CHECK(test, !document.UngroupNode({0, 99}));
    Finish(test, "design document groups", before);
}

void TestCompilerSurface(TestContext& test) {
    const int before = test.failed;
    const CompileResult compiled = CompileUi(Source(
        "<window><tokens><color name=\"primary\" value=\"#2563EB\"/><spacing name=\"md\" value=\"16\"/><radius name=\"md\" value=\"8\"/></tokens>"
        "<style name=\"button\" background=\"#111111\" radius=\"8\"><state name=\"hover\" background=\"#222222\"/>"
        "<state name=\"loading\" background=\"#333333\"/></style>"
        "<animation name=\"pulse\" trigger=\"hover\" property=\"opacity\" easing=\"easeOut\"><keyframe time=\"0%\" opacity=\"1\"/></animation>"
        "<animate target=\"title\" property=\"scale\" from=\"1\" to=\"1.05\" duration=\"150ms\" easing=\"easeOut\" trigger=\"onHover\"/>"
        "<frame id=\"card\" style=\"button\" background=\"$primary\" padding=\"$spacing.md\" radius=\"$radius.md\" position=\"sticky\" shrink=\"1\" "
        "onClick=\"set(user.loggedIn, true); navigate('/dashboard');\">"
        "<text text=\"Hello\" fontFamily=\"Segoe UI\" fontStyle=\"italic\" textAlign=\"justify\" verticalAlign=\"center\" textTransform=\"uppercase\" "
        "underline=\"true\" direction=\"ltr\" writingMode=\"horizontal\"/>"
        "<image source=\"photo.png\" fit=\"cover\" flipX=\"true\"/>"
        "<path d=\"M0 0 L10 0 Z\" fill=\"#000000\" lineCap=\"round\" dash=\"2 2\"/>"
        "<line x2=\"10\" lineCap=\"butt\" dashGap=\"2\"/>"
        "<group rotation=\"10\" scale=\"1\" visibility=\"visible\"/>"
        "<scroll direction=\"scrollY\" smoothScrolling=\"true\" scrollbar=\"auto\"/>"
        "</frame></window>"));
    CHECK(test, compiled.Ok());
    if (compiled.root.has_value()) {
        const IrNode* card = nullptr;
        for (const IrNode& child : compiled.root->children) {
            if (child.id == "card") {
                card = &child;
            }
        }
        CHECK(test, card != nullptr);
        if (card != nullptr) {
            const UiValue* background = FindProperty(*card, "background");
            CHECK(test, background != nullptr && background->color.r > 0.1f);
            const UiValue* click = FindProperty(*card, "onClick");
            CHECK(test, click != nullptr && click->kind == TypeKind::EVENT_HANDLER);
            CHECK(test, card->visual.used);
            CHECK(test, card->visual.extraPhases.count("loading") == 1);
        }
    }

    // Every tool that draws a shape names a component the compiler knows, and every tool has a
    // name, so the editor can only ever insert a node a document can hold. A tool whose component
    // is not in the catalog would draw a shape the editor cannot compile.
    const EditorTool tools[] = {EditorTool::SELECT,     EditorTool::FRAME,         EditorTool::RECTANGLE,     EditorTool::ELLIPSE,
                                EditorTool::POLYGON,    EditorTool::STAR,          EditorTool::TEXT,          EditorTool::IMAGE,
                                EditorTool::SVG,        EditorTool::LINE,          EditorTool::ARROW,         EditorTool::BOOLEAN,
                                EditorTool::COMPONENT,  EditorTool::COMPONENT_SET, EditorTool::INSTANCE,      EditorTool::SLOT,
                                EditorTool::EMBED,      EditorTool::LINK,          EditorTool::HAND,          EditorTool::ZOOM};
    for (const EditorTool tool : tools) {
        CHECK(test, std::string(EditorToolName(tool)).size() > 0);
        if (const char* component = EditorToolComponent(tool)) {
            CHECK(test, FindComponent(component) != nullptr);
            CHECK(test, ComponentAcceptsProperty(component, "width"));
        }
    }
    // A shape tool draws its node by name, and every tool that draws one names the node it draws. The
    // four nodes of the component system are drawn by tools like any other node, so they name the
    // elements the compiler knows rather than being reached only through the library panel.
    CHECK(test, std::string(EditorToolComponent(EditorTool::COMPONENT)) == "component");
    CHECK(test, std::string(EditorToolComponent(EditorTool::COMPONENT_SET)) == "componentSet");
    CHECK(test, std::string(EditorToolComponent(EditorTool::INSTANCE)) == "instance");
    CHECK(test, std::string(EditorToolComponent(EditorTool::SLOT)) == "slot");
    CHECK(test, std::string(EditorToolComponent(EditorTool::EMBED)) == "embed");
    CHECK(test, std::string(EditorToolComponent(EditorTool::LINK)) == "link");
    // The four nodes of the component system are reached through the command list rather than by keys
    // of their own, because every letter one could answer to already draws a shape or acts on the
    // view, and a key given to one of them would take it away from what it already does.
    CHECK(test, std::string(EditorToolShortcut(EditorTool::COMPONENT)).empty());
    CHECK(test, std::string(EditorToolShortcut(EditorTool::COMPONENT_SET)).empty());
    CHECK(test, std::string(EditorToolShortcut(EditorTool::INSTANCE)).empty());
    CHECK(test, std::string(EditorToolShortcut(EditorTool::SLOT)).empty());
    CHECK(test, std::string(EditorToolShortcut(EditorTool::EMBED)).empty());
    CHECK(test, std::string(EditorToolShortcut(EditorTool::LINK)).empty());
    // No two tools answer to one key, so a key draws the one tool it names.
    for (const EditorTool tool : tools) {
        const std::string shortcut = EditorToolShortcut(tool);
        if (shortcut.empty()) {
            continue;
        }
        for (const EditorTool other : tools) {
            if (other != tool) {
                CHECK(test, shortcut != EditorToolShortcut(other));
            }
        }
    }
    Finish(test, "compiler surface", before);
}

bool EncodeBitmap(const GUID& format, std::vector<unsigned char>& bytes, int width = 1, int height = 1) {
    const HRESULT start = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    Microsoft::WRL::ComPtr<IWICImagingFactory> factory;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(factory.GetAddressOf())))) {
        if (start == S_OK) {
            CoUninitialize();
        }
        return false;
    }
    Microsoft::WRL::ComPtr<IStream> stream;
    if (FAILED(CreateStreamOnHGlobal(nullptr, TRUE, stream.GetAddressOf()))) {
        if (start == S_OK) {
            CoUninitialize();
        }
        return false;
    }
    Microsoft::WRL::ComPtr<IWICBitmapEncoder> encoder;
    if (FAILED(factory->CreateEncoder(format, nullptr, encoder.GetAddressOf())) || FAILED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache))) {
        if (start == S_OK) {
            CoUninitialize();
        }
        return false;
    }
    Microsoft::WRL::ComPtr<IWICBitmapFrameEncode> frame;
    if (FAILED(encoder->CreateNewFrame(frame.GetAddressOf(), nullptr)) || FAILED(frame->Initialize(nullptr))) {
        if (start == S_OK) {
            CoUninitialize();
        }
        return false;
    }
    frame->SetSize(static_cast<UINT>(width), static_cast<UINT>(height));
    WICPixelFormatGUID pixel = GUID_WICPixelFormat32bppRGBA;
    frame->SetPixelFormat(&pixel);
    std::vector<unsigned char> pixelBytes(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u, 0);
    for (std::size_t at = 0; at < pixelBytes.size(); at += 4) {
        pixelBytes[at] = 255;
        pixelBytes[at + 3] = 255;
    }
    if (FAILED(frame->WritePixels(static_cast<UINT>(height), static_cast<UINT>(width) * 4u,
                                  static_cast<UINT>(pixelBytes.size()), pixelBytes.data())) ||
        FAILED(frame->Commit()) || FAILED(encoder->Commit())) {
        if (start == S_OK) {
            CoUninitialize();
        }
        return false;
    }
    HGLOBAL memory = nullptr;
    GetHGlobalFromStream(stream.Get(), &memory);
    const unsigned char* data = static_cast<const unsigned char*>(GlobalLock(memory));
    const SIZE_T size = GlobalSize(memory);
    if (data != nullptr && size > 0) {
        bytes.assign(data, data + size);
    }
    if (data != nullptr) {
        GlobalUnlock(memory);
    }
    encoder.Reset();
    frame.Reset();
    stream.Reset();
    factory.Reset();
    if (start == S_OK) {
        CoUninitialize();
    }
    return !bytes.empty();
}

// Whether this machine's own image reader can turn an AVIF into pixels. Windows reads the container
// itself and draws on the AV1 codec that ships separately beside it, so the answer is a fact about
// the machine rather than about the file: a file that reads as eight by eight can have no pixels in
// it anywhere at all on a machine that never installed that codec.
bool PlatformDecodesAvif(const unsigned char* data, int size) {
    HRESULT start = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(start)) {
        // The thread is already in an apartment of another kind, which is a thread COM is ready on.
        start = S_FALSE;
    }
    Microsoft::WRL::ComPtr<IWICImagingFactory> factory;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(factory.GetAddressOf()));
    if (SUCCEEDED(hr)) {
        Microsoft::WRL::ComPtr<IStream> stream;
        ULONG written = 0;
        hr = CreateStreamOnHGlobal(nullptr, TRUE, stream.GetAddressOf());
        if (SUCCEEDED(hr)) {
            stream->Write(data, static_cast<ULONG>(size), &written);
            LARGE_INTEGER zero{};
            stream->Seek(zero, STREAM_SEEK_SET, nullptr);
        }
        Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder;
        if (SUCCEEDED(hr)) {
            hr = factory->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnDemand, decoder.GetAddressOf());
        }
        Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame;
        if (SUCCEEDED(hr)) {
            hr = decoder->GetFrame(0, frame.GetAddressOf());
        }
        UINT width = 0;
        UINT height = 0;
        if (SUCCEEDED(hr)) {
            hr = frame->GetSize(&width, &height);
        }
        Microsoft::WRL::ComPtr<IWICFormatConverter> converter;
        if (SUCCEEDED(hr)) {
            hr = factory->CreateFormatConverter(converter.GetAddressOf());
        }
        if (SUCCEEDED(hr)) {
            hr = converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0.0,
                                       WICBitmapPaletteTypeCustom);
        }
        if (SUCCEEDED(hr) && width > 0 && height > 0) {
            // The whole picture is asked for, because a copy of part of it into a buffer smaller
            // than the frame is a copy the platform refuses on the size of the buffer rather than
            // on what it can read.
            const UINT stride = width * 4u;
            std::vector<unsigned char> pixels(static_cast<std::size_t>(stride) * height, 0);
            hr = converter->CopyPixels(nullptr, stride, stride * height, pixels.data());
        } else {
            hr = E_FAIL;
        }
    }
    if (start == S_OK) {
        CoUninitialize();
    }
    return SUCCEEDED(hr);
}

void WriteLe32(std::vector<unsigned char>& bytes, std::size_t at, unsigned int value) {
    bytes[at] = static_cast<unsigned char>(value & 0xFFu);
    bytes[at + 1] = static_cast<unsigned char>((value >> 8) & 0xFFu);
    bytes[at + 2] = static_cast<unsigned char>((value >> 16) & 0xFFu);
    bytes[at + 3] = static_cast<unsigned char>((value >> 24) & 0xFFu);
}

// A two by two bitmap of four solid colours, written the way a bitmap is written rather than encoded
// by the platform: a file header, a header that says how wide and how deep, and the rows bottom up,
// each one padded out to a whole number of four-byte words. Building it here is what makes the test
// about the reading of the file rather than about the writer that made it.
std::vector<unsigned char> MakeBitmap() {
    const int rowBytes = 8;
    std::vector<unsigned char> bytes(static_cast<std::size_t>(14 + 40 + rowBytes * 2), 0);
    bytes[0] = 'B';
    bytes[1] = 'M';
    WriteLe32(bytes, 2, static_cast<unsigned int>(bytes.size()));
    WriteLe32(bytes, 10, 54);
    WriteLe32(bytes, 14, 40);
    WriteLe32(bytes, 18, 2);
    WriteLe32(bytes, 22, 2);
    bytes[26] = 1;
    bytes[28] = 24;
    // The row written first is the bottom one, so blue then white, and the row after it is the top,
    // so red then green. A pixel is written blue first, which is the order every image is handed
    // over in.
    const unsigned char bottom[6] = {255, 0, 0, 255, 255, 255};
    const unsigned char top[6] = {0, 0, 255, 0, 255, 0};
    std::memcpy(bytes.data() + 54, bottom, 6);
    std::memcpy(bytes.data() + 54 + rowBytes, top, 6);
    return bytes;
}

// A bare bitmap the way an icon carries one: no file header of its own, and a height that counts
// the colours and the stencil after them as though they were one picture. The stencil marks the top
// left pixel, which is the red one, as a pixel that is not there.
std::vector<unsigned char> MakeBitmapPicture() {
    const int rowBytes = 8;
    const int maskRowBytes = 4;
    std::vector<unsigned char> payload(static_cast<std::size_t>(40 + rowBytes * 2 + maskRowBytes * 2), 0);
    WriteLe32(payload, 0, 40);
    WriteLe32(payload, 4, 2);
    WriteLe32(payload, 8, 4);
    payload[12] = 1;
    payload[14] = 24;
    const unsigned char bottom[6] = {255, 0, 0, 255, 255, 255};
    const unsigned char top[6] = {0, 0, 255, 0, 255, 0};
    std::memcpy(payload.data() + 40, bottom, 6);
    std::memcpy(payload.data() + 40 + rowBytes, top, 6);
    payload[40 + rowBytes * 2 + maskRowBytes] = 0x80;
    return payload;
}

// An icon carrying one picture, which is a PNG inside rather than a bitmap, since that is the form
// an icon is written in when it is written by a program rather than by hand.
std::vector<unsigned char> MakeIcon(const std::vector<unsigned char>& picture) {
    std::vector<unsigned char> bytes(static_cast<std::size_t>(6 + 16) + picture.size(), 0);
    bytes[2] = 1;
    bytes[4] = 1;
    bytes[6] = 1;
    bytes[7] = 1;
    WriteLe32(bytes, 14, static_cast<unsigned int>(picture.size()));
    WriteLe32(bytes, 18, 22);
    std::memcpy(bytes.data() + 22, picture.data(), picture.size());
    return bytes;
}

void TestImage(TestContext& test) {
    const int before = test.failed;

    // The pixels a picture is drawn from, checked on a buffer rather than through a device: the crop
    // decides which pixels are read, the look decides what they become, and the two mirrors decide
    // the order they come out in. One row of two pixels, blue first the way every image is handed
    // over, and the second one twice as bright as the first.
    const unsigned char row[8] = {10, 20, 30, 255, 200, 100, 50, 255};
    std::vector<unsigned char> pixels;
    int width = 0;
    int height = 0;
    AdjustImage(row, 2, 1, false, ImageCrop{}, ImageLook{}, pixels, width, height);
    CHECK(test, width == 2 && height == 1 && pixels.size() == 8 && pixels[0] == 10 && pixels[4] == 200);

    ImageLook mirrored;
    mirrored.flipX = true;
    AdjustImage(row, 2, 1, false, ImageCrop{}, mirrored, pixels, width, height);
    CHECK(test, width == 2 && pixels[0] == 200 && pixels[2] == 50 && pixels[4] == 10 && pixels[6] == 30);
    mirrored.flipX = false;
    mirrored.flipY = true;
    AdjustImage(row, 2, 1, false, ImageCrop{}, mirrored, pixels, width, height);
    // One row mirrored down is the same row: the pixels of the row are the row.
    CHECK(test, width == 2 && pixels[0] == 10 && pixels[4] == 200);

    ImageCrop right;
    right.x = 1;
    right.width = 1;
    right.height = 1;
    AdjustImage(row, 2, 1, false, right, ImageLook{}, pixels, width, height);
    CHECK(test, width == 1 && height == 1 && pixels.size() == 4 && pixels[0] == 200 && pixels[1] == 100 && pixels[2] == 50);

    // A crop written past the edge of a picture is the part of the picture that is there, because a
    // region nobody kept cannot be the whole of what is drawn.
    ImageCrop beyond;
    beyond.x = 5;
    beyond.y = 5;
    beyond.width = 10;
    beyond.height = 10;
    AdjustImage(row, 2, 1, false, beyond, ImageLook{}, pixels, width, height);
    CHECK(test, width == 1 && height == 1 && pixels[0] == 200);

    // A wash of red at half green and a quarter blue, twice as bright. The first pixel is ten, twenty
    // and thirty: a tenth of blue, a fifth of green and a tenth of red.
    ImageLook washed;
    washed.tinted = true;
    washed.tint = *ParseColor("#FF8040");
    washed.brightness = 2.0f;
    AdjustImage(row, 2, 1, false, ImageCrop{}, washed, pixels, width, height);
    CHECK(test, pixels[0] == 5 && pixels[1] == 20 && pixels[2] == 60 && pixels[3] == 255);

    // No saturation at all leaves every channel at the brightness the eye sees the colour at.
    ImageLook grey;
    grey.saturation = 0.0f;
    AdjustImage(row, 2, 1, false, ImageCrop{}, grey, pixels, width, height);
    CHECK(test, pixels[0] == 21 && pixels[1] == 21 && pixels[2] == 21);

    // A pixel that is not fully opaque is worked on as the colour it stands for rather than as the
    // numbers in the buffer. Half a red, with its alpha already folded in, is a red: pulling its
    // contrast towards the middle pulls a full red there, and the channel comes back with the alpha
    // folded in again, so half a red that was softened is half of a softened red.
    const unsigned char folded[8] = {0, 0, 128, 128, 255, 255, 255, 64};
    ImageLook softer;
    softer.contrast = 0.5f;
    AdjustImage(folded, 2, 1, true, ImageCrop{}, softer, pixels, width, height);
    CHECK(test, pixels[0] == 32 && pixels[1] == 32 && pixels[2] == 96 && pixels[3] == 128);
    // A buffer that is premultiplied already and a look that changes nothing is handed on as it was.
    AdjustImage(folded, 2, 1, true, ImageCrop{}, ImageLook{}, pixels, width, height);
    CHECK(test, pixels.size() == 8 && pixels[0] == 0 && pixels[2] == 128 && pixels[3] == 128 && pixels[4] == 255 &&
                    pixels[7] == 64);
    // A straight buffer is handed over with the alpha folded in, which is the form the platform
    // composites: half of a full red is what is drawn, and no channel is brighter than its alpha.
    const unsigned char translucent[8] = {0, 0, 255, 128, 10, 20, 30, 0};
    AdjustImage(translucent, 2, 1, false, ImageCrop{}, ImageLook{}, pixels, width, height);
    CHECK(test, pixels[0] == 0 && pixels[2] == 128 && pixels[3] == 128 && pixels[4] == 0 && pixels[5] == 0 && pixels[6] == 0 &&
                    pixels[7] == 0);

    // A picture drawn the way the file held it is the key it always had; a picture drawn any other
    // way has a key of its own, which is what keeps two looks from sharing one cached image.
    CHECK(test, ImageLookName(ImageCrop{}, ImageLook{}).empty());
    ImageLook tintedOnce;
    tintedOnce.tinted = true;
    tintedOnce.tint = *ParseColor("#FF8040");
    CHECK(test, !ImageLookName(ImageCrop{}, mirrored).empty());
    CHECK(test, ImageLookName(ImageCrop{}, mirrored) != ImageLookName(ImageCrop{}, tintedOnce));
    ImageLook brighter;
    brighter.brightness = 1.5f;
    CHECK(test, ImageLookName(ImageCrop{}, tintedOnce) != ImageLookName(ImageCrop{}, brighter));
    CHECK(test, !ImageLookName(right, ImageLook{}).empty());

    // A picture is read from a file, so the tests write one: a single pixel, which is enough to tell
    // the size a node hugs from the size it was given.
    std::vector<unsigned char> encoded;
    if (!EncodeBitmap(GUID_ContainerFormatPng, encoded)) {
        CHECK(test, false);
        Finish(test, "image", before);
        return;
    }
    const std::filesystem::path picturePath = std::filesystem::temp_directory_path() / "runtime-image-element-test.png";
    {
        std::ofstream file(picturePath, std::ios::binary);
        file.write(reinterpret_cast<const char*>(encoded.data()), static_cast<std::streamsize>(encoded.size()));
    }
    CHECK(test, std::filesystem::exists(picturePath));
    const std::string photo = picturePath.string();
    // A picture larger than the boxes it is drawn into, so a picture that is shrunk rather than
    // magnified can be read here as well: the corners of a box can only be written into a picture
    // that has pixels small enough to hold them.
    std::vector<unsigned char> encodedWide;
    CHECK(test, EncodeBitmap(GUID_ContainerFormatPng, encodedWide, 200, 120));
    const std::filesystem::path widePath = std::filesystem::temp_directory_path() / "runtime-image-element-wide-test.png";
    {
        std::ofstream file(widePath, std::ios::binary);
        file.write(reinterpret_cast<const char*>(encodedWide.data()), static_cast<std::streamsize>(encodedWide.size()));
    }
    const std::string widePhoto = widePath.string();

    // An image is a box drawn around a picture: it is a rectangle, so everything a rectangle is it
    // is, and the picture is the one thing it brings of its own.
    std::unique_ptr<Element> plain = Layout(
        ("<window width=\"400\" height=\"200\"><frame layout=\"none\" width=\"400\" height=\"200\">"
         "<image id=\"photo\" x=\"10\" y=\"10\" width=\"120\" height=\"80\" source=\"" +
         photo +
         "\"/></frame></window>")
            .c_str(),
        400.0f, 200.0f);
    Element* node = plain != nullptr ? plain->FindById("photo") : nullptr;
    CHECK(test, node != nullptr && node->Kind() == NodeKind::IMAGE);
    ImageElement* picture = dynamic_cast<ImageElement*>(node);
    CHECK(test, picture != nullptr && dynamic_cast<RectangleElement*>(picture) != nullptr);
    if (picture != nullptr) {
        // The picture the file holds is one pixel square, so a node with no size of its own is one
        // pixel square, and the box it was given is the box it covers.
        CHECK(test, picture->Source() == photo);
        CHECK(test, std::fabs(picture->Intrinsic().width - 1.0f) < 0.01f && std::fabs(picture->Intrinsic().height - 1.0f) < 0.01f);
        CHECK(test, std::fabs(picture->Bounds().x - 10.0f) < 0.01f && std::fabs(picture->Bounds().width - 120.0f) < 0.01f);
        CHECK(test, std::fabs(picture->PictureBox().width - 120.0f) < 0.01f && std::fabs(picture->PictureBox().height - 80.0f) < 0.01f);
        RecordingRenderer renderer;
        plain->Paint(renderer);
        const PaintRecord* drawn = PaintRecordFor(renderer, picture->PictureBox());
        CHECK(test, drawn != nullptr && drawn->imagePixels);
        CHECK(test, drawn != nullptr && drawn->imageName.find(photo) != std::string::npos);
        CHECK(test, drawn != nullptr && drawn->imageWidth == 1 && drawn->imageHeight == 1);
    }

    // How the picture is fitted into the box: stretched over it, fitted whole inside it, filling it
    // and hanging over, left at the size it came at, or fitted whole but never grown. A picture that
    // does not cover its box sits where the position says, which is a fraction of the space left.
    std::unique_ptr<Element> fitted = Layout(
        ("<window width=\"400\" height=\"200\"><frame layout=\"none\" width=\"400\" height=\"200\">"
         "<image id=\"stretch\" x=\"10\" y=\"10\" width=\"120\" height=\"80\" source=\"" +
         photo +
         "\"/>"
         "<image id=\"contain\" x=\"200\" y=\"10\" width=\"120\" height=\"80\" fit=\"contain\" source=\"" +
         photo +
         "\"/>"
         "<image id=\"cover\" x=\"10\" y=\"120\" width=\"120\" height=\"80\" fit=\"cover\" source=\"" +
         photo +
         "\"/>"
         "<image id=\"native\" x=\"200\" y=\"120\" width=\"120\" height=\"80\" fit=\"none\" source=\"" +
         photo +
         "\"/>"
         "<image id=\"small\" x=\"330\" y=\"10\" width=\"60\" height=\"40\" fit=\"scaleDown\" objectPosition=\"left top\" source=\"" +
         photo + "\"/></frame></window>")
            .c_str(),
        400.0f, 200.0f);
    const auto boxOf = [&](const char* id) -> Rect {
        Element* found = fitted != nullptr ? fitted->FindById(id) : nullptr;
        ImageElement* element = dynamic_cast<ImageElement*>(found);
        return element == nullptr ? Rect{} : element->PictureBox();
    };
    const Rect stretch = boxOf("stretch");
    CHECK(test, std::fabs(stretch.width - 120.0f) < 0.01f && std::fabs(stretch.height - 80.0f) < 0.01f && std::fabs(stretch.x - 10.0f) < 0.01f);
    // Fitted whole in a hundred and twenty by eighty box, a square picture is eighty across, centred.
    const Rect contain = boxOf("contain");
    CHECK(test, std::fabs(contain.width - 80.0f) < 0.01f && std::fabs(contain.height - 80.0f) < 0.01f && std::fabs(contain.x - 220.0f) < 0.01f);
    // Covering the same box fills its width and hangs over top and bottom by twenty.
    const Rect cover = boxOf("cover");
    CHECK(test, std::fabs(cover.width - 120.0f) < 0.01f && std::fabs(cover.height - 120.0f) < 0.01f && std::fabs(cover.y - 100.0f) < 0.01f);
    // At the size it came at, a single pixel sits in the middle of the box it was given.
    const Rect native = boxOf("native");
    CHECK(test, std::fabs(native.width - 1.0f) < 0.01f && std::fabs(native.x - 259.5f) < 0.01f && std::fabs(native.y - 159.5f) < 0.01f);
    // Never grown: a picture smaller than its box is not blown up, and the position puts it against
    // the top left corner instead of the middle.
    // (`small` is one of the names the Windows headers keep for themselves, so the box is `shrunk`.)
    const Rect shrunk = boxOf("small");
    CHECK(test, std::fabs(shrunk.width - 1.0f) < 0.01f && std::fabs(shrunk.x - 330.0f) < 0.01f && std::fabs(shrunk.y - 10.0f) < 0.01f);

    // A picture that hangs over its box is cut off at the box whether it was asked to be clipped or
    // because covering the box is exactly the case where it hangs over.
    std::unique_ptr<Element> held = Layout(
        ("<window width=\"700\" height=\"200\"><frame layout=\"none\" width=\"700\" height=\"200\">"
         "<image id=\"loose\" x=\"10\" y=\"10\" width=\"120\" height=\"80\" fit=\"cover\" source=\"" +
         photo +
         "\"/><image id=\"clipped\" x=\"200\" y=\"10\" width=\"120\" height=\"80\" clip=\"true\" source=\"" + photo +
         "\"/><image id=\"rounded\" x=\"10\" y=\"120\" width=\"120\" height=\"80\" radius=\"16\" source=\"" + photo +
         "\"/><image id=\"roundedOver\" x=\"200\" y=\"120\" width=\"120\" height=\"80\" radius=\"16\" fit=\"cover\" source=\"" +
         photo +
         "\"/><image id=\"roundedDown\" x=\"400\" y=\"120\" width=\"120\" height=\"80\" radius=\"16\" source=\"" +
         widePhoto + "\"/></frame></window>")
            .c_str(),
        700.0f, 200.0f);
    if (held != nullptr) {
        RecordingRenderer renderer;
        held->Paint(renderer);
        // The picture is drawn in the box it was fitted into rather than in the box the node was
        // given, so the record it lies in is the one at the fitted box.
        ImageElement* loose = dynamic_cast<ImageElement*>(held->FindById("loose"));
        ImageElement* clipped = dynamic_cast<ImageElement*>(held->FindById("clipped"));
        const PaintRecord* covering = loose == nullptr ? nullptr : PaintRecordFor(renderer, loose->PictureBox());
        const PaintRecord* cut = clipped == nullptr ? nullptr : PaintRecordFor(renderer, clipped->PictureBox());
        CHECK(test, covering != nullptr && covering->clipDepth > 0);
        CHECK(test, cut != nullptr && cut->clipDepth > 0);
        // A box without corners needs no shape: the plain box is the cut it is given, and the corner
        // radii of the shape being square is what tells the two cuts apart.
        CHECK(test, cut != nullptr && cut->clipShape.empty());
        // A box with corners costs the platform a mask, which it composites for every picture of
        // every frame, so a picture that fills the box exactly and is no larger than its box carries
        // the corners in its own pixels instead: it is cut out of nothing, because the corners are
        // already in it.
        ImageElement* round = dynamic_cast<ImageElement*>(held->FindById("roundedDown"));
        const PaintRecord* filled = round == nullptr ? nullptr : PaintRecordFor(renderer, round->PictureBox());
        CHECK(test, filled != nullptr && filled->clipDepth == 0 && filled->clipShape.empty());
        // The corners written into it are the ones the box would have cut: the corner of the picture
        // is gone and the middle of it is untouched.
        if (round != nullptr) {
            ImageElement::Picture carried;
            CHECK(test, round->ReadPicture(carried));
            if (carried.width == 200 && carried.height == 120 && carried.pixels != nullptr) {
                CHECK(test, carried.pixels[0] == 0 && carried.pixels[1] == 0 && carried.pixels[2] == 0 && carried.pixels[3] == 0);
                CHECK(test, carried.pixels[(60 * 200 + 40) * 4 + 3] == 255);
            } else {
                CHECK(test, false);
            }
        }
        // A picture that hangs over its box is cut where it hangs over as well, which is a cut the
        // picture cannot carry, and a picture magnified has corners finer than its own pixels, so
        // those are still held inside the shape their corners draw. The cut is a path, which is how
        // a cut that is a shape rather than a box is made.
        ImageElement* over = dynamic_cast<ImageElement*>(held->FindById("roundedOver"));
        const PaintRecord* softened = over == nullptr ? nullptr : PaintRecordFor(renderer, over->PictureBox());
        CHECK(test, softened != nullptr && softened->clipDepth > 0 && !softened->clipShape.empty() &&
                        softened->clipShape.find("A ") != std::string::npos);
        ImageElement* tight = dynamic_cast<ImageElement*>(held->FindById("rounded"));
        const PaintRecord* grown = tight == nullptr ? nullptr : PaintRecordFor(renderer, tight->PictureBox());
        CHECK(test, grown != nullptr && grown->clipDepth > 0 && grown->clipShape.find("A ") != std::string::npos);
    }

    // What the document said about the picture reaches the node: the region of the file that is
    // drawn, the look it is washed with, and the two mirrors.
    std::unique_ptr<Element> washed_once = Layout(
        ("<window width=\"400\" height=\"200\"><frame layout=\"none\" width=\"400\" height=\"200\">"
         "<image id=\"washed\" x=\"10\" y=\"10\" width=\"120\" height=\"80\" source=\"" +
         photo +
         "\" crop=\"0 0 1 1\" tint=\"#FF8040\" brightness=\"0.5\" contrast=\"2\" saturation=\"0\" "
         "flipX=\"true\" flipY=\"true\" rotation=\"30\"/></frame></window>")
            .c_str(),
        400.0f, 200.0f);
    ImageElement* washedElement = dynamic_cast<ImageElement*>(washed_once != nullptr ? washed_once->FindById("washed") : nullptr);
    CHECK(test, washedElement != nullptr);
    if (washedElement != nullptr) {
        CHECK(test, washedElement->Cropped() && washedElement->Crop().width == 1 && washedElement->Crop().x == 0);
        CHECK(test, washedElement->Tinted() && washedElement->TintColor().r > 0.9f && washedElement->TintColor().b < 0.4f);
        CHECK(test, std::fabs(washedElement->Brightness() - 0.5f) < 0.01f);
        CHECK(test, std::fabs(washedElement->Contrast() - 2.0f) < 0.01f);
        CHECK(test, std::fabs(washedElement->Saturation()) < 0.01f);
        CHECK(test, washedElement->FlippedX() && washedElement->FlippedY());
        CHECK(test, std::fabs(washedElement->Rotation() - 30.0f) < 0.01f);
        // A crop is the size the picture is drawn at, because the region is the part of the file the
        // document asked to see rather than the whole of it.
        CHECK(test, std::fabs(washedElement->Intrinsic().width - 1.0f) < 0.01f);
        RecordingRenderer renderer;
        washed_once->Paint(renderer);
        const PaintRecord* record = PaintRecordFor(renderer, washedElement->Bounds());
        CHECK(test, record != nullptr && record->imagePixels && record->rotationDepth > 0);
        // Two pictures of the same file washed two ways are two images to the renderer's cache.
        CHECK(test, record != nullptr && record->imageName.find(photo) != std::string::npos && record->imageName.size() > photo.size());
    }

    // A picture that names a file nobody keeps is not drawn as nothing: the box it would have covered
    // is drawn, named after the file, so a gap in a design is visible where it stands.
    std::unique_ptr<Element> missing = Layout(
        "<window width=\"400\" height=\"200\"><frame layout=\"none\" width=\"400\" height=\"200\">"
        "<image id=\"gap\" x=\"10\" y=\"10\" width=\"120\" height=\"80\" source=\"Nowhere.png\"/></frame></window>",
        400.0f, 200.0f);
    ImageElement* absent = dynamic_cast<ImageElement*>(missing != nullptr ? missing->FindById("gap") : nullptr);
    CHECK(test, absent != nullptr && absent->Source() == "Nowhere.png");
    if (absent != nullptr) {
        CHECK(test, std::fabs(absent->Intrinsic().width - 120.0f) < 0.01f && std::fabs(absent->Intrinsic().height - 80.0f) < 0.01f);
        RecordingRenderer renderer;
        missing->Paint(renderer);
        bool named = false;
        for (const PaintRecord& record : renderer.records) {
            if (record.text == "Nowhere.png") {
                named = true;
            }
        }
        CHECK(test, named);
    }

    // What an image takes and what it refuses. It takes the box, the corners, the turn, the fitting,
    // the region, the position, and the look; it refuses everything that describes a box of content,
    // because a picture is one thing in the flow of its parent rather than a box around others.
    CHECK(test, CompileUi(Source("<window><image source=\"photo.png\" fit=\"cover\" crop=\"0 0 10 10\" objectPosition=\"top right\" "
                                  "tint=\"#FFFFFF\" brightness=\"1.2\" contrast=\"0.8\" saturation=\"2\" flipX=\"true\" flipY=\"true\" "
                                  "radius=\"8\" clip=\"true\" rotation=\"15\" width=\"120\" height=\"80\" x=\"4\" y=\"8\" position=\"absolute\"/></window>"))
                    .Ok());
    CHECK(test, !CompileUi(Source("<window><image layout=\"vertical\"/></window>")).Ok());
    CHECK(test, !CompileUi(Source("<window><image gap=\"8\"/></window>")).Ok());
    CHECK(test, !CompileUi(Source("<window><image padding=\"8\"/></window>")).Ok());
    CHECK(test, !CompileUi(Source("<window><image overflow=\"scroll\"/></window>")).Ok());
    CHECK(test, !CompileUi(Source("<window><image text=\"Hello\"/></window>")).Ok());
    CHECK(test, !CompileUi(Source("<window><image d=\"M0 0 L8 8\"/></window>")).Ok());
    // The fit, the position, and the crop are read by the element, so a document that names none of
    // them is the picture stretched over the box it was given, sitting the way it was written.
    std::unique_ptr<Element> plainer = Layout("<window width=\"400\" height=\"200\"><frame layout=\"none\" width=\"400\" height=\"200\">"
                                              "<image id=\"plain\" x=\"10\" y=\"10\" width=\"120\" height=\"80\" source=\"Nowhere.png\"/></frame></window>",
                                              400.0f, 200.0f);
    ImageElement* unwritten = dynamic_cast<ImageElement*>(plainer != nullptr ? plainer->FindById("plain") : nullptr);
    CHECK(test, unwritten != nullptr && unwritten->Fitting() == ImageFit::FILL && !unwritten->Clipped());
    CHECK(test, unwritten != nullptr && std::fabs(unwritten->PositionX() - 0.5f) < 0.01f && std::fabs(unwritten->PositionY() - 0.5f) < 0.01f);
    CHECK(test, unwritten != nullptr && !unwritten->Tinted() && !unwritten->Cropped() && !unwritten->FlippedX());
    CHECK(test, unwritten != nullptr && std::fabs(unwritten->Brightness() - 1.0f) < 0.01f);

    std::filesystem::remove(picturePath);
    Finish(test, "image", before);
}

// A picture of two frames held for a tenth of a second each, which is the kind of file a video
// plays: one pass through it is a fifth of a second, so a moment inside it can be named and a moment
// past its end is a fact a test can measure. Both frames are the same lone pixel, so which of the
// two is being drawn is read from the name the pixels are held under rather than from their colour.
const unsigned char VIDEO_TWO_FRAME_GIF[] = {
    0x47, 0x49, 0x46, 0x38, 0x39, 0x61, 0x01, 0x00, 0x01, 0x00, 0x80, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x21, 0xF9, 0x04, 0x00, 0x0A, 0x00, 0x00, 0x00, 0x2C, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00,
    0x00, 0x02, 0x02, 0x44, 0x01, 0x00, 0x21, 0xF9, 0x04, 0x00, 0x0A, 0x00, 0x00, 0x00, 0x2C, 0x00, 0x00, 0x00,
    0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x02, 0x02, 0x44, 0x01, 0x00, 0x3B};

// The same picture with one frame. A picture is a video only when there is more than one frame to
// run through, so a file of a single frame has no length, never runs, and asks for no next pass.
const unsigned char VIDEO_ONE_FRAME_GIF[] = {
    0x47, 0x49, 0x46, 0x38, 0x39, 0x61, 0x01, 0x00, 0x01, 0x00, 0x80, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x21, 0xF9, 0x04, 0x00, 0x0A, 0x00, 0x00, 0x00, 0x2C, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00,
    0x00, 0x02, 0x02, 0x44, 0x01, 0x00, 0x3B};

// Whether a picture drawn from a file is the frame with that number of it. A frame is drawn from the
// name the pixels are cached under, so the number the name carries is the number that was drawn.
bool DrawnFrame(const ImageElement::Picture& picture, int frame) {
    return picture.name.find("#" + std::to_string(frame) + "#") != std::string::npos;
}

// A container of moving pictures is written here byte by byte, because a test cannot lean on a codec
// of the machine under it: the frames inside are the ones the runtime's own readers already carry,
// and the container around them is the plainest one each format allows. Two containers are written
// because the two are walked very differently: an IVF is a flat run of frames each of which states
// its own size, and an AVI nests its frames inside lists inside the file.
namespace {

void PutTwo(std::vector<unsigned char>& out, unsigned value) {
    out.push_back(static_cast<unsigned char>(value & 0xFFu));
    out.push_back(static_cast<unsigned char>((value >> 8) & 0xFFu));
}

void PutFour(std::vector<unsigned char>& out, unsigned value) {
    PutTwo(out, value & 0xFFFFu);
    PutTwo(out, (value >> 16) & 0xFFFFu);
}

void PutEight(std::vector<unsigned char>& out, unsigned long long value) {
    PutFour(out, static_cast<unsigned>(value & 0xFFFFFFFFull));
    PutFour(out, static_cast<unsigned>((value >> 32) & 0xFFFFFFFFull));
}

void PutName(std::vector<unsigned char>& out, const char* name) {
    out.insert(out.end(), name, name + 4);
}

// A chunk of a RIFF file: what it is called, how long what it holds is, and then what it holds,
// padded to an even length because every chunk of a RIFF file begins on an even byte.
void PutChunk(std::vector<unsigned char>& out, const char* name, const std::vector<unsigned char>& payload) {
    PutName(out, name);
    PutFour(out, static_cast<unsigned>(payload.size()));
    out.insert(out.end(), payload.begin(), payload.end());
    if (payload.size() % 2u != 0u) {
        out.push_back(0);
    }
}

// A list of a RIFF file, which is a chunk whose payload begins by naming what kind of list it is.
void PutList(std::vector<unsigned char>& out, const char* kind, const std::vector<unsigned char>& content) {
    std::vector<unsigned char> payload;
    PutName(payload, kind);
    payload.insert(payload.end(), content.begin(), content.end());
    PutChunk(out, "LIST", payload);
}

std::vector<unsigned char> MakeIvf(const std::vector<std::vector<unsigned char>>& frames, unsigned width, unsigned height,
                                   unsigned timescale, unsigned scale) {
    std::vector<unsigned char> out;
    PutName(out, "DKIF");
    PutTwo(out, 0);        // the version of the format
    PutTwo(out, 32);       // how much room the header takes, which is where the frames begin
    PutName(out, "VP80");  // the codec every frame of it is written in
    PutTwo(out, width);
    PutTwo(out, height);
    PutFour(out, timescale);  // the denominator of the rate the moments are written at
    PutFour(out, scale);      // the numerator of it
    PutFour(out, static_cast<unsigned>(frames.size()));
    PutFour(out, 0);
    for (std::size_t index = 0; index < frames.size(); ++index) {
        PutFour(out, static_cast<unsigned>(frames[index].size()));
        PutEight(out, static_cast<unsigned long long>(index));  // the moment this frame begins at
        // The frames of an IVF stand one after another with nothing between them.
        out.insert(out.end(), frames[index].begin(), frames[index].end());
    }
    return out;
}

std::vector<unsigned char> MakeAvi(const std::vector<std::vector<unsigned char>>& frames, unsigned width, unsigned height,
                                   unsigned microsecondsPerFrame) {
    // The stream header: a stream of pictures, written as Motion JPEG.
    std::vector<unsigned char> strh;
    PutName(strh, "vids");
    PutName(strh, "MJPG");
    PutFour(strh, 0);
    PutTwo(strh, 0);
    PutTwo(strh, 0);
    PutFour(strh, 0);
    PutFour(strh, 1);
    PutFour(strh, microsecondsPerFrame > 0u ? 1000000u / microsecondsPerFrame : 10u);
    PutFour(strh, 0);
    PutFour(strh, static_cast<unsigned>(frames.size()));
    PutFour(strh, 0);
    PutFour(strh, 0);
    PutFour(strh, 0);
    PutTwo(strh, 0);
    PutTwo(strh, 0);
    PutTwo(strh, 0);
    PutTwo(strh, 0);

    // The format header of the stream: the shape of the picture its frames are written in.
    std::vector<unsigned char> strf;
    PutFour(strf, 40);
    PutFour(strf, width);
    PutFour(strf, height);
    PutTwo(strf, 1);
    PutTwo(strf, 24);
    PutName(strf, "MJPG");  // the codec, written where a bitmap header names one
    PutFour(strf, width * height * 3u);
    PutFour(strf, 0);
    PutFour(strf, 0);
    PutFour(strf, 0);
    PutFour(strf, 0);

    std::vector<unsigned char> stream;
    PutChunk(stream, "strh", strh);
    PutChunk(stream, "strf", strf);

    // The header of the file: how long a frame is held for, and the shape of the picture.
    std::vector<unsigned char> avih;
    PutFour(avih, microsecondsPerFrame);
    PutFour(avih, 0);
    PutFour(avih, 0);
    PutFour(avih, 0);
    PutFour(avih, static_cast<unsigned>(frames.size()));
    PutFour(avih, 0);
    PutFour(avih, 1);
    PutFour(avih, 0);
    PutFour(avih, width);
    PutFour(avih, height);
    PutFour(avih, 0);
    PutFour(avih, 0);
    PutFour(avih, 0);
    PutFour(avih, 0);

    std::vector<unsigned char> header;
    PutChunk(header, "avih", avih);
    PutList(header, "strl", stream);

    std::vector<unsigned char> pictures;
    for (const std::vector<unsigned char>& frame : frames) {
        PutChunk(pictures, "00dc", frame);
    }

    std::vector<unsigned char> body;
    PutName(body, "AVI ");
    PutList(body, "hdrl", header);
    PutList(body, "movi", pictures);

    std::vector<unsigned char> out;
    PutChunk(out, "RIFF", body);
    return out;
}

void WriteBytes(const std::filesystem::path& path, const std::vector<unsigned char>& bytes) {
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

}  // namespace

// A container of moving pictures is walked for where its frames lie rather than read whole, which is
// the one thing the whole-file reading of a picture cannot do: a minute of a moving picture is
// thousands of frames and a document has no business holding them. What is proved here is the
// walking of the container and not the codec inside it, so the frames written into the two
// containers are the ones the runtime's own readers were already carrying.
void TestVideoContainers(TestContext& test) {
    const int before = test.failed;

    // The lossy WebP of a lone red pixel, the file the image tests already read. Everything around
    // the picture in it is a RIFF chunk header and nothing else, so the payload of its `VP8 ` chunk
    // is a whole VP8 picture: a frame of an IVF carries no more than that.
    const unsigned char webp[] = {
        0x52, 0x49, 0x46, 0x46, 0x3C, 0x00, 0x00, 0x00, 0x57, 0x45, 0x42, 0x50, 0x56, 0x50, 0x38, 0x20, 0x30, 0x00, 0x00, 0x00, 0xD0, 0x01,
        0x00, 0x9D, 0x01, 0x2A, 0x01, 0x00, 0x01, 0x00, 0x02, 0x00, 0x34, 0x25, 0xA0, 0x02, 0x74, 0xBA, 0x01, 0xF8, 0x00, 0x03, 0xB0, 0x00,
        0xFE, 0xF0, 0xC4, 0x0B, 0xFF, 0x20, 0xB9, 0x61, 0x75, 0xC8, 0xD7, 0xFF, 0x20, 0x3F, 0xE4, 0x07, 0xFC, 0x80, 0xFF, 0xF8, 0xF2, 0x00,
        0x00, 0x00};
    const std::vector<unsigned char> vp8Frame(webp + 20, webp + sizeof(webp));

    // Two frames of a tenth of a second each, so one pass through the container is a fifth of a
    // second and a moment inside it can be named the way a moment inside a GIF is.
    const std::filesystem::path ivfPath = std::filesystem::temp_directory_path() / "runtime-video-container-test.ivf";
    WriteBytes(ivfPath, MakeIvf({vp8Frame, vp8Frame}, 1, 1, 10, 1));
    const std::filesystem::path webpPath = std::filesystem::temp_directory_path() / "runtime-video-not-a-container-test.webp";
    WriteBytes(webpPath, std::vector<unsigned char>(webp, webp + sizeof(webp)));
    CHECK(test, std::filesystem::exists(ivfPath) && std::filesystem::exists(webpPath));

    VideoStream stream;
    CHECK(test, stream.Open(ivfPath.string()) && stream.Ready());
    CHECK(test, stream.Format() == "ivf");
    // The size of the picture is stated in the header of the container, so the box a video lays out
    // as is known without a frame of it having been read.
    CHECK(test, stream.FrameCount() == 2 && stream.Width() == 1 && stream.Height() == 1);
    CHECK(test, stream.DurationMilliseconds() == 200ull);
    const ImageFrame* first = stream.FrameAt(0);
    CHECK(test, first != nullptr && first->width == 1 && first->height == 1 && first->bgra.size() >= 4u);
    CHECK(test, first != nullptr && first->bgra[2] > 240 && first->bgra[1] < 16 && first->bgra[0] < 16);
    // The frame already in hand is the frame asked for again, which is what makes a picture standing
    // on one frame cost nothing to stand there.
    CHECK(test, stream.FrameAt(0) == first);
    CHECK(test, stream.FrameAt(2) == nullptr && stream.FrameAt(-1) == nullptr);

    // A file that is not a container of moving pictures is refused, and so is one that is not there,
    // so that the element which asked is free to read it the way it reads any other file.
    VideoStream still;
    CHECK(test, !still.Open(webpPath.string()) && !still.Ready());
    CHECK(test, !still.Open((std::filesystem::temp_directory_path() / "runtime-video-nowhere-test.mp4").string()));

    // The other container, and the other whole-picture codec: every frame of a Motion JPEG AVI is a
    // JPEG, and the file is nested lists rather than a flat run.
    std::vector<unsigned char> jpegFrame;
    CHECK(test, EncodeBitmap(GUID_ContainerFormatJpeg, jpegFrame, 16, 16));
    const std::filesystem::path aviPath = std::filesystem::temp_directory_path() / "runtime-video-container-test.avi";
    if (!jpegFrame.empty()) {
        WriteBytes(aviPath, MakeAvi({jpegFrame, jpegFrame}, 16, 16, 100000));
        VideoStream motion;
        CHECK(test, motion.Open(aviPath.string()) && motion.Ready());
        CHECK(test, motion.Format() == "avi");
        CHECK(test, motion.FrameCount() == 2 && motion.Width() == 16 && motion.Height() == 16);
        CHECK(test, motion.DurationMilliseconds() == 200ull);
        const ImageFrame* drawn = motion.FrameAt(1);
        CHECK(test, drawn != nullptr && drawn->width == 16 && drawn->height == 16 && drawn->bgra.size() == 1024u);
    }

    // And through the element a document actually asks for, because a container is only useful if
    // the video playing it is the same video as one playing a GIF: the same length, the same frames
    // at the same moments, and a box that is the size the container states when the document states
    // no size of its own.
    std::unique_ptr<Element> tree = Layout(("<window width=\"240\" height=\"120\"><frame layout=\"none\" width=\"240\" "
                                            "height=\"120\"><video id=\"clip\" x=\"0\" y=\"0\" width=\"60\" height=\"40\" "
                                            "controls=\"true\" source=\"" +
                                            ivfPath.string() + "\"/><video id=\"sized\" x=\"80\" y=\"0\" source=\"" +
                                            ivfPath.string() + "\"/></frame></window>")
                                               .c_str(),
                                           240.0f, 120.0f);
    VideoElement* clip = dynamic_cast<VideoElement*>(tree != nullptr ? tree->FindById("clip") : nullptr);
    VideoElement* sized = dynamic_cast<VideoElement*>(tree != nullptr ? tree->FindById("sized") : nullptr);
    CHECK(test, clip != nullptr && sized != nullptr);
    if (clip != nullptr) {
        CHECK(test, std::fabs(clip->Bounds().width - 60.0f) < 0.01f && std::fabs(clip->Bounds().height - 40.0f) < 0.01f);
        CHECK(test, clip->Playing() && clip->Looping() && clip->ControlsShown() && clip->WantsHandCursor());
        CHECK(test, std::fabs(clip->Duration() - 0.2f) < 0.001f);
        clip->Seek(0.0);
        ImageElement::Picture frame;
        CHECK(test, clip->ReadPicture(frame) && DrawnFrame(frame, 0));
        clip->Seek(0.15);
        CHECK(test, clip->ReadPicture(frame) && DrawnFrame(frame, 1));
        // A playhead at the end of the container stands inside its last frame rather than past it.
        clip->Seek(0.4);
        CHECK(test, std::fabs(clip->Position() - 0.2f) < 0.001f);
        CHECK(test, clip->ReadPicture(frame) && DrawnFrame(frame, 1));
        // Running moves the playhead by the time it was given times the rate it runs at.
        clip->Seek(0.0);
        CHECK(test, clip->Tick(0.01f));
        CHECK(test, std::fabs(clip->Position() - 0.01f) < 0.001f);
        // And the box of a video with no size of its own is the size the container states.
        CHECK(test, std::fabs(sized->Bounds().width - 1.0f) < 0.01f && std::fabs(sized->Bounds().height - 1.0f) < 0.01f);
    }

    // A container holds its file open for as long as it stands, so the tree is let go of before the
    // files are: a file this machine still has open is a file it will not remove. The removing is
    // asked for by way of the error it may report rather than by way of an exception, because a
    // temporary file that could not be let go of is no reason to bring the whole run down.
    tree.reset();
    std::error_code ignored;
    std::filesystem::remove(ivfPath, ignored);
    std::filesystem::remove(webpPath, ignored);
    std::filesystem::remove(aviPath, ignored);
    Finish(test, "video containers", before);
}

void TestVideo(TestContext& test) {
    const int before = test.failed;
    // A video is a picture that is played rather than shown, so it is read from the same files a
    // picture is read from whenever one of them holds more than one frame. The file here is written
    // where the test can name it, because an element reads its picture from a file rather than from
    // the bytes of one.
    const std::filesystem::path clipPath = std::filesystem::temp_directory_path() / "runtime-video-element-test.gif";
    const std::filesystem::path stillPath = std::filesystem::temp_directory_path() / "runtime-video-still-test.gif";
    {
        std::ofstream file(clipPath, std::ios::binary);
        file.write(reinterpret_cast<const char*>(VIDEO_TWO_FRAME_GIF), static_cast<std::streamsize>(sizeof(VIDEO_TWO_FRAME_GIF)));
    }
    {
        std::ofstream file(stillPath, std::ios::binary);
        file.write(reinterpret_cast<const char*>(VIDEO_ONE_FRAME_GIF), static_cast<std::streamsize>(sizeof(VIDEO_ONE_FRAME_GIF)));
    }
    CHECK(test, std::filesystem::exists(clipPath) && std::filesystem::exists(stillPath));
    const std::string clip = clipPath.string();
    const std::string still = stillPath.string();

    std::unique_ptr<Element> tree = Layout(
        ("<window width=\"420\" height=\"300\"><frame layout=\"none\" width=\"420\" height=\"300\">"
         "<video id=\"clip\" x=\"10\" y=\"10\" width=\"160\" height=\"90\" controls=\"true\" source=\"" +
         clip +
         "\"/><video id=\"held\" x=\"200\" y=\"10\" width=\"160\" height=\"90\" controls=\"true\" playing=\"false\" "
         "time=\"0.15\" source=\"" +
         clip +
         "\"/><video id=\"still\" x=\"10\" y=\"120\" width=\"160\" height=\"90\" source=\"" + still +
         "\"/></frame></window>")
            .c_str(),
        420.0f, 300.0f);
    VideoElement* playing = dynamic_cast<VideoElement*>(tree != nullptr ? tree->FindById("clip") : nullptr);
    VideoElement* held = dynamic_cast<VideoElement*>(tree != nullptr ? tree->FindById("held") : nullptr);
    VideoElement* single = dynamic_cast<VideoElement*>(tree != nullptr ? tree->FindById("still") : nullptr);
    CHECK(test, playing != nullptr && held != nullptr && single != nullptr);
    if (playing != nullptr) {
        // A video is a picture, so it is the box the document gave it rather than the size of the
        // lone pixel inside it, and the name it answers to is the component's own.
        CHECK(test, playing->Kind() == NodeKind::VIDEO);
        CHECK(test, std::fabs(playing->Bounds().width - 160.0f) < 0.01f && std::fabs(playing->Bounds().height - 90.0f) < 0.01f);
        // A video with nothing written about its playing runs, loops, and runs at the rate the file's
        // own delays were written at, which is what a document that writes no playing expects.
        CHECK(test, playing->Playing() && playing->Looping() && playing->ControlsShown());
        CHECK(test, std::fabs(playing->Speed() - 1.0f) < 0.01f);
        CHECK(test, std::fabs(playing->Duration() - 0.2f) < 0.001f);
        // The frame drawn is the frame the playhead stands in, so a moment inside the picture names
        // the frame that moment falls in and the end of a pass is inside its last frame.
        playing->Seek(0.0);
        ImageElement::Picture frame;
        CHECK(test, playing->ReadPicture(frame) && DrawnFrame(frame, 0));
        playing->Seek(0.15);
        CHECK(test, playing->ReadPicture(frame) && DrawnFrame(frame, 1));
        playing->Seek(0.4);
        CHECK(test, std::fabs(playing->Position() - 0.2f) < 0.001f);
        CHECK(test, playing->ReadPicture(frame) && DrawnFrame(frame, 1));
        // Running: the pass that drew this frame is never the last one, and the playhead moves by
        // the time it was given times the rate it runs at.
        playing->Seek(0.0);
        CHECK(test, playing->Tick(0.01f));
        CHECK(test, std::fabs(playing->Position() - 0.01f) < 0.001f);
        playing->Seek(0.0);
        playing->SetSpeed(2.0f);
        CHECK(test, playing->Tick(0.01f));
        CHECK(test, std::fabs(playing->Position() - 0.02f) < 0.001f);
        playing->SetSpeed(1.0f);
        // A picture that is stopped stands where it was left: the playhead does not move and there
        // is nothing new to draw, so the pass is not asked for again.
        playing->SetPlaying(false);
        const float stopped = playing->Position();
        CHECK(test, !playing->Tick(0.01f));
        CHECK(test, std::fabs(playing->Position() - stopped) < 0.0001f);
        // A click anywhere on the picture is what starts it and stops it, the way it is on every
        // player, and the click a document hears is still a click.
        int clicks = 0;
        playing->Connect("click", [&clicks](const KeyEvent&) { ++clicks; });
        playing->Activate();
        CHECK(test, playing->Playing() && clicks == 1);
        // The bar is a control and not only a mark on the picture. The track stands the playhead
        // where the pointer is and carries it along as the pointer moves, and lifting the pointer
        // there does not start or stop the picture, because the press has already said what it
        // meant. The button is what starts and stops it, and so is anywhere else on the picture.
        playing->SetPlaying(false);
        playing->Seek(0.0);
        playing->OnPointerDown(Point{74.5f, 80.0f});
        CHECK(test, std::fabs(playing->Position() - 0.05f) < 0.005f);
        CHECK(test, playing->OnDrag(Point{127.5f, 80.0f}));
        CHECK(test, std::fabs(playing->Position() - 0.15f) < 0.005f);
        playing->Activate();
        // The click is still heard, and the picture is still stopped: a click on the track is a
        // click that moved the playhead rather than one that started or stopped anything.
        CHECK(test, !playing->Playing() && clicks == 2);
        playing->OnPointerDown(Point{32.0f, 80.0f});
        playing->Activate();
        CHECK(test, playing->Playing());
        // A drag that did not begin on the track is nobody's drag here, so the element underneath
        // is free to take it.
        playing->OnPointerDown(Point{100.0f, 20.0f});
        CHECK(test, !playing->OnDrag(Point{120.0f, 20.0f}));
        playing->Activate();
        CHECK(test, !playing->Playing());
        playing->SetPlaying(true);
        // A pass that has run out comes round to the beginning, so the playhead is inside the
        // picture again rather than past its end.
        CHECK(test, playing->Tick(0.5f));
        CHECK(test, playing->Position() >= 0.0f && playing->Position() < playing->Duration());
        // Without a loop the picture stops at its end and holds the frame it stopped on.
        playing->SetLoop(false);
        playing->Seek(0.19);
        CHECK(test, playing->Tick(0.05f));
        CHECK(test, !playing->Playing() && std::fabs(playing->Position() - 0.2f) < 0.001f);
        CHECK(test, playing->ReadPicture(frame) && DrawnFrame(frame, 1));
        // Started again from its end it plays again from its beginning, which is what a play button
        // at the end of a video does anywhere.
        playing->SetPlaying(true);
        CHECK(test, playing->Playing() && std::fabs(playing->Position()) < 0.001f);
        // The playing of it is what a binding reaches, and a value the language does not read as a
        // boolean leaves the video as it was rather than stopping it.
        playing->ApplyBound("playing", "false");
        CHECK(test, !playing->Playing());
        playing->ApplyBound("playing", "true");
        CHECK(test, playing->Playing());
        playing->ApplyBound("playing", "sometimes");
        CHECK(test, playing->Playing());
        playing->ApplyBound("speed", "3");
        CHECK(test, std::fabs(playing->Speed() - 3.0f) < 0.01f);
        playing->ApplyBound("loop", "false");
        CHECK(test, !playing->Looping());
        playing->ApplyBound("controls", "false");
        CHECK(test, !playing->ControlsShown());
        playing->ApplyBound("controls", "true");
        CHECK(test, playing->ControlsShown());
        playing->ApplyBound("time", "0.05");
        CHECK(test, std::fabs(playing->Position() - 0.05f) < 0.001f);
        // A rate a picture cannot be played at is kept to one that can, rather than stopping it.
        playing->ApplyBound("speed", "100");
        CHECK(test, playing->Speed() > 0.0f && playing->Speed() <= 16.0f);
        // A picture that can be played is something the pointer acts on, so the pointer says so.
        CHECK(test, playing->WantsHandCursor());
    }
    if (held != nullptr) {
        // A picture that was told where to start stands there rather than at its beginning, and it
        // is drawn at the frame that moment falls in.
        CHECK(test, !held->Playing() && std::fabs(held->Position() - 0.15f) < 0.001f);
        ImageElement::Picture frame;
        CHECK(test, held->ReadPicture(frame) && DrawnFrame(frame, 1));
        // Nothing is running, so nothing asks for another pass.
        CHECK(test, !held->Tick(1.0f / 60.0f));
    }
    if (single != nullptr) {
        // A picture of one frame is a picture rather than a video: there is nothing to play, so it
        // has no length, it does not run, and it asks for no next pass.
        CHECK(test, single->Playing());
        CHECK(test, single->Duration() == 0.0f && std::fabs(single->Progress()) < 0.001f);
        CHECK(test, !single->Tick(1.0f / 60.0f));
        // It is still drawn as the picture it holds, which is the frame there is only one of.
        ImageElement::Picture sole;
        CHECK(test, single->ReadPicture(sole) && DrawnFrame(sole, 0));
    }

    // A video is drawn as the picture it plays, with the bar of it at the foot of the box the
    // picture was drawn in, and the shape on the bar is the shape of the thing that will happen: the
    // picture that is stopped draws the mark a picture is started with, and the one that is running
    // draws the two bars it is stopped with.
    if (tree != nullptr) {
        RecordingRenderer renderer;
        tree->Paint(renderer);
        int playMarks = 0;
        bool pictureDrawn = false;
        for (const PaintRecord& record : renderer.records) {
            if (record.vector && record.path.find("13.5 8") != std::string::npos) {
                ++playMarks;
            }
            if (record.imagePixels && record.imageName.find(clip) != std::string::npos) {
                pictureDrawn = true;
            }
        }
        CHECK(test, pictureDrawn);
        // One mark for each picture that is stopped. The one that is running draws bars instead.
        CHECK(test, playMarks == 1);
        // The bar stands inside the foot of the box, an inset from each edge of it.
        CHECK(test, PaintRecordFor(renderer, Rect{18.0f, 10.0f + 90.0f - 8.0f - 24.0f, 160.0f - 16.0f, 24.0f}) != nullptr);
    }

    // A video whose file cannot be read is the gap a picture with no file is, named after what it
    // would have been. There is nothing to play, so nothing runs and no bar is drawn over it.
    std::unique_ptr<Element> absentTree = Layout(
        "<window width=\"300\" height=\"200\"><frame layout=\"none\" width=\"300\" height=\"200\">"
        "<video id=\"gap\" x=\"10\" y=\"10\" width=\"160\" height=\"90\" controls=\"true\" source=\"Nowhere.mp4\"/></frame></window>",
        300.0f, 200.0f);
    VideoElement* absent = dynamic_cast<VideoElement*>(absentTree != nullptr ? absentTree->FindById("gap") : nullptr);
    CHECK(test, absent != nullptr);
    if (absent != nullptr) {
        // Nothing was read, so there is no length to play and no frame to hold.
        CHECK(test, absent->Duration() == 0.0f);
        CHECK(test, !absent->Tick(1.0f / 60.0f));
        CHECK(test, !absent->WantsHandCursor());
        RecordingRenderer renderer;
        absentTree->Paint(renderer);
        int marks = 0;
        bool named = false;
        for (const PaintRecord& record : renderer.records) {
            if (record.vector) {
                ++marks;
            }
            if (record.text == "Nowhere.mp4") {
                named = true;
            }
        }
        CHECK(test, marks == 0 && named);
        CHECK(test, PaintRecordFor(renderer, Rect{18.0f, 10.0f + 90.0f - 8.0f - 24.0f, 160.0f - 16.0f, 24.0f}) == nullptr);
    }

    // What a video takes and what it refuses. It takes the box, the corners, the turn, the picture
    // and the playing of it; it refuses everything that describes a box of content, because a video
    // is a picture and a picture is one thing in the flow of its parent.
    CHECK(test, CompileUi(Source("<window><video source=\"clip.webp\" fit=\"cover\" crop=\"0 0 8 8\" objectPosition=\"top right\" "
                                  "radius=\"8\" clip=\"true\" rotation=\"15\" playing=\"false\" loop=\"false\" speed=\"0.5\" "
                                  "time=\"1.5\" controls=\"true\" width=\"160\" height=\"90\"/></window>"))
                    .Ok());
    CHECK(test, !CompileUi(Source("<window><video layout=\"vertical\"/></window>")).Ok());
    CHECK(test, !CompileUi(Source("<window><video padding=\"8\"/></window>")).Ok());
    CHECK(test, !CompileUi(Source("<window><video overflow=\"scroll\"/></window>")).Ok());
    CHECK(test, !CompileUi(Source("<window><video playing=\"sometimes\"/></window>")).Ok());

    std::filesystem::remove(clipPath);
    std::filesystem::remove(stillPath);
    Finish(test, "video", before);
}

void TestRemainingSurface(TestContext& test) {
    const int before = test.failed;
    const std::vector<PathSample> cubic = SamplePath("M0 0 C0 40 30 40 30 0");
    CHECK(test, cubic.size() > 2);
    bool curved = false;
    for (const PathSample& point : cubic) {
        if (point.y > 5.0f) {
            curved = true;
        }
    }
    CHECK(test, curved);
    const std::vector<PathSample> smooth = SamplePath("M0 0 C0 20 10 20 10 0 S20 -20 20 0");
    CHECK(test, smooth.size() > cubic.size() / 2);
    const std::vector<PathSample> quad = SamplePath("M0 0 Q15 30 30 0");
    CHECK(test, quad.back().x > 20.0f);
    const std::vector<PathSample> smoothQuad = SamplePath("M0 0 Q10 20 20 0 T40 0");
    CHECK(test, smoothQuad.back().x > 30.0f);
    const std::vector<PathSample> arc = SamplePath("M0 10 A10 10 0 0 1 20 10");
    bool bowed = false;
    for (const PathSample& point : arc) {
        if (std::fabs(point.y - 10.0f) > 2.0f) {
            bowed = true;
        }
    }
    CHECK(test, bowed);
    const std::vector<PathSample> packed = SamplePath("M0 0c1.5.5 2 3 4 5");
    CHECK(test, !packed.empty() && std::fabs(packed.back().x - 4.0f) < 0.01f && std::fabs(packed.back().y - 5.0f) < 0.01f);
    const std::vector<PathSample> exponent = SamplePath("M0 0L1.5e-1 2e1");
    CHECK(test, !exponent.empty() && std::fabs(exponent.back().x - 0.15f) < 0.001f && std::fabs(exponent.back().y - 20.0f) < 0.01f);
    const std::vector<PathSample> closed = SamplePath("M0 0 L10 0 L10 10 Z");
    CHECK(test, !closed.empty() && closed.back().x == 0.0f && closed.back().y == 0.0f);

    TextStyle latin;
    latin.fontSize = 18.0f;
    const ShapedText hello = ShapeText("Hello", latin, 200.0f);
    CHECK(test, hello.shaped && hello.width > 10.0f && hello.fallback);
    TextStyle arabic = latin;
    arabic.rtl = true;
    arabic.locale = "ar";
    const ShapedText greeting = ShapeText("مرحبا", arabic, 200.0f);
    CHECK(test, greeting.shaped && greeting.rtl && greeting.width > 4.0f);
    const ShapedText emoji = ShapeText("😀", latin, 80.0f);
    CHECK(test, emoji.shaped && emoji.width > 1.0f);
    TextStyle missing = latin;
    missing.fontFamily = "MissingFontXYZ";
    const ShapedText fallback = ShapeText("Hello", missing, 200.0f);
    CHECK(test, fallback.shaped && fallback.fallback);

    std::vector<unsigned char> png;
    std::vector<unsigned char> jpeg;
    CHECK(test, EncodeBitmap(GUID_ContainerFormatPng, png));
    CHECK(test, EncodeBitmap(GUID_ContainerFormatJpeg, jpeg));
    DecodedImage decoded;
    CHECK(test, DecodeImageBytes(png.data(), static_cast<int>(png.size()), decoded) && decoded.format == "png" && decoded.frames.size() == 1);
    CHECK(test, DecodeImageBytes(jpeg.data(), static_cast<int>(jpeg.size()), decoded) && decoded.format == "jpeg");
    // A progressive JPEG writes its picture in as many passes as it likes: one pass for the first
    // coefficient of every block, passes for bands of the rest, and passes that sharpen what an
    // earlier one wrote rather than adding anything to it. The file below has nine, among them two
    // refinements of the first coefficient and three of the bands, so a reader that stops after the
    // first pass draws an approximation and one that mis-reads a refinement draws a picture that is
    // wrong everywhere rather than slightly. Two files are read, one that keeps colour for every
    // pixel and one that keeps it for every second pixel, because those two share a pass over the
    // picture differently.
    //
    // The pixels each file decodes to elsewhere are written beside it, and the two are compared
    // within the last step of the transform. The colours of the second file are compared more
    // loosely, because it holds colour for every second pixel and the difference between taking the
    // sample a pixel stands over and blending the four around it is a difference in the drawing
    // rather than in the coefficients.
    const unsigned char progressiveColor[] = {
        0xFF, 0xD8, 0xFF, 0xE0, 0x00, 0x10, 0x4A, 0x46, 0x49, 0x46, 0x00, 0x01,
        0x01, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0xFF, 0xDB, 0x00, 0x43,
        0x00, 0x06, 0x04, 0x05, 0x06, 0x05, 0x04, 0x06, 0x06, 0x05, 0x06, 0x07,
        0x07, 0x06, 0x08, 0x0A, 0x10, 0x0A, 0x0A, 0x09, 0x09, 0x0A, 0x14, 0x0E,
        0x0F, 0x0C, 0x10, 0x17, 0x14, 0x18, 0x18, 0x17, 0x14, 0x16, 0x16, 0x1A,
        0x1D, 0x25, 0x1F, 0x1A, 0x1B, 0x23, 0x1C, 0x16, 0x16, 0x20, 0x2C, 0x20,
        0x23, 0x26, 0x27, 0x29, 0x2A, 0x29, 0x19, 0x1F, 0x2D, 0x30, 0x2D, 0x28,
        0x30, 0x25, 0x28, 0x29, 0x28, 0xFF, 0xDB, 0x00, 0x43, 0x01, 0x07, 0x07,
        0x07, 0x0A, 0x08, 0x0A, 0x13, 0x0A, 0x0A, 0x13, 0x28, 0x1A, 0x16, 0x1A,
        0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28,
        0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28,
        0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28,
        0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28,
        0x28, 0x28, 0xFF, 0xC2, 0x00, 0x11, 0x08, 0x00, 0x10, 0x00, 0x10, 0x03,
        0x01, 0x11, 0x00, 0x02, 0x11, 0x01, 0x03, 0x11, 0x01, 0xFF, 0xC4, 0x00,
        0x16, 0x00, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x05, 0x07, 0xFF, 0xC4, 0x00,
        0x17, 0x01, 0x00, 0x03, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x06, 0x07, 0xFF, 0xDA,
        0x00, 0x0C, 0x03, 0x01, 0x00, 0x02, 0x10, 0x03, 0x10, 0x00, 0x00, 0x01,
        0x1E, 0x08, 0xDE, 0xD8, 0xD4, 0x9C, 0xCB, 0x33, 0x97, 0x0F, 0x3F, 0xFF,
        0xC4, 0x00, 0x16, 0x10, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x04, 0x10, 0xFF,
        0xDA, 0x00, 0x08, 0x01, 0x01, 0x00, 0x01, 0x05, 0x02, 0x0A, 0xD0, 0x2B,
        0x4F, 0xFF, 0xC4, 0x00, 0x15, 0x11, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x10,
        0xFF, 0xDA, 0x00, 0x08, 0x01, 0x03, 0x01, 0x01, 0x3F, 0x01, 0x32, 0x61,
        0x93, 0x3F, 0xFF, 0xC4, 0x00, 0x15, 0x11, 0x01, 0x01, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
        0x10, 0xFF, 0xDA, 0x00, 0x08, 0x01, 0x02, 0x01, 0x01, 0x3F, 0x01, 0x18,
        0x33, 0xFF, 0xC4, 0x00, 0x14, 0x10, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x20, 0xFF,
        0xDA, 0x00, 0x08, 0x01, 0x01, 0x00, 0x06, 0x3F, 0x02, 0x1F, 0xFF, 0xC4,
        0x00, 0x16, 0x10, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x21, 0x31, 0xFF, 0xDA,
        0x00, 0x08, 0x01, 0x01, 0x00, 0x01, 0x3F, 0x21, 0x9A, 0x13, 0x43, 0xFF,
        0xDA, 0x00, 0x0C, 0x03, 0x01, 0x00, 0x02, 0x00, 0x03, 0x00, 0x00, 0x00,
        0x10, 0x10, 0xFF, 0x00, 0xFF, 0xC4, 0x00, 0x16, 0x11, 0x00, 0x03, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x10, 0x21, 0x31, 0xFF, 0xDA, 0x00, 0x08, 0x01, 0x03, 0x01, 0x01,
        0x3F, 0x10, 0x9B, 0x13, 0x63, 0xFF, 0xC4, 0x00, 0x14, 0x11, 0x01, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x20, 0xFF, 0xDA, 0x00, 0x08, 0x01, 0x02, 0x01, 0x01, 0x3F,
        0x10, 0x1F, 0xFF, 0xC4, 0x00, 0x15, 0x10, 0x01, 0x01, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10,
        0xF1, 0xFF, 0xDA, 0x00, 0x08, 0x01, 0x01, 0x00, 0x01, 0x3F, 0x10, 0x82,
        0x41, 0x3F, 0xFF, 0xD9,
    };
    const unsigned char progressiveShared[] = {
        0xFF, 0xD8, 0xFF, 0xE0, 0x00, 0x10, 0x4A, 0x46, 0x49, 0x46, 0x00, 0x01,
        0x01, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0xFF, 0xDB, 0x00, 0x43,
        0x00, 0x06, 0x04, 0x05, 0x06, 0x05, 0x04, 0x06, 0x06, 0x05, 0x06, 0x07,
        0x07, 0x06, 0x08, 0x0A, 0x10, 0x0A, 0x0A, 0x09, 0x09, 0x0A, 0x14, 0x0E,
        0x0F, 0x0C, 0x10, 0x17, 0x14, 0x18, 0x18, 0x17, 0x14, 0x16, 0x16, 0x1A,
        0x1D, 0x25, 0x1F, 0x1A, 0x1B, 0x23, 0x1C, 0x16, 0x16, 0x20, 0x2C, 0x20,
        0x23, 0x26, 0x27, 0x29, 0x2A, 0x29, 0x19, 0x1F, 0x2D, 0x30, 0x2D, 0x28,
        0x30, 0x25, 0x28, 0x29, 0x28, 0xFF, 0xDB, 0x00, 0x43, 0x01, 0x07, 0x07,
        0x07, 0x0A, 0x08, 0x0A, 0x13, 0x0A, 0x0A, 0x13, 0x28, 0x1A, 0x16, 0x1A,
        0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28,
        0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28,
        0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28,
        0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28,
        0x28, 0x28, 0xFF, 0xC2, 0x00, 0x11, 0x08, 0x00, 0x10, 0x00, 0x10, 0x03,
        0x01, 0x22, 0x00, 0x02, 0x11, 0x01, 0x03, 0x11, 0x01, 0xFF, 0xC4, 0x00,
        0x16, 0x00, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x05, 0x07, 0xFF, 0xC4, 0x00,
        0x15, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x04, 0xFF, 0xDA, 0x00, 0x0C,
        0x03, 0x01, 0x00, 0x02, 0x10, 0x03, 0x10, 0x00, 0x00, 0x01, 0x1E, 0x8E,
        0xA6, 0x73, 0x52, 0xFF, 0xC4, 0x00, 0x16, 0x10, 0x00, 0x03, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x03, 0x04, 0x10, 0xFF, 0xDA, 0x00, 0x08, 0x01, 0x01, 0x00, 0x01, 0x05,
        0x02, 0x0A, 0xD0, 0x2B, 0x4F, 0xFF, 0xC4, 0x00, 0x1A, 0x11, 0x00, 0x01,
        0x05, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x05, 0x00, 0x01, 0x02, 0x11, 0x12, 0x21, 0xFF, 0xDA, 0x00,
        0x08, 0x01, 0x03, 0x01, 0x01, 0x3F, 0x01, 0x18, 0x5B, 0x30, 0x7E, 0x5A,
        0xFF, 0xC4, 0x00, 0x17, 0x11, 0x00, 0x03, 0x01, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x04,
        0x31, 0xFF, 0xDA, 0x00, 0x08, 0x01, 0x02, 0x01, 0x01, 0x3F, 0x01, 0x8D,
        0x9A, 0x7F, 0xFF, 0xC4, 0x00, 0x14, 0x10, 0x01, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x20,
        0xFF, 0xDA, 0x00, 0x08, 0x01, 0x01, 0x00, 0x06, 0x3F, 0x02, 0x1F, 0xFF,
        0xC4, 0x00, 0x16, 0x10, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x21, 0x31, 0xFF,
        0xDA, 0x00, 0x08, 0x01, 0x01, 0x00, 0x01, 0x3F, 0x21, 0x9A, 0x13, 0x43,
        0xFF, 0xDA, 0x00, 0x0C, 0x03, 0x01, 0x00, 0x02, 0x00, 0x03, 0x00, 0x00,
        0x00, 0x10, 0x5F, 0xFF, 0xC4, 0x00, 0x1A, 0x11, 0x00, 0x01, 0x05, 0x01,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x11, 0x00, 0x01, 0x31, 0x41, 0x61, 0x71, 0xFF, 0xDA, 0x00, 0x08, 0x01,
        0x03, 0x01, 0x01, 0x3F, 0x10, 0xE5, 0x9B, 0x02, 0x71, 0xD7, 0xFF, 0xC4,
        0x00, 0x19, 0x11, 0x00, 0x01, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x11, 0x00, 0x01, 0x21, 0x31,
        0xC1, 0xFF, 0xDA, 0x00, 0x08, 0x01, 0x02, 0x01, 0x01, 0x3F, 0x10, 0x13,
        0x44, 0xD6, 0xAF, 0xFF, 0xC4, 0x00, 0x15, 0x10, 0x01, 0x01, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x10, 0xF1, 0xFF, 0xDA, 0x00, 0x08, 0x01, 0x01, 0x00, 0x01, 0x3F, 0x10,
        0x82, 0x41, 0x3F, 0xFF, 0xD9,
    };
    auto AlmostEqual = [](int value, int expect, int tolerance) {
        const int delta = value - expect;
        return delta <= tolerance && delta >= -tolerance;
    };
    CHECK(test, DecodeImageBytes(progressiveColor, static_cast<int>(sizeof(progressiveColor)), decoded) &&
                    decoded.format == "jpeg" && decoded.frames.size() == 1);
    if (!decoded.frames.empty()) {
        const ImageFrame& frame = decoded.frames.front();
        CHECK(test, frame.width == 16 && frame.height == 16 && frame.bgra.size() == 1024);
        const int probes[6][5] = {{0, 0, 0, 0, 0},         {5, 3, 75, 48, 63},   {8, 0, 230, 30, 199},
                                  {12, 7, 230, 30, 199},   {3, 10, 47, 160, 104}, {15, 15, 20, 30, 91}};
        for (const auto& probe : probes) {
            const std::size_t at = (static_cast<std::size_t>(probe[1]) * 16 + static_cast<std::size_t>(probe[0])) * 4u;
            CHECK(test, AlmostEqual(frame.bgra[at], probe[4], 6) && AlmostEqual(frame.bgra[at + 1], probe[3], 6) &&
                            AlmostEqual(frame.bgra[at + 2], probe[2], 6));
        }
    }
    CHECK(test, DecodeImageBytes(progressiveShared, static_cast<int>(sizeof(progressiveShared)), decoded) &&
                    decoded.format == "jpeg" && decoded.frames.size() == 1);
    if (!decoded.frames.empty()) {
        const ImageFrame& frame = decoded.frames.front();
        CHECK(test, frame.width == 16 && frame.height == 16 && frame.bgra.size() == 1024);
        const int probes[5][5] = {{2, 2, 31, 33, 32},   {4, 6, 59, 98, 80},   {12, 4, 234, 26, 208},
                                  {13, 12, 19, 32, 87}, {5, 13, 77, 211, 142}};
        for (const auto& probe : probes) {
            const std::size_t at = (static_cast<std::size_t>(probe[1]) * 16 + static_cast<std::size_t>(probe[0])) * 4u;
            CHECK(test, AlmostEqual(frame.bgra[at], probe[4], 40) && AlmostEqual(frame.bgra[at + 1], probe[3], 40) &&
                            AlmostEqual(frame.bgra[at + 2], probe[2], 40));
        }
    }

    const unsigned char webp[] = {
        0x52, 0x49, 0x46, 0x46, 0x3C, 0x00, 0x00, 0x00, 0x57, 0x45, 0x42, 0x50, 0x56, 0x50, 0x38, 0x20, 0x30, 0x00, 0x00, 0x00, 0xD0, 0x01,
        0x00, 0x9D, 0x01, 0x2A, 0x01, 0x00, 0x01, 0x00, 0x02, 0x00, 0x34, 0x25, 0xA0, 0x02, 0x74, 0xBA, 0x01, 0xF8, 0x00, 0x03, 0xB0, 0x00,
        0xFE, 0xF0, 0xC4, 0x0B, 0xFF, 0x20, 0xB9, 0x61, 0x75, 0xC8, 0xD7, 0xFF, 0x20, 0x3F, 0xE4, 0x07, 0xFC, 0x80, 0xFF, 0xF8, 0xF2, 0x00,
        0x00, 0x00};
    CHECK(test, DecodeImageBytes(webp, static_cast<int>(sizeof(webp)), decoded) && decoded.format == "webp" && decoded.frames.size() == 1);
    CHECK(test, decoded.frames[0].width == 1 && decoded.frames[0].bgra.size() >= 4 && decoded.frames[0].bgra[2] > 240 &&
                    decoded.frames[0].bgra[1] < 16 && decoded.frames[0].bgra[0] < 16);
    const unsigned char avif[] = {
        0x00, 0x00, 0x00, 0x1C, 0x66, 0x74, 0x79, 0x70, 0x61, 0x76, 0x69, 0x66, 0x00, 0x00, 0x00, 0x00,
        0x6D, 0x69, 0x66, 0x31, 0x61, 0x76, 0x69, 0x66, 0x6D, 0x69, 0x61, 0x66, 0x00, 0x00, 0x00, 0xD6,
        0x6D, 0x65, 0x74, 0x61, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x21, 0x68, 0x64, 0x6C, 0x72,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x70, 0x69, 0x63, 0x74, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x22, 0x69, 0x6C, 0x6F,
        0x63, 0x00, 0x00, 0x00, 0x00, 0x44, 0x40, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
        0xFA, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x29, 0x00, 0x00, 0x00, 0x23, 0x69,
        0x69, 0x6E, 0x66, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x15, 0x69, 0x6E, 0x66,
        0x65, 0x02, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x61, 0x76, 0x30, 0x31, 0x00, 0x00, 0x00,
        0x00, 0x0E, 0x70, 0x69, 0x74, 0x6D, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x56,
        0x69, 0x70, 0x72, 0x70, 0x00, 0x00, 0x00, 0x38, 0x69, 0x70, 0x63, 0x6F, 0x00, 0x00, 0x00, 0x0C,
        0x61, 0x76, 0x31, 0x43, 0x81, 0x40, 0x6C, 0x00, 0x00, 0x00, 0x00, 0x14, 0x69, 0x73, 0x70, 0x65,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x10,
        0x70, 0x69, 0x78, 0x69, 0x00, 0x00, 0x00, 0x00, 0x03, 0x0C, 0x0C, 0x0C, 0x00, 0x00, 0x00, 0x16,
        0x69, 0x70, 0x6D, 0x61, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x03, 0x81,
        0x02, 0x03, 0x00, 0x00, 0x00, 0x31, 0x6D, 0x64, 0x61, 0x74, 0x12, 0x00, 0x0A, 0x09, 0x58, 0x08,
        0xBF, 0x6B, 0x40, 0x43, 0x41, 0xB8, 0x40, 0x32, 0x1A, 0x19, 0x47, 0x87, 0x86, 0x21, 0x89, 0xA6,
        0x9A, 0x66, 0x80, 0x00, 0x00, 0x90, 0x3F, 0x9B, 0x0C, 0x61, 0x4B, 0x2B, 0x4D, 0x8F, 0x51, 0x45,
        0x4E, 0xA4, 0x20};
    // The container of an AVIF is read by the runtime itself and not by the platform, so the file is
    // known to be an AVIF and known to be as big as it says wherever it is opened, whether or not the
    // machine under it can turn the picture into pixels. The pixels come from a reader the host
    // supplied; where this machine has none that reads AV1 the file is refused and the size it states
    // is the whole of what is known about it, which is what lets the gap it leaves be the gap the
    // picture would have covered instead of a box of a size the file never mentioned.
    {
        DecodedImage read;
        const bool decodedAvif = DecodeImageBytes(avif, static_cast<int>(sizeof(avif)), read);
        CHECK(test, read.format == "avif");
        CHECK(test, read.declaredWidth == 8 && read.declaredHeight == 8);
        if (PlatformDecodesAvif(avif, static_cast<int>(sizeof(avif)))) {
            CHECK(test, decodedAvif && read.frames.size() == 1);
            if (!read.frames.empty()) {
                CHECK(test, read.frames[0].width == 8 && read.frames[0].bgra.size() >= 4 && read.frames[0].bgra[2] > 200 &&
                                read.frames[0].bgra[0] < 40);
            }
        } else {
            // No AV1 codec reachable from here: the container is read and the picture inside it
            // cannot be, so the file is refused rather than drawn as the wrong thing.
            std::printf("  note: no reachable AV1 codec, so an AVIF is refused and lays out at the size it states\n");
            CHECK(test, !decodedAvif && read.frames.empty());
        }
    }
    // With no reader supplied by the host, the runtime reads the container and nothing more. The file
    // is still an AVIF and still says it is eight by eight, and no pixels come out of it. That is the
    // whole of what the runtime knows on a machine with nothing that reads AV1, and it is the same on
    // every machine rather than a fact about this one, because the reading of the container is the
    // runtime's own code rather than a call into the platform.
    {
        const ImageFormatProvider keep = GetImageFormatProvider();
        SetImageFormatProvider(nullptr);
        DecodedImage bare;
        CHECK(test, !DecodeImageBytes(avif, static_cast<int>(sizeof(avif)), bare));
        CHECK(test, bare.format == "avif" && bare.declaredWidth == 8 && bare.declaredHeight == 8 && bare.frames.empty());
        // A picture whose pixels cannot be produced still measures as the picture it holds, so a
        // document naming one leaves the gap the picture would have covered rather than a box of a
        // size the file never mentioned.
        const std::filesystem::path avifPath = std::filesystem::temp_directory_path() / "runtime-image-element-test.avif";
        {
            std::ofstream file(avifPath, std::ios::binary);
            file.write(reinterpret_cast<const char*>(avif), static_cast<std::streamsize>(sizeof(avif)));
        }
        const std::string avifDocument =
            "<window width=\"400\" height=\"200\"><frame layout=\"none\" width=\"400\" height=\"200\">"
            "<image id=\"avifbox\" x=\"10\" y=\"10\" source=\"" +
            avifPath.string() + "\"/></frame></window>";
        std::unique_ptr<Element> unread = Layout(avifDocument.c_str(), 400.0f, 200.0f);
        ImageElement* stated = dynamic_cast<ImageElement*>(unread != nullptr ? unread->FindById("avifbox") : nullptr);
        CHECK(test, stated != nullptr && std::fabs(stated->Intrinsic().width - 8.0f) < 0.01f &&
                        std::fabs(stated->Intrinsic().height - 8.0f) < 0.01f);
        std::filesystem::remove(avifPath);
        SetImageFormatProvider(keep);
    }
    // A bitmap and an icon are read by readers written here rather than by the platform, so both are
    // turned into pixels on every machine. The bitmap is built by hand, which leaves nothing between
    // the bytes and the reading of them; the icon carries a PNG, so the reading of an icon is the
    // reading of the picture inside it as well.
    const std::vector<unsigned char> bitmap = MakeBitmap();
    CHECK(test, DecodeImageBytes(bitmap.data(), static_cast<int>(bitmap.size()), decoded) && decoded.format == "bmp" &&
                    decoded.frames.size() == 1);
    if (!decoded.frames.empty()) {
        const ImageFrame& frame = decoded.frames.front();
        CHECK(test, frame.width == 2 && frame.height == 2 && frame.bgra.size() == 16);
        // The first pixel written in the file is the bottom left one and the first pixel drawn is
        // the top left one, so the row that was written last is the row that comes out first: red
        // with green beside it, and blue with white below.
        CHECK(test, frame.bgra[0] == 0 && frame.bgra[2] == 255);
        CHECK(test, frame.bgra[4] == 0 && frame.bgra[5] == 255 && frame.bgra[6] == 0);
        CHECK(test, frame.bgra[8] == 255 && frame.bgra[9] == 0 && frame.bgra[10] == 0);
        CHECK(test, frame.bgra[12] == 255 && frame.bgra[13] == 255 && frame.bgra[14] == 255);
    }
    std::vector<unsigned char> smallPng;
    const bool haveSmallPng = EncodeBitmap(GUID_ContainerFormatPng, smallPng);
    CHECK(test, haveSmallPng);
    if (haveSmallPng && !smallPng.empty()) {
        const std::vector<unsigned char> icon = MakeIcon(smallPng);
        CHECK(test, DecodeImageBytes(icon.data(), static_cast<int>(icon.size()), decoded) && decoded.format == "ico" &&
                        decoded.frames.size() == 1 && decoded.frames.front().width == 1);
    }
    // The other form an icon is written in: a bare bitmap beside a stencil that says which pixels are
    // not there. The stencil is the only transparency a picture of fewer than thirty-two bits has, so
    // a marked pixel has to come out transparent rather than as the colour underneath it.
    const std::vector<unsigned char> bareIcon = MakeIcon(MakeBitmapPicture());
    CHECK(test, DecodeImageBytes(bareIcon.data(), static_cast<int>(bareIcon.size()), decoded) && decoded.format == "ico" &&
                    decoded.frames.size() == 1);
    if (!decoded.frames.empty()) {
        const ImageFrame& frame = decoded.frames.front();
        CHECK(test, frame.width == 2 && frame.height == 2 && frame.bgra.size() == 16);
        CHECK(test, frame.bgra[3] == 0 && frame.bgra[7] == 255);
        CHECK(test, frame.bgra[8] == 255 && frame.bgra[9] == 0 && frame.bgra[10] == 0);
    }

    // A tagged picture is a list of named figures with the pixels kept somewhere else in the file,
    // and the list says both how wide the picture is and how the run of bytes that holds it was
    // squeezed. The four files below were written by the format's own library and are read back
    // against the pixels it wrote: the same eight pixels in every squeeze, so a reader that got a
    // run of codes wrong would be wrong in the pixels rather than in the size it reported.
    const unsigned char kTiffLzw[] = {
        0x49, 0x49, 0x2a, 0x00, 0x26, 0x00, 0x00, 0x00, 0x80, 0x02, 0x82, 0x81, 0xe1, 0x40, 0xc8, 0x78,
        0x46, 0x28, 0x16, 0x8c, 0x86, 0xe3, 0xc2, 0x09, 0x18, 0x96, 0x50, 0x2a, 0x96, 0x8b, 0xe6, 0x43,
        0x49, 0xb8, 0xe6, 0x78, 0x40, 0x40, 0x0a, 0x00, 0x00, 0x01, 0x03, 0x00, 0x01, 0x00, 0x00, 0x00,
        0x04, 0x00, 0x00, 0x00, 0x01, 0x01, 0x03, 0x00, 0x01, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00,
        0x02, 0x01, 0x03, 0x00, 0x03, 0x00, 0x00, 0x00, 0xa4, 0x00, 0x00, 0x00, 0x03, 0x01, 0x03, 0x00,
        0x01, 0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 0x06, 0x01, 0x03, 0x00, 0x01, 0x00, 0x00, 0x00,
        0x02, 0x00, 0x00, 0x00, 0x11, 0x01, 0x04, 0x00, 0x01, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00,
        0x15, 0x01, 0x03, 0x00, 0x01, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x16, 0x01, 0x03, 0x00,
        0x01, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x17, 0x01, 0x04, 0x00, 0x01, 0x00, 0x00, 0x00,
        0x1e, 0x00, 0x00, 0x00, 0x1c, 0x01, 0x03, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x08, 0x00, 0x08, 0x00,
    };
    const unsigned char kTiffPackBits[] = {
        0x49, 0x49, 0x2a, 0x00, 0x22, 0x00, 0x00, 0x00, 0x0b, 0x0a, 0x14, 0x1e, 0x28, 0x32, 0x3c, 0x46,
        0x50, 0x5a, 0x64, 0x6e, 0x78, 0x0b, 0x82, 0x8c, 0x96, 0xa0, 0xaa, 0xb4, 0xbe, 0xc8, 0xd2, 0xdc,
        0xe6, 0xf0, 0x0a, 0x00, 0x00, 0x01, 0x03, 0x00, 0x01, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00,
        0x01, 0x01, 0x03, 0x00, 0x01, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x02, 0x01, 0x03, 0x00,
        0x03, 0x00, 0x00, 0x00, 0xa0, 0x00, 0x00, 0x00, 0x03, 0x01, 0x03, 0x00, 0x01, 0x00, 0x00, 0x00,
        0x05, 0x80, 0x00, 0x00, 0x06, 0x01, 0x03, 0x00, 0x01, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00,
        0x11, 0x01, 0x04, 0x00, 0x01, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x15, 0x01, 0x03, 0x00,
        0x01, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x16, 0x01, 0x03, 0x00, 0x01, 0x00, 0x00, 0x00,
        0x02, 0x00, 0x00, 0x00, 0x17, 0x01, 0x04, 0x00, 0x01, 0x00, 0x00, 0x00, 0x1a, 0x00, 0x00, 0x00,
        0x1c, 0x01, 0x03, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x08, 0x00, 0x08, 0x00, 0x08, 0x00,
    };
    const unsigned char kTiffDeflate[] = {
        0x49, 0x49, 0x2a, 0x00, 0x2a, 0x00, 0x00, 0x00, 0x78, 0x9c, 0xe3, 0x12, 0x91, 0xd3, 0x30, 0xb2,
        0x71, 0x0b, 0x88, 0x4a, 0xc9, 0xab, 0x68, 0xea, 0x99, 0xb6, 0x60, 0xd5, 0x96, 0x7d, 0x27, 0x2e,
        0xdd, 0x79, 0xf6, 0x01, 0x00, 0x65, 0xa8, 0x0b, 0xb9, 0x00, 0x0a, 0x00, 0x00, 0x01, 0x03, 0x00,
        0x01, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x01, 0x01, 0x03, 0x00, 0x01, 0x00, 0x00, 0x00,
        0x02, 0x00, 0x00, 0x00, 0x02, 0x01, 0x03, 0x00, 0x03, 0x00, 0x00, 0x00, 0xa8, 0x00, 0x00, 0x00,
        0x03, 0x01, 0x03, 0x00, 0x01, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x06, 0x01, 0x03, 0x00,
        0x01, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x11, 0x01, 0x04, 0x00, 0x01, 0x00, 0x00, 0x00,
        0x08, 0x00, 0x00, 0x00, 0x15, 0x01, 0x03, 0x00, 0x01, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00,
        0x16, 0x01, 0x03, 0x00, 0x01, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x17, 0x01, 0x04, 0x00,
        0x01, 0x00, 0x00, 0x00, 0x21, 0x00, 0x00, 0x00, 0x1c, 0x01, 0x03, 0x00, 0x01, 0x00, 0x00, 0x00,
        0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x08, 0x00, 0x08, 0x00,
    };
    const unsigned char kTiffBigEndian[] = {
        0x4d, 0x4d, 0x00, 0x2a, 0x00, 0x00, 0x00, 0x08, 0x00, 0x0a, 0x01, 0x00, 0x00, 0x04, 0x00, 0x00,
        0x00, 0x01, 0x00, 0x00, 0x00, 0x04, 0x01, 0x01, 0x00, 0x04, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00,
        0x00, 0x02, 0x01, 0x02, 0x00, 0x03, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x86, 0x01, 0x03,
        0x00, 0x03, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x01, 0x06, 0x00, 0x03, 0x00, 0x00,
        0x00, 0x01, 0x00, 0x02, 0x00, 0x00, 0x01, 0x11, 0x00, 0x04, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00,
        0x00, 0x8c, 0x01, 0x15, 0x00, 0x03, 0x00, 0x00, 0x00, 0x01, 0x00, 0x03, 0x00, 0x00, 0x01, 0x16,
        0x00, 0x04, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x02, 0x01, 0x17, 0x00, 0x04, 0x00, 0x00,
        0x00, 0x01, 0x00, 0x00, 0x00, 0x18, 0x01, 0x1c, 0x00, 0x03, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x08, 0x00, 0x08, 0x0a, 0x14, 0x1e, 0x28,
        0x32, 0x3c, 0x46, 0x50, 0x5a, 0x64, 0x6e, 0x78, 0x82, 0x8c, 0x96, 0xa0, 0xaa, 0xb4, 0xbe, 0xc8,
        0xd2, 0xdc, 0xe6, 0xf0,
    };
    const unsigned char kTiffPalette[] = {
        0x49, 0x49, 0x2a, 0x00, 0x08, 0x00, 0x00, 0x00, 0x09, 0x00, 0x00, 0x01, 0x04, 0x00, 0x01, 0x00,
        0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x01, 0x01, 0x04, 0x00, 0x01, 0x00, 0x00, 0x00, 0x02, 0x00,
        0x00, 0x00, 0x02, 0x01, 0x03, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x03, 0x01,
        0x03, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x06, 0x01, 0x03, 0x00, 0x01, 0x00,
        0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x11, 0x01, 0x04, 0x00, 0x01, 0x00, 0x00, 0x00, 0x86, 0x00,
        0x00, 0x00, 0x15, 0x01, 0x03, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x16, 0x01,
        0x04, 0x00, 0x01, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x40, 0x01, 0x03, 0x00, 0x06, 0x00,
        0x00, 0x00, 0x7a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00,
        0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x55, 0xaa,
    };
    const unsigned char kTiffColours[32] = {30,20,10,255,60,50,40,255,90,80,70,255,120,110,100,255,150,140,130,255,180,170,160,255,210,200,190,255,240,230,220,255};
    const unsigned char kTiffPaletteColours[64] = {0,0,255,255,0,255,0,255,0,0,255,255,0,255,0,255,0,0,255,255,0,255,0,255,0,0,255,255,0,255,0,255,0,255,0,255,0,0,255,255,0,255,0,255,0,0,255,255,0,255,0,255,0,0,255,255,0,255,0,255,0,0,255,255};

    const unsigned char* const kTiffFiles[] = {kTiffLzw, kTiffPackBits, kTiffDeflate, kTiffBigEndian};
    const int kTiffSizes[] = {
        static_cast<int>(sizeof(kTiffLzw)), static_cast<int>(sizeof(kTiffPackBits)),
        static_cast<int>(sizeof(kTiffDeflate)), static_cast<int>(sizeof(kTiffBigEndian)),
    };
    for (int file = 0; file < 4; ++file) {
        DecodedImage tagged;
        const bool read = DecodeImageBytes(kTiffFiles[file], kTiffSizes[file], tagged);
        CHECK(test, read && tagged.format == "tiff" && tagged.frames.size() == 1);
        if (!read || tagged.frames.empty()) {
            continue;
        }
        const ImageFrame& frame = tagged.frames.front();
        CHECK(test, frame.width == 4 && frame.height == 2 && frame.bgra.size() == sizeof(kTiffColours));
        CHECK(test, std::memcmp(frame.bgra.data(), kTiffColours, sizeof(kTiffColours)) == 0);
    }
    // A picture of one bit a sample whose colours are named in a table rather than held in the
    // pixels themselves. The table writes each colour as sixteen bits, which is what a table holds.
    DecodedImage taggedPalette;
    const bool readPalette = DecodeImageBytes(kTiffPalette, static_cast<int>(sizeof(kTiffPalette)), taggedPalette);
    CHECK(test, readPalette && taggedPalette.format == "tiff" && taggedPalette.frames.size() == 1);
    if (readPalette && !taggedPalette.frames.empty()) {
        const ImageFrame& frame = taggedPalette.frames.front();
        CHECK(test, frame.width == 8 && frame.height == 2 && frame.bgra.size() == sizeof(kTiffPaletteColours));
        CHECK(test, std::memcmp(frame.bgra.data(), kTiffPaletteColours, sizeof(kTiffPaletteColours)) == 0);
    }

    // A picture drawn in a box with rounded corners carries those corners itself, so the platform
    // does not have to cut it with a mask on every frame. The arcs are given in the pixels of the
    // picture, and the pixels are premultiplied, so a corner that is cut is scaled in every channel
    // rather than emptied of its alpha alone.
    auto corners = [](std::vector<unsigned char>& pixels, float across, float down) {
        RoundImageCorners(pixels, 8, 8, across, down);
    };
    auto lost = [](const std::vector<unsigned char>& pixels) {
        int count = 0;
        for (std::size_t at = 3; at < pixels.size(); at += 4) {
            if (pixels[at] == 0) {
                ++count;
            }
        }
        return count;
    };
    std::vector<unsigned char> square(8 * 8 * 4, 200);
    corners(square, 0.0f, 0.0f);
    CHECK(test, lost(square) == 0 && square[72] == 200);
    std::vector<unsigned char> rounded(8 * 8 * 4, 200);
    corners(rounded, 3.0f, 3.0f);
    // The very corner has gone in every channel, the pixel two in from it is untouched, and the
    // far corner has gone as well.
    CHECK(test, rounded[0] == 0 && rounded[1] == 0 && rounded[2] == 0 && rounded[3] == 0);
    CHECK(test, rounded[252] == 0 && rounded[255] == 0);
    CHECK(test, rounded[72] == 200 && rounded[75] == 200);
    std::vector<unsigned char> narrow(8 * 8 * 4, 200);
    corners(narrow, 1.0f, 4.0f);
    std::vector<unsigned char> wide(8 * 8 * 4, 200);
    corners(wide, 4.0f, 4.0f);
    // An arc that reaches further across takes more of the picture, on each of its two axes.
    CHECK(test, lost(wide) > lost(narrow));
    std::vector<unsigned char> tiny(4 * 4 * 4, 200);
    RoundImageCorners(tiny, 2, 2, 100.0f, 100.0f);
    CHECK(test, tiny[3] < 200);

    // An animation is a PNG with frames: the picture's own bytes are the first frame, and every frame
    // after it is written as only the part of the picture that changed, laid where it changed. The
    // file here is a blue square with a green patch drawn on it and then taken away again, so the
    // second frame is a two-pixel patch and the third is that patch put back. A reader that drew
    // either of them at the wrong place, or over the whole picture instead of over part of it, would
    // be wrong everywhere rather than slightly.
    const unsigned char animation[] = {
        0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D,
        0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x08,
        0x08, 0x06, 0x00, 0x00, 0x00, 0xC4, 0x0F, 0xBE, 0x8B, 0x00, 0x00, 0x00,
        0x08, 0x61, 0x63, 0x54, 0x4C, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00,
        0x00, 0xCE, 0xED, 0xBA, 0xC0, 0x00, 0x00, 0x00, 0x1A, 0x66, 0x63, 0x54,
        0x4C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00,
        0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x78, 0x03,
        0xE8, 0x00, 0x00, 0xBE, 0x5F, 0x93, 0xFC, 0x00, 0x00, 0x00, 0x16, 0x49,
        0x44, 0x41, 0x54, 0x78, 0x9C, 0x63, 0x64, 0x60, 0xF8, 0xFF, 0x9F, 0x01,
        0x0F, 0x60, 0xC2, 0x27, 0x39, 0x7C, 0x14, 0x00, 0x00, 0x10, 0x5D, 0x02,
        0x0E, 0x65, 0x66, 0x7A, 0x41, 0x00, 0x00, 0x00, 0x1A, 0x66, 0x63, 0x54,
        0x4C, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00,
        0x02, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x78, 0x03,
        0xE8, 0x00, 0x00, 0x58, 0xCF, 0xCB, 0x3E, 0x00, 0x00, 0x00, 0x18, 0x66,
        0x64, 0x41, 0x54, 0x00, 0x00, 0x00, 0x02, 0x78, 0x9C, 0x63, 0x64, 0xF8,
        0xCF, 0xF0, 0x9F, 0x81, 0x81, 0x81, 0x81, 0x89, 0x01, 0x0A, 0x00, 0x1E,
        0x18, 0x02, 0x02, 0x32, 0x6A, 0x7D, 0xF0, 0x00, 0x00, 0x00, 0x1A, 0x66,
        0x63, 0x54, 0x4C, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x02, 0x00,
        0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00,
        0x78, 0x03, 0xE8, 0x00, 0x00, 0xB5, 0x59, 0x18, 0xD7, 0x00, 0x00, 0x00,
        0x18, 0x66, 0x64, 0x41, 0x54, 0x00, 0x00, 0x00, 0x04, 0x78, 0x9C, 0x63,
        0x64, 0x60, 0xF8, 0xFF, 0x9F, 0x81, 0x81, 0x81, 0x81, 0x89, 0x01, 0x0A,
        0x00, 0x1D, 0x19, 0x02, 0x02, 0x1F, 0xDB, 0xEB, 0xF7, 0x00, 0x00, 0x00,
        0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82,
    };
    CHECK(test, DecodeImageBytes(animation, static_cast<int>(sizeof(animation)), decoded) && decoded.format == "png" &&
                    decoded.frames.size() == 3);
    if (decoded.frames.size() == 3) {
        const std::size_t corner = 0;
        const std::size_t patch = (1u * 8u + 1u) * 4u;
        for (const ImageFrame& frame : decoded.frames) {
            CHECK(test, frame.width == 8 && frame.height == 8 && frame.delayMs == 120);
        }
        CHECK(test, decoded.frames[0].bgra[corner] == 255 && decoded.frames[0].bgra[corner + 1] == 0 &&
                        decoded.frames[0].bgra[corner + 2] == 0 && decoded.frames[0].bgra[corner + 3] == 255);
        CHECK(test, decoded.frames[0].bgra[patch] == 255 && decoded.frames[0].bgra[patch + 1] == 0);
        CHECK(test, decoded.frames[1].bgra[patch] == 0 && decoded.frames[1].bgra[patch + 1] == 255 &&
                        decoded.frames[1].bgra[patch + 2] == 0 && decoded.frames[1].bgra[patch + 3] == 255);
        CHECK(test, decoded.frames[1].bgra[corner] == 255 && decoded.frames[1].bgra[corner + 2] == 0);
        CHECK(test, decoded.frames[2].bgra[patch] == 255 && decoded.frames[2].bgra[patch + 1] == 0);
    }
    const char* arrow = "<svg viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"2\"><path d=\"M3.5 13h6\"/><path d=\"M18 7v9\"/></svg>";
    const std::string vectorXml = VectorXmlFromSvg(arrow, "Arrow", 24, 24, 26);
    CHECK(test, vectorXml.find("<vector") != std::string::npos && vectorXml.find("name=\"Arrow\"") != std::string::npos && vectorXml.find("<path") != std::string::npos &&
                    vectorXml.find("strokeWidth=\"2\"") != std::string::npos);
    const CompileResult vectorDocument = CompileUi(Source("<window width=\"200\" height=\"200\">" + vectorXml + "</window>"));
    CHECK(test, vectorDocument.Ok() && vectorDocument.root.has_value() && vectorDocument.root->children.size() == 1 &&
                    vectorDocument.root->children[0].kind == NodeKind::VECTOR && vectorDocument.root->children[0].children.size() == 2);
    const char* svg = "<svg width=\"2\" height=\"2\"><rect width=\"2\" height=\"2\" fill=\"#ff0000\"/></svg>";
    CHECK(test, DecodeImageBytes(reinterpret_cast<const unsigned char*>(svg), static_cast<int>(std::strlen(svg)), decoded) && decoded.format == "svg");
    CHECK(test, !decoded.frames.empty() && decoded.frames[0].bgra.size() >= 3 && decoded.frames[0].bgra[2] == 255);
    const char* hole = "<svg width=\"8\" height=\"8\" viewBox=\"0 0 8 8\"><path fill=\"#00ff00\" fill-rule=\"evenodd\" d=\"M0 0 H8 V8 H0 Z M3 3 H5 V5 H3 Z\"/></svg>";
    CHECK(test, DecodeImageBytes(reinterpret_cast<const unsigned char*>(hole), static_cast<int>(std::strlen(hole)), decoded) && decoded.frames.size() == 1 &&
                    decoded.frames[0].width == 8);
    if (!decoded.frames.empty() && decoded.frames[0].bgra.size() >= 8 * 8 * 4) {
        const unsigned char* corner = decoded.frames[0].bgra.data();
        const unsigned char* center = decoded.frames[0].bgra.data() + (4 * 8 + 4) * 4;
        CHECK(test, corner[1] == 255 && corner[3] == 255);
        CHECK(test, center[3] == 0);
    }
    const char* ring = "<svg width=\"8\" height=\"8\" viewBox=\"0 0 8 8\"><circle cx=\"4\" cy=\"4\" r=\"3\" fill=\"none\" stroke=\"#ff0000\" stroke-width=\"2\"/></svg>";
    CHECK(test, DecodeImageBytes(reinterpret_cast<const unsigned char*>(ring), static_cast<int>(std::strlen(ring)), decoded) && !decoded.frames.empty());
    if (!decoded.frames.empty() && decoded.frames[0].bgra.size() >= 8 * 8 * 4) {
        const unsigned char* middle = decoded.frames[0].bgra.data() + (4 * 8 + 4) * 4;
        const unsigned char* top = decoded.frames[0].bgra.data() + (1 * 8 + 4) * 4;
        CHECK(test, middle[3] == 0);
        CHECK(test, top[2] > 200 && top[3] > 200);
    }
    const unsigned char gif[] = {0x47, 0x49, 0x46, 0x38, 0x39, 0x61, 0x01, 0x00, 0x01, 0x00, 0x80, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x00, 0x00,
                                 0x00, 0x21, 0xF9, 0x04, 0x00, 0x0A, 0x00, 0x00, 0x00, 0x2C, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00,
                                 0x00, 0x02, 0x02, 0x44, 0x01, 0x00, 0x21, 0xF9, 0x04, 0x00, 0x0A, 0x00, 0x00, 0x00, 0x2C, 0x00, 0x00, 0x00,
                                 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x02, 0x02, 0x44, 0x01, 0x00, 0x3B};
    CHECK(test, DecodeImageBytes(gif, static_cast<int>(sizeof(gif)), decoded) && decoded.frames.size() >= 2);

    std::unique_ptr<Element> anchored = Layout(
        "<window width=\"200\" height=\"100\"><frame id=\"bar\" position=\"absolute\" left=\"parent.left\" right=\"parent.right\" "
        "top=\"parent.top\" height=\"20\"/></window>",
        200.0f, 100.0f);
    CHECK(test, anchored != nullptr && anchored->FindById("bar") != nullptr);
    if (anchored != nullptr && anchored->FindById("bar") != nullptr) {
        CHECK(test, std::fabs(anchored->FindById("bar")->Bounds().width - 200.0f) < 1.0f);
        CHECK(test, std::fabs(anchored->FindById("bar")->Bounds().y) < 1.0f);
    }
    std::unique_ptr<Element> responsive = Layout(
        "<window width=\"400\" height=\"200\"><frame id=\"phone\" minScreen=\"700\" width=\"30\" height=\"10\"/>"
        "<frame id=\"wide\" maxScreen=\"500\" width=\"30\" height=\"10\"/>"
        "<frame id=\"side\" orientation=\"landscape\" width=\"30\" height=\"10\"/>"
        "<frame width=\"120\" height=\"40\"><frame id=\"query\" minContainer=\"300\" width=\"10\" height=\"10\"/></frame></window>",
        200.0f, 400.0f);
    CHECK(test, responsive != nullptr);
    if (responsive != nullptr) {
        CHECK(test, responsive->FindById("phone")->Measured().width == 0.0f);
        CHECK(test, responsive->FindById("wide")->Measured().width > 0.0f);
        CHECK(test, responsive->FindById("side")->Measured().width == 0.0f);
        CHECK(test, responsive->FindById("query")->Measured().width == 0.0f);
    }

    const CompileResult styled = CompileUi(Source(
        "<window><tokens><color name=\"primary\" value=\"#112233\"/></tokens>"
        "<style name=\"base\" background=\"$primary\"/>"
        "<style name=\"button\" extends=\"base\"><state name=\"compact\" maxWidth=\"500\" background=\"#445566\"/></style>"
        "<style name=\"button\" variant=\"primary\" background=\"#778899\"/>"
        "<frame id=\"box\" style=\"button\" variant=\"primary\" width=\"40\" height=\"20\"/>"
        "<frame id=\"plain\" style=\"button\" width=\"40\" height=\"20\"/></window>"));
    CHECK(test, styled.Ok());
    if (styled.root.has_value()) {
        std::unique_ptr<Element> root = Instantiate(*styled.root, {});
        root->SetViewport(800.0f, 200.0f);
        Element* plain = root->FindById("plain");
        CHECK(test, plain != nullptr && plain->ActivePatch().background && plain->ActivePatch().backgroundColor.r < 0.15f);
        root->SetViewport(400.0f, 200.0f);
        CHECK(test, plain->ActivePatch().backgroundColor.r > 0.2f);
        Element* box = root->FindById("box");
        CHECK(test, box != nullptr && box->ActivePatch().backgroundColor.b > box->ActivePatch().backgroundColor.r);
    }

    const CompileResult bound = CompileUi(Source(
        "<window><state name=\"user.name\" value=\"Ada\"/><state name=\"user.age\" value=\"36\"/>"
        "<state name=\"user.first\" value=\"Ada\"/><state name=\"user.last\" value=\"Lovelace\"/>"
        "<text id=\"name\" text=\"{user.name}\"/>"
        "<text id=\"once\" text=\"{!user.name}\"/>"
        "<text id=\"age\" text=\"{user.age}\"/>"
        "<text id=\"full\" text=\"{user.first + user.last}\"/>"
        "<text id=\"edit\" text=\"{=user.name}\"/>"
        "<text id=\"live\" text=\"{reactive user.name}\"/>"
        "<text id=\"later\" text=\"{async user.photo}\"/>"
        "</window>"));
    CHECK(test, bound.Ok());
    if (bound.root.has_value()) {
        std::unique_ptr<Element> root = Instantiate(*bound.root, {});
        auto* name = dynamic_cast<TextElement*>(root->FindById("name"));
        auto* once = dynamic_cast<TextElement*>(root->FindById("once"));
        auto* age = dynamic_cast<TextElement*>(root->FindById("age"));
        auto* full = dynamic_cast<TextElement*>(root->FindById("full"));
        auto* edit = dynamic_cast<TextElement*>(root->FindById("edit"));
        auto* live = dynamic_cast<TextElement*>(root->FindById("live"));
        auto* later = dynamic_cast<TextElement*>(root->FindById("later"));
        CHECK(test, name != nullptr && name->Text() == "Ada");
        CHECK(test, full != nullptr && full->Text() == "Ada Lovelace");
        CHECK(test, later != nullptr && later->Text().empty());
        const int ageCount = age->ApplyCount();
        root->SetStateValue("user.name", "Grace");
        CHECK(test, name->Text() == "Grace");
        CHECK(test, once->Text() == "Ada");
        CHECK(test, age->ApplyCount() == ageCount);
        CHECK(test, live->Text() == "Grace");
        edit->WriteBinding("text", "Joan");
        CHECK(test, name->Text() == "Joan");
        root->SetStateValue("user.photo", "ready");
        CHECK(test, later->Text() == "ready");
        CHECK(test, name == root->FindById("name"));
    }

    MotionDecl fade;
    fade.name = "fade";
    fade.property = "opacity";
    fade.from = 0.0f;
    fade.to = 1.0f;
    fade.hasFrom = true;
    fade.hasTo = true;
    fade.duration = 0.1f;
    MotionDecl slide = fade;
    slide.name = "slide";
    slide.property = "x";
    slide.to = 20.0f;
    MotionDecl intro;
    intro.sequence = "fade,slide";
    const std::vector<MotionSample> midway = SampleMotions({fade, slide, intro}, 0.05f, 0, false);
    CHECK(test, !midway.empty() && midway[0].property == "opacity" && midway[0].number > 0.4f && midway[0].number < 0.6f);
    MotionDecl scale = fade;
    scale.name = "scale";
    scale.property = "scale";
    MotionDecl turn = fade;
    turn.name = "turn";
    turn.property = "rotation";
    MotionDecl pop;
    pop.parallel = "fade,scale,turn";
    CHECK(test, SampleMotions({fade, scale, turn, pop}, 0.1f, 0, false).size() >= 3);
    MotionDecl item = fade;
    item.stagger = 0.05f;
    const std::vector<MotionSample> staggered = SampleMotions({item}, 0.05f, 2, false);
    CHECK(test, !staggered.empty() && staggered[0].number < 0.05f);
    MotionDecl spring;
    spring.property = "x";
    spring.from = 0.0f;
    spring.to = 10.0f;
    spring.hasFrom = true;
    spring.hasTo = true;
    spring.easing = "spring";
    spring.mass = 1.0f;
    spring.stiffness = 180.0f;
    spring.damping = 20.0f;
    const std::vector<MotionSample> settled = SampleMotions({spring}, 2.0f, 0, false);
    CHECK(test, !settled.empty() && std::fabs(settled[0].number - 10.0f) < 1.0f);
    MotionDecl pathMotion;
    pathMotion.property = "path";
    pathMotion.duration = 1.0f;
    pathMotion.keys.push_back(MotionKey{0.0f, "path", 0.0f, false, "M0 0"});
    pathMotion.keys.push_back(MotionKey{1.0f, "path", 0.0f, false, "M10 10"});
    const std::vector<MotionSample> pathSample = SampleMotions({pathMotion}, 0.2f, 0, false);
    CHECK(test, !pathSample.empty() && pathSample[0].text == "M0 0");

    std::unique_ptr<Element> played = Layout("<window><frame id=\"title\" width=\"20\" height=\"20\" opacity=\"1\"/></window>", 100.0f, 100.0f);
    CHECK(test, played != nullptr);
    if (played != nullptr) {
        MotionDecl play;
        play.target = "title";
        play.property = "opacity";
        play.from = 1.0f;
        play.to = 0.0f;
        play.hasFrom = true;
        play.hasTo = true;
        play.duration = 0.1f;
        played->SetMotions({play});
        played->Tick(0.05f);
        CHECK(test, played->FindById("title")->Opacity() < 0.6f && played->FindById("title")->Opacity() > 0.4f);
    }

    int clicks = 0;
    std::unique_ptr<Element> events = Layout("<window><frame id=\"save\" name=\"Save\" width=\"40\" height=\"20\"/></window>", 100.0f, 40.0f);
    CHECK(test, events != nullptr);
    if (events != nullptr) {
        Element* save = events->FindById("save");
        save->Connect("click", [&](const KeyEvent& event) {
            ++clicks;
            CHECK(test, event.key == "Enter" && event.ctrl);
        });
        KeyEvent key;
        key.key = "Enter";
        key.code = "Enter";
        key.ctrl = true;
        save->OnKeyEvent(key);
        CHECK(test, clicks == 1);
        CHECK(test, events->Announcement().find("activate") != std::string::npos);
        Element* next = events->NextFocus(nullptr, false);
        CHECK(test, next == save);
        CHECK(test, events->Announcement().find("focus") != std::string::npos);
        save->SetAccessibilityFlag("disabled", true);
        CHECK(test, save->AccessibilityState().find("disabled") != std::string::npos);
    }
    Finish(test, "remaining surface", before);
}

bool NearColor(Color color, Color expected) {
    return std::fabs(color.r - expected.r) < 0.02f && std::fabs(color.g - expected.g) < 0.02f && std::fabs(color.b - expected.b) < 0.02f &&
           std::fabs(color.a - expected.a) < 0.02f;
}

void TestTypography(TestContext& test) {
    const int before = test.failed;
    const CompileResult compiled = CompileUi(Source(
        "<window>"
        "<text id=\"wide\" text=\"Fill\" width=\"fill\"/>"
        "<text id=\"screen\" text=\"Screen\" width=\"screen\"/>"
        "<text id=\"note\" text=\"Meta\" fontFamily=\"Segoe UI\" fontSize=\"14\"/>"
        "</window>"));
    CHECK(test, compiled.Ok());
    if (compiled.root.has_value()) {
        std::unique_ptr<Element> root = Instantiate(*compiled.root, {});
        root->SetViewport(1200.0f, 800.0f);
        FakeMeasurer measurer;
        root->Measure(measurer, 400.0f, 800.0f, true);
        auto* wide = dynamic_cast<TextElement*>(root->FindById("wide"));
        auto* screen = dynamic_cast<TextElement*>(root->FindById("screen"));
        auto* note = dynamic_cast<TextElement*>(root->FindById("note"));
        CHECK(test, wide != nullptr && std::fabs(wide->Measured().width - 400.0f) < 1.0f);
        CHECK(test, screen != nullptr && std::fabs(screen->Measured().width - 1200.0f) < 1.0f);
        CHECK(test, note != nullptr && note->FontFamily() == "Segoe UI" && std::fabs(note->FontSize() - 14.0f) < 0.1f);
    }
    // The names the text subsystem gives the values that shape a run of text are the ones a
    // document may say, and a line height may be the word that leaves the choice to the font.
    CHECK(test, ComponentAcceptsProperty("text", "sizing"));
    CHECK(test, ComponentAcceptsProperty("text", "textCase"));
    CHECK(test, ComponentAcceptsProperty("text", "textDecoration"));
    CHECK(test, ComponentAcceptsProperty("text", "lineHeight"));

    // A block of text decides the box it covers: the size it was given, a width that hugs the one
    // line it holds, or a height that grows with the lines it wraps into. The mode moves only the
    // axis it names, so whatever the document said about the other axis still stands.
    const CompileResult sized = CompileUi(Source(
        "<window>"
        "<text id=\"hug\" text=\"Hug\" fontSize=\"20\" lineHeight=\"1.5\"/>"
        "<text id=\"given\" text=\"Given\" fontSize=\"20\" lineHeight=\"1.5\" width=\"80\"/>"
        "<text id=\"block\" text=\"Block\" fontSize=\"20\" lineHeight=\"1.5\" sizing=\"fixed\"/>"
        "<text id=\"narrow\" text=\"Narrow\" fontSize=\"20\" lineHeight=\"1.5\" width=\"80\" sizing=\"autoWidth\"/>"
        "<text id=\"tall\" text=\"Tall\" fontSize=\"20\" lineHeight=\"1.5\" height=\"12\" sizing=\"autoHeight\"/>"
        "<text id=\"short\" text=\"Short\" fontSize=\"20\" lineHeight=\"1.5\" width=\"80\" height=\"90\" sizing=\"fixed\"/>"
        "<text id=\"folded\" text=\"Folded\" fontSize=\"20\" lineHeight=\"1.5\" height=\"12\" sizing=\"AUTO HEIGHT\"/>"
        "<text id=\"unnamed\" text=\"Unnamed\" fontSize=\"20\" lineHeight=\"1.5\" width=\"80\" sizing=\"blend\"/>"
        "</window>"));
    CHECK(test, sized.Ok());
    if (sized.root.has_value()) {
        std::unique_ptr<Element> root = Instantiate(*sized.root, {});
        root->SetViewport(1200.0f, 800.0f);
        FakeMeasurer measurer;
        root->Measure(measurer, 400.0f, 800.0f, true);
        const auto boxOf = [&](const char* id) {
            const Element* node = root->FindById(id);
            return node == nullptr ? Size{-1.0f, -1.0f} : node->Measured();
        };
        // Nothing said about the size: the box is the text, one line of twenty at one and a half.
        CHECK(test, std::fabs(boxOf("hug").width - 20.0f) < 0.5f && std::fabs(boxOf("hug").height - 30.0f) < 0.5f);
        // A size the document gave still wins when nothing was said about the mode.
        CHECK(test, std::fabs(boxOf("given").width - 80.0f) < 0.5f && std::fabs(boxOf("given").height - 30.0f) < 0.5f);
        // A fixed block with no size to be fixed to takes the room it was offered across.
        CHECK(test, std::fabs(boxOf("block").width - 400.0f) < 0.5f && std::fabs(boxOf("block").height - 30.0f) < 0.5f);
        // Auto width hugs one line even though the document asked for eighty.
        CHECK(test, std::fabs(boxOf("narrow").width - 20.0f) < 0.5f && std::fabs(boxOf("narrow").height - 30.0f) < 0.5f);
        // Auto height grows with the lines even though the document asked for twelve.
        CHECK(test, std::fabs(boxOf("tall").width - 20.0f) < 0.5f && std::fabs(boxOf("tall").height - 30.0f) < 0.5f);
        // A fixed block given both sizes keeps them.
        CHECK(test, std::fabs(boxOf("short").width - 80.0f) < 0.5f && std::fabs(boxOf("short").height - 90.0f) < 0.5f);
        // The word is read the way every keyword is: case and the space between words mean nothing.
        CHECK(test, std::fabs(boxOf("folded").height - 30.0f) < 0.5f);
        // A word that names no mode leaves the box as it stood rather than guessing at one.
        CHECK(test, std::fabs(boxOf("unnamed").width - 80.0f) < 0.5f && std::fabs(boxOf("unnamed").height - 30.0f) < 0.5f);
        auto* block = dynamic_cast<TextElement*>(root->FindById("block"));
        auto* narrow = dynamic_cast<TextElement*>(root->FindById("narrow"));
        auto* tall = dynamic_cast<TextElement*>(root->FindById("tall"));
        auto* unnamed = dynamic_cast<TextElement*>(root->FindById("unnamed"));
        CHECK(test, block != nullptr && block->Sizing() == TextSizing::FIXED);
        CHECK(test, narrow != nullptr && narrow->Sizing() == TextSizing::AUTO_WIDTH);
        CHECK(test, tall != nullptr && tall->Sizing() == TextSizing::AUTO_HEIGHT);
        CHECK(test, unnamed != nullptr && unnamed->Sizing() == TextSizing::UNSET);
        // A block that hugs its width is one line, so it is drawn as the one line it was measured as,
        // while a block that hugs nothing is drawn wrapped inside the box it was given. The string a
        // draw carries is what tells them apart, because a box that hugs its text is exactly the
        // size of its neighbour that hugs the same text.
        RecordingRenderer renderer;
        root->Paint(renderer);
        const auto drawOf = [&](const char* drawn) {
            const PaintRecord* found = nullptr;
            for (const PaintRecord& record : renderer.records) {
                if (record.text == drawn) {
                    found = &record;
                }
            }
            return found;
        };
        const PaintRecord* narrowPaint = drawOf("Narrow");
        const PaintRecord* hugPaint = drawOf("Hug");
        CHECK(test, narrowPaint != nullptr && !narrowPaint->wrap);
        CHECK(test, hugPaint != nullptr && hugPaint->wrap);
    }

    // A line height is a multiple of the font size, or the word that leaves the choice to the font,
    // which is the single line a font asks for when it is asked for nothing.
    const CompileResult leading = CompileUi(Source(
        "<window>"
        "<text id=\"auto\" text=\"Auto\" fontSize=\"20\" lineHeight=\"auto\"/>"
        "<text id=\"double\" text=\"Double\" fontSize=\"20\" lineHeight=\"2\"/>"
        "<text id=\"capped\" text=\"Capped\" fontSize=\"20\" lineHeight=\"auto\" maxLines=\"3\"/>"
        "</window>"));
    CHECK(test, leading.Ok());
    if (leading.root.has_value()) {
        std::unique_ptr<Element> root = Instantiate(*leading.root, {});
        root->SetViewport(1200.0f, 800.0f);
        FakeMeasurer measurer;
        root->Measure(measurer, 400.0f, 800.0f, true);
        const auto heightOf = [&](const char* id) {
            const Element* node = root->FindById(id);
            return node == nullptr ? -1.0f : node->Measured().height;
        };
        CHECK(test, std::fabs(heightOf("auto") - 20.0f) < 0.5f);
        CHECK(test, std::fabs(heightOf("double") - 40.0f) < 0.5f);
        // A cap on the lines needs a line height to count with, and the word still counts as one
        // line rather than as no line at all.
        CHECK(test, heightOf("capped") > 0.0f && std::fabs(heightOf("capped") - 20.0f) < 0.5f);
    }
    // A number that is not a line height and a line height below zero are refused rather than read
    // as something they are not.
    CHECK(test, !CompileUi(Source("<window><text text=\"Bad\" lineHeight=\"banana\"/></window>")).Ok());
    CHECK(test, !CompileUi(Source("<window><text text=\"Bad\" lineHeight=\"-1\"/></window>")).Ok());
    // The word reaches the tree as the value that means the font decides, and it is written back as
    // the word rather than as the number no line height is.
    const CompileResult dumped = CompileUi(Source("<window><text id=\"auto\" text=\"Auto\" lineHeight=\"auto\"/></window>"));
    CHECK(test, dumped.Ok());
    if (dumped.root.has_value()) {
        const IrNode* autoNode = nullptr;
        for (const IrNode& child : dumped.root->children) {
            if (child.id == "auto") {
                autoNode = &child;
            }
        }
        CHECK(test, autoNode != nullptr);
        if (autoNode != nullptr) {
            const UiValue* value = FindProperty(*autoNode, "lineHeight");
            CHECK(test, value != nullptr && value->kind == TypeKind::LINE_HEIGHT && std::fabs(value->number) < 0.001f);
        }
        CHECK(test, FormatIr(*dumped.root).find("lineHeight=auto") != std::string::npos);
    }

    // The case a run is written in and the line drawn through it, named the way the text subsystem
    // names them, over the spelling a document may also use for the same thing.
    std::unique_ptr<Element> styled = Layout(
        "<window width=\"400\" height=\"240\"><frame layout=\"none\" width=\"400\" height=\"240\">"
        "<text id=\"upper\" x=\"0\" y=\"0\" text=\"Hello\" textCase=\"upper\" textTransform=\"none\"/>"
        "<text id=\"lower\" x=\"40\" y=\"0\" text=\"Hello\" textCase=\"LOWER\"/>"
        "<text id=\"original\" x=\"80\" y=\"0\" text=\"Hello\" textCase=\"original\" textTransform=\"uppercase\"/>"
        "<text id=\"under\" x=\"120\" y=\"0\" text=\"Link\" textDecoration=\"underline\"/>"
        "<text id=\"strike\" x=\"160\" y=\"0\" text=\"Old\" textDecoration=\"Strikethrough\"/>"
        "<text id=\"clear\" x=\"200\" y=\"0\" text=\"Plain\" textDecoration=\"none\" underline=\"true\" lineThrough=\"true\" overline=\"true\"/>"
        "<text id=\"flags\" x=\"240\" y=\"0\" text=\"Both\" underline=\"true\" lineThrough=\"true\"/>"
        "</frame></window>",
        400.0f, 240.0f);
    CHECK(test, styled != nullptr);
    if (styled != nullptr) {
        const auto displayOf = [&](const char* id) {
            const auto* node = dynamic_cast<const TextElement*>(styled->FindById(id));
            return node == nullptr ? std::string() : node->DisplayText();
        };
        CHECK(test, displayOf("upper") == "HELLO");
        CHECK(test, displayOf("lower") == "hello");
        // The word for the case decides over the appearance the document spelled out separately.
        CHECK(test, displayOf("original") == "Hello");

        RecordingRenderer renderer;
        styled->Paint(renderer);
        const auto recordOf = [&](const char* id) {
            const Element* node = styled->FindById(id);
            return node == nullptr ? nullptr : PaintRecordFor(renderer, node->Bounds());
        };
        const PaintRecord* upperPaint = recordOf("upper");
        CHECK(test, upperPaint != nullptr);
        if (upperPaint != nullptr) {
            // The case reaches the renderer, not just the element's own idea of its text.
            CHECK(test, upperPaint->text == "HELLO");
        }
        const PaintRecord* underPaint = recordOf("under");
        const PaintRecord* strikePaint = recordOf("strike");
        const PaintRecord* clearPaint = recordOf("clear");
        const PaintRecord* flagsPaint = recordOf("flags");
        CHECK(test, underPaint != nullptr && underPaint->underline && !underPaint->lineThrough && !underPaint->overline);
        CHECK(test, strikePaint != nullptr && strikePaint->lineThrough && !strikePaint->underline && !strikePaint->overline);
        // A word that names no line takes every flag away; no word leaves the flags standing.
        CHECK(test, clearPaint != nullptr && !clearPaint->underline && !clearPaint->lineThrough && !clearPaint->overline);
        CHECK(test, flagsPaint != nullptr && flagsPaint->underline && flagsPaint->lineThrough && !flagsPaint->overline);
    }

    // The spellings the text subsystem writes the values in: capitals and the underscores between
    // words mean nothing to a document, so the words a spec names them with are the words that work.
    const CompileResult spec = CompileUi(Source(
        "<window>"
        "<text id=\"wide\" text=\"Wide\" fontSize=\"20\" lineHeight=\"1.5\" width=\"80\" sizing=\"AUTO_WIDTH\"/>"
        "<text id=\"high\" text=\"High\" fontSize=\"20\" lineHeight=\"1.5\" height=\"12\" sizing=\"AUTO_HEIGHT\"/>"
        "<text id=\"held\" text=\"Held\" fontSize=\"20\" lineHeight=\"1.5\" width=\"80\" height=\"90\" sizing=\"FIXED\"/>"
        "<text id=\"as-is\" text=\"Mixed\" textCase=\"ORIGINAL\" textTransform=\"uppercase\"/>"
        "<text id=\"plain\" text=\"Plain\" textDecoration=\"NONE\" underline=\"true\"/>"
        "<text id=\"old\" text=\"Old\" textDecoration=\"STRIKETHROUGH\"/>"
        "</window>"));
    CHECK(test, spec.Ok());
    if (spec.root.has_value()) {
        std::unique_ptr<Element> root = Instantiate(*spec.root, {});
        root->SetViewport(1200.0f, 800.0f);
        FakeMeasurer measurer;
        root->Measure(measurer, 400.0f, 800.0f, true);
        const auto sizingOf = [&](const char* id) {
            const auto* node = dynamic_cast<const TextElement*>(root->FindById(id));
            return node == nullptr ? TextSizing::UNSET : node->Sizing();
        };
        CHECK(test, sizingOf("wide") == TextSizing::AUTO_WIDTH);
        CHECK(test, sizingOf("high") == TextSizing::AUTO_HEIGHT);
        CHECK(test, sizingOf("held") == TextSizing::FIXED);
        // AUTO_WIDTH hugs the line and AUTO_HEIGHT grows with it, read off the box each covered.
        const Element* wideNode = root->FindById("wide");
        const Element* highNode = root->FindById("high");
        CHECK(test, wideNode != nullptr && std::fabs(wideNode->Measured().width - 20.0f) < 0.5f);
        CHECK(test, highNode != nullptr && std::fabs(highNode->Measured().height - 30.0f) < 0.5f);
        // ORIGINAL is the case no transform is, and it decides over the appearance spelled out.
        const auto* asIs = dynamic_cast<const TextElement*>(root->FindById("as-is"));
        CHECK(test, asIs != nullptr && asIs->DisplayText() == "Mixed");
        RecordingRenderer renderer;
        root->Paint(renderer);
        const auto drawOf = [&](const char* drawn) {
            const PaintRecord* found = nullptr;
            for (const PaintRecord& record : renderer.records) {
                if (record.text == drawn) {
                    found = &record;
                }
            }
            return found;
        };
        const PaintRecord* plainPaint = drawOf("Plain");
        const PaintRecord* oldPaint = drawOf("Old");
        // NONE is the decoration no line is, and it takes away the line a flag asked for.
        CHECK(test, plainPaint != nullptr && !plainPaint->underline && !plainPaint->lineThrough && !plainPaint->overline);
        CHECK(test, oldPaint != nullptr && oldPaint->lineThrough && !oldPaint->underline);
    }

    Finish(test, "typography", before);
}

void TestTextSizing(TestContext& test) {
    const int before = test.failed;
    // A block of text is measured before anything is drawn, so the lines it wraps into and the
    // line height it is set on both have to reach the measurement. A block given a width and left
    // to grow is the case where the two disagree: the platform answers with the spacing the font
    // would like, which is a shorter box than the lines the document asked for.
    const CompileResult compiled = CompileUi(Source(
        "<window>"
        "<text id=\"single\" text=\"This line wraps into much more than one line, so the box has to grow to hold it all.\""
        " width=\"150\" fontSize=\"14\" lineHeight=\"1\" sizing=\"autoHeight\"/>"
        "<text id=\"double\" text=\"This line wraps into much more than one line, so the box has to grow to hold it all.\""
        " width=\"150\" fontSize=\"14\" lineHeight=\"2\" sizing=\"autoHeight\"/>"
        "</window>"));
    CHECK(test, compiled.Ok());
    if (compiled.root.has_value()) {
        std::unique_ptr<Element> root = Instantiate(*compiled.root, {});
        root->SetViewport(1200.0f, 800.0f);
        ShapedMeasurer measurer;
        root->Measure(measurer, 400.0f, 800.0f, true);
        const Element* single = root->FindById("single");
        const Element* doubled = root->FindById("double");
        CHECK(test, single != nullptr && doubled != nullptr);
        if (single != nullptr && doubled != nullptr) {
            const float one = single->Measured().height;
            const float two = doubled->Measured().height;
            // The box is a whole number of lines at the height the document set them on: one line
            // of fourteen, or one line of twenty-eight, and never a box a line would hang out of.
            const float lines = one / 14.0f;
            CHECK(test, std::fabs(lines - std::round(lines)) < 0.02f);
            // A sentence cannot wrap into one line of a hundred and fifty pixels, and the lines it
            // holds are what the box grows for.
            CHECK(test, lines > 1.5f);
            CHECK(test, std::fabs(single->Measured().width - 150.0f) < 0.01f);
            // The same text and the same width are twice as tall when the document sets the lines
            // twice as far apart. A measurement that never heard about the line height answers the
            // same box for both, which is the defect this holds shut.
            CHECK(test, std::fabs(two - one * 2.0f) < 0.5f);
        }
    }

    // The style a block is measured with is the style it is drawn with, down to the cap on its
    // lines, and the text it is measured with is the text it draws, which is the case the document
    // asked for rather than the case the document was written in.
    const auto watched = [&](const char* body) {
        StyleWatchingMeasurer watcher;
        const CompileResult watch = CompileUi(Source(std::string("<window>") + body + "</window>"));
        CHECK(test, watch.Ok());
        if (!watch.root.has_value()) {
            return watcher;
        }
        std::unique_ptr<Element> root = Instantiate(*watch.root, {});
        root->SetViewport(1200.0f, 800.0f);
        root->Measure(watcher, 300.0f, 800.0f, true);
        return watcher;
    };
    const StyleWatchingMeasurer wrapped = watched(
        "<text text=\"Wrapped\" width=\"80\" fontSize=\"14\" lineHeight=\"1.42\" maxLines=\"3\" textCase=\"upper\"/>");
    CHECK(test, std::fabs(wrapped.style.fontSize - 14.0f) < 0.01f);
    CHECK(test, std::fabs(wrapped.style.lineHeight - 1.42f) < 0.001f);
    CHECK(test, wrapped.style.maxLines == 3);
    CHECK(test, wrapped.style.wrap);
    CHECK(test, std::fabs(wrapped.measuredWidth - 80.0f) < 0.01f);
    CHECK(test, wrapped.measuredText == "WRAPPED");
    // A block that hugs its width is the one line it holds, so it is measured in no width at all:
    // wrapping it would hand the box the width the document gave instead of the width of its line.
    const StyleWatchingMeasurer hugged =
        watched("<text text=\"Hugged\" width=\"80\" fontSize=\"14\" sizing=\"autoWidth\"/>");
    CHECK(test, !hugged.style.wrap);
    CHECK(test, hugged.measuredWidth >= UNBOUNDED_MEASURE);

    // A block that hugs its width is the width of its one line, whatever width the document gave it
    // and whatever the room around it offered. The box it covers is that line, so a line drawn
    // loosely in a wider box still covers the same one line, and a line the document tracked is a
    // wider line and so a wider box.
    const CompileResult widthways = CompileUi(Source(
        "<window>"
        "<text id=\"hug\" text=\"Hugs its one line\" width=\"200\" fontSize=\"18\" lineHeight=\"1.42\" sizing=\"autoWidth\"/>"
        "<text id=\"given\" text=\"Hugs its one line\" width=\"200\" fontSize=\"18\" lineHeight=\"1.42\"/>"
        "<text id=\"tracked\" text=\"Hugs its one line\" width=\"200\" fontSize=\"18\" lineHeight=\"1.42\" letterSpacing=\"0.1\" sizing=\"autoWidth\"/>"
        "</window>"));
    CHECK(test, widthways.Ok());
    if (widthways.root.has_value()) {
        std::unique_ptr<Element> root = Instantiate(*widthways.root, {});
        root->SetViewport(1200.0f, 800.0f);
        ShapedMeasurer measurer;
        root->Measure(measurer, 400.0f, 800.0f, true);
        const Element* mixture = root->FindById("hug");
        const Element* given = root->FindById("given");
        const Element* tracked = root->FindById("tracked");
        CHECK(test, mixture != nullptr && given != nullptr && tracked != nullptr);
        if (mixture != nullptr && given != nullptr && tracked != nullptr) {
            // The width the document gave is kept by a block that did not ask to hug, and left
            // behind by one that did.
            CHECK(test, std::fabs(given->Measured().width - 200.0f) < 0.01f);
            CHECK(test, mixture->Measured().width < 200.0f);
            // Hugging is one line, and one line of eighteen at one and a half is the height.
            CHECK(test, std::fabs(mixture->Measured().height - 18.0f * 1.42f) < 0.05f);
            // The box covers the line it holds: the line the same text is shaped into at the same
            // size and the same line height is no wider than the box around it.
            TextStyle line = TextStyle{};
            line.fontSize = 18.0f;
            line.lineHeight = 1.42f;
            line.wrap = false;
            CHECK(test, std::fabs(mixture->Measured().width - ShapeText("Hugs its one line", line, UNBOUNDED_MEASURE).width) < 0.5f);
            // A tracked line is a wider line, and the box grew for it rather than staying the width
            // of the untracked line, which is a box the run would hang out of.
            line.letterSpacing = 1.8f;
            CHECK(test, std::fabs(tracked->Measured().width - ShapeText("Hugs its one line", line, UNBOUNDED_MEASURE).width) < 0.5f);
            CHECK(test, tracked->Measured().width > mixture->Measured().width + 20.0f);
        }
    }
    Finish(test, "text sizing", before);
}

void TestWindow(TestContext& test) {
    const int before = test.failed;
    const std::string source =
        "<window id=\"one\" name=\"Window 1\" title=\"Window 1\" position=\"absolute\" x=\"0\" y=\"0\" width=\"800\" height=\"600\" background=\"#FFFFFF\"/>"
        "<window id=\"two\" name=\"Window 2\" title=\"Window 2\" position=\"absolute\" x=\"880\" y=\"0\" width=\"400\" height=\"300\" background=\"#F5F5F7\"/>";
    const ParseResult parsed = ParseXml(Source(source));
    CHECK(test, parsed.root.has_value() && parsed.root->name == "screens" && parsed.root->children.size() == 2);
    if (parsed.root.has_value()) {
        const std::string serialized = SerializeXml(*parsed.root);
        CHECK(test, serialized.find("<screens") == std::string::npos);
        CHECK(test, serialized.find("<window") != std::string::npos && serialized.find("</window>") == std::string::npos);
    }
    const CompileResult nested = CompileUi(Source("<window><window id=\"inner\"/></window>"));
    CHECK(test, !nested.Ok());
    const CompileResult compiled = CompileUi(Source(source));
    CHECK(test, compiled.Ok() && compiled.root.has_value() && compiled.root->name == "screens" && compiled.root->children.size() == 2);
    if (compiled.root.has_value()) {
        std::unique_ptr<Element> root = Instantiate(*compiled.root, {});
        FakeMeasurer measurer;
        root->SetViewport(800.0f, 600.0f);
        root->Measure(measurer, 1600.0f, 600.0f, true);
        root->Arrange(Rect{0.0f, 0.0f, 1600.0f, 600.0f});
        auto* one = dynamic_cast<WindowElement*>(root->FindById("one"));
        auto* two = dynamic_cast<WindowElement*>(root->FindById("two"));
        CHECK(test, one != nullptr && one->Kind() == NodeKind::WINDOW && one->Title() == "Window 1");
        CHECK(test, one != nullptr && std::fabs(one->Bounds().width - 800.0f) < 0.5f && std::fabs(one->Bounds().height - 600.0f) < 0.5f);
        CHECK(test, two != nullptr && two->Title() == "Window 2" && std::fabs(two->Bounds().x - 880.0f) < 0.5f);
        CHECK(test, two != nullptr && std::fabs(two->Bounds().width - 400.0f) < 0.5f && NearColor(two->BackgroundColor(), *ParseColor("#F5F5F7")));
    }
    const CompileResult limited = CompileUi(Source(
        "<window id=\"phone\" mobile=\"true\" tablet=\"false\" desktop=\"false\" width=\"390\" height=\"800\"/>"
        "<window id=\"desk\" mobile=\"false\" tablet=\"false\" desktop=\"true\" width=\"1280\" height=\"800\"/>"));
    CHECK(test, limited.Ok() && limited.root.has_value() && limited.root->children.size() == 2);
    if (limited.root.has_value() && limited.root->children.size() == 2) {
        const IrNode& phone = limited.root->children[0];
        const IrNode& desk = limited.root->children[1];
        CHECK(test, WindowLaunchesOn(phone, DeviceClass::MOBILE) && !WindowLaunchesOn(phone, DeviceClass::TABLET) &&
                        !WindowLaunchesOn(phone, DeviceClass::DESKTOP));
        CHECK(test, WindowLaunchesOn(desk, DeviceClass::DESKTOP) && !WindowLaunchesOn(desk, DeviceClass::MOBILE));
        CHECK(test, compiled.root.has_value() && !compiled.root->children.empty() &&
                        WindowLaunchesOn(compiled.root->children[0], DeviceClass::MOBILE));
    }
    CHECK(test, DeviceClassForWidth(390.0f) == DeviceClass::MOBILE);
    CHECK(test, DeviceClassForWidth(800.0f) == DeviceClass::TABLET);
    CHECK(test, DeviceClassForWidth(1280.0f) == DeviceClass::DESKTOP);
    Finish(test, "window", before);
}

void TestBoard(TestContext& test) {
    const int before = test.failed;
    const CompileResult nested = CompileUi(Source("<window><board id=\"inside\"/></window>"));
    CHECK(test, !nested.Ok());
    const CompileResult compiled = CompileUi(Source(
        "<window id=\"home\" width=\"800\" height=\"600\"/>"
        "<board id=\"card\" name=\"Card\" width=\"320\" height=\"180\" background=\"#FFFFFF\">"
        "<text id=\"label\" text=\"Hello\"/>"
        "</board>"));
    CHECK(test, compiled.Ok() && compiled.root.has_value() && compiled.root->name == "screens" && compiled.root->children.size() == 2);
    if (compiled.root.has_value()) {
        std::unique_ptr<Element> root = Instantiate(*compiled.root, {});
        FakeMeasurer measurer;
        root->Measure(measurer, 1600.0f, 600.0f, true);
        root->Arrange(Rect{0.0f, 0.0f, 1600.0f, 600.0f});
        auto* board = dynamic_cast<BoardElement*>(root->FindById("card"));
        CHECK(test, board != nullptr && board->Kind() == NodeKind::BOARD);
        CHECK(test, board != nullptr && std::fabs(board->Bounds().width - 320.0f) < 0.5f && board->FindById("label") != nullptr);
        CHECK(test, root->FindById("home") != nullptr && root->FindById("home")->Kind() == NodeKind::WINDOW);
    }
    DesignDocument document;
    CHECK(test, document.LoadText("<window id=\"home\"/><board id=\"card\"><text text=\"Hello\"/></board>", "memory.xml"));
    CHECK(test, document.AddBoardContent({1}, {0}));
    CHECK(test, document.NodeAt({0}) != nullptr && document.NodeAt({0})->children.size() == 1);
    CHECK(test, document.NodeAt({0})->children[0].name == "text");
    CHECK(test, document.NodeAt({1}) != nullptr && document.NodeAt({1})->name == "board" && document.NodeAt({1})->children.size() == 1);
    CHECK(test, document.Text().find("<screens") == std::string::npos);
    Finish(test, "board", before);
}

void TestIrDump(TestContext& test) {
    const int before = test.failed;
    const CompileResult compiled = CompileUi(Source("<window><frame id=\"box\" name=\"Box\"/></window>"));
    CHECK(test, compiled.Ok());
    const std::string dump = FormatIr(*compiled.root);
    CHECK(test, dump.find("frame") != std::string::npos);
    CHECK(test, dump.find("box") != std::string::npos);
    Finish(test, "ir", before);
}

const UiValue* PropertyOf(const IrNode& node, const char* name) {
    for (const auto& property : node.properties) {
        if (property.first == name) {
            return &property.second;
        }
    }
    return nullptr;
}

void TestFoundations(TestContext& test) {
    const int before = test.failed;
    CHECK(test, ParseColor("$brand-primary").has_value() && NearColor(*ParseColor("$brand-primary"), *ParseColor("#0E7AFF")));
    CHECK(test, ParseColor("$color.primary").has_value() && NearColor(*ParseColor("$color.primary"), *ParseColor("$brand-primary")));
    const CompileResult compiled = CompileUi(Source(
        "<window><frame id=\"card\" background=\"$bg-secondary\" padding=\"$space-4\" radius=\"$radius-lg\" width=\"$width-docs\">"
        "<text text=\"Title\" fontSize=\"$type-h1\" fontWeight=\"$weight-semibold\" letterSpacing=\"$tracking-h1\" textColor=\"$text-primary\"/>"
        "</frame></window>"));
    CHECK(test, compiled.Ok() && compiled.root.has_value() && !compiled.root->children.empty());
    if (compiled.root.has_value() && !compiled.root->children.empty()) {
        const IrNode& card = compiled.root->children[0];
        const UiValue* background = PropertyOf(card, "background");
        const UiValue* padding = PropertyOf(card, "padding");
        const UiValue* radius = PropertyOf(card, "radius");
        const UiValue* width = PropertyOf(card, "width");
        CHECK(test, background != nullptr && NearColor(background->color, *ParseColor("#F5F5F7")));
        CHECK(test, padding != nullptr && std::fabs(padding->edges.left.value - 16.0f) < 0.01f);
        CHECK(test, radius != nullptr && std::fabs(radius->length.value - 12.0f) < 0.01f);
        CHECK(test, width != nullptr && std::fabs(width->length.value - 760.0f) < 0.01f);
    }
    const CompileResult replaced = CompileUi(Source(
        "<window><tokens><color name=\"brand-primary\" value=\"#112233\"/></tokens>"
        "<frame background=\"$brand-primary\"/></window>"));
    CHECK(test, replaced.Ok() && replaced.root.has_value() && !replaced.root->children.empty());
    if (replaced.root.has_value() && !replaced.root->children.empty()) {
        const UiValue* background = PropertyOf(replaced.root->children[0], "background");
        CHECK(test, background != nullptr && NearColor(background->color, *ParseColor("#112233")));
    }
    const CompileResult removed = CompileUi(Source(
        "<window><tokens><color name=\"brand-primary\" removed=\"true\"/></tokens>"
        "<frame background=\"$brand-primary\"/></window>"));
    CHECK(test, !removed.Ok());
    DesignDocument document;
    CHECK(test, document.LoadText("<window width=\"800\" height=\"600\"><frame/></window>", "memory.xml"));
    CHECK(test, document.SetToken("color", "ink", "#ABCDEF"));
    CHECK(test, document.Text().find("ink") != std::string::npos);
    CHECK(test, document.RemoveToken("color", "brand-primary", true));
    CHECK(test, document.Text().find("removed=\"true\"") != std::string::npos);
    Finish(test, "foundations", before);
}

}  // namespace

// A component is the definition of something that is placed more than once. The four nodes of the
// component system are the definition itself, the set that gathers the variants of one component,
// the copy placed from a definition, and the place inside a definition that a copy fills. What is
// proved here is that a copy holds the content of the definition it named, that a copy of a set
// draws the variant it asked for, that a copy replaces the children it names, and that a copy fills
// the places its definition offers.
void TestComponents(TestContext& test) {
    const int before = test.failed;

    // The four nodes are components the compiler knows and the designer can offer, each with the
    // values it takes.
    CHECK(test, FindComponent("component") != nullptr);
    CHECK(test, FindComponent("componentSet") != nullptr);
    CHECK(test, FindComponent("instance") != nullptr);
    CHECK(test, FindComponent("slot") != nullptr);
    CHECK(test, std::string(NodeKindName(NodeKind::COMPONENT)) == "component");
    CHECK(test, std::string(NodeKindName(NodeKind::COMPONENTSET)) == "componentSet");
    CHECK(test, std::string(NodeKindName(NodeKind::INSTANCE)) == "instance");
    CHECK(test, std::string(NodeKindName(NodeKind::SLOT)) == "slot");
    CHECK(test, ComponentAcceptsProperty("component", "variant"));
    CHECK(test, ComponentAcceptsProperty("component", "layout"));
    CHECK(test, ComponentAcceptsProperty("componentSet", "gap"));
    CHECK(test, ComponentAcceptsProperty("instance", "component"));
    CHECK(test, ComponentAcceptsProperty("instance", "variant"));
    CHECK(test, ComponentAcceptsProperty("slot", "text"));

    // A definition and a copy of it. The copy holds what the definition holds, is a node of its own
    // kind, and knows the definition it was placed from.
    std::unique_ptr<Element> placed = Layout(
        "<window width=\"320\" height=\"200\"><frame id=\"stage\" width=\"320\" height=\"200\">"
        "<component id=\"badgeMaster\" name=\"Badge\" variant=\"Primary\" x=\"0\" y=\"0\" width=\"90\" height=\"30\">"
        "<text id=\"badgeLabel\" text=\"Ok\" width=\"90\" height=\"30\"/></component>"
        "<instance id=\"badge\" component=\"Badge\" position=\"absolute\" x=\"120\" y=\"0\" width=\"90\" height=\"30\"/>"
        "<instance id=\"missing\" component=\"Nothing\" position=\"absolute\" x=\"120\" y=\"60\" width=\"90\" height=\"30\"/>"
        "</frame></window>",
        320.0f, 200.0f);
    CHECK(test, placed != nullptr);
    if (placed != nullptr) {
        ComponentElement* master = dynamic_cast<ComponentElement*>(placed->FindById("badgeMaster"));
        CHECK(test, master != nullptr);
        if (master != nullptr) {
            CHECK(test, master->Kind() == NodeKind::COMPONENT);
            CHECK(test, master->Variant() == "Primary" && master->HasVariant());
            CHECK(test, master->BadgeVisible());
        }
        InstanceElement* instance = dynamic_cast<InstanceElement*>(placed->FindById("badge"));
        CHECK(test, instance != nullptr && instance->Kind() == NodeKind::INSTANCE);
        if (instance != nullptr) {
            CHECK(test, instance->ComponentName() == "Badge");
            CHECK(test, instance->Resolved() && !instance->Detached());
            CHECK(test, instance->OverrideCount() == 0);
            // The copy holds the content of its definition, and the content is a node of the copy
            // rather than the definition's node reached through it.
            CHECK(test, instance->Children().size() == 1);
            Element* heldLabel = instance->FindById("badgeLabel");
            CHECK(test, heldLabel != nullptr && heldLabel->Kind() == NodeKind::TEXT);
            if (heldLabel != nullptr) {
                const TextElement* label = dynamic_cast<const TextElement*>(heldLabel);
                CHECK(test, label != nullptr && label->Text() == "Ok");
            }
            // The box of the copy is the box the copy was given, not the box its definition was
            // authored at, so a copy can be placed at any size.
            CHECK(test, std::fabs(instance->Bounds().width - 90.0f) < 0.01f);
        }
        // A copy that names a definition the document does not hold is left unresolved and draws a
        // marked placeholder, so the mistake is seen rather than silently blank.
        InstanceElement* ghost = dynamic_cast<InstanceElement*>(placed->FindById("missing"));
        CHECK(test, ghost != nullptr && !ghost->Resolved());
        if (ghost != nullptr && !ghost->Resolved()) {
            RecordingRenderer renderer;
            placed->Paint(renderer);
            CHECK(test, PaintRecordFor(renderer, ghost->Bounds()) != nullptr);
        }
    }

    // A set holds the variants of one component, and a copy of the set draws the variant it asked
    // for rather than whichever variant comes first.
    std::unique_ptr<Element> varied = Layout(
        "<window width=\"400\" height=\"220\"><frame id=\"variantStage\" width=\"400\" height=\"220\">"
        "<componentSet id=\"buttonSet\" name=\"Button\" layout=\"horizontal\" gap=\"16\" width=\"320\" height=\"40\">"
        "<component id=\"vPrimary\" name=\"Button\" variant=\"Primary\" width=\"140\" height=\"40\">"
        "<text id=\"primaryLabel\" text=\"Primary\" width=\"140\" height=\"40\"/></component>"
        "<component id=\"vDanger\" name=\"Button\" variant=\"Danger\" width=\"140\" height=\"40\">"
        "<text id=\"dangerLabel\" text=\"Danger\" width=\"140\" height=\"40\"/></component>"
        "</componentSet>"
        "<instance id=\"dangerCopy\" component=\"Button\" variant=\"Danger\" position=\"absolute\" x=\"0\" y=\"120\" width=\"140\" height=\"40\"/>"
        "</frame></window>",
        400.0f, 220.0f);
    CHECK(test, varied != nullptr);
    if (varied != nullptr) {
        ComponentSetElement* set = dynamic_cast<ComponentSetElement*>(varied->FindById("buttonSet"));
        CHECK(test, set != nullptr && set->Kind() == NodeKind::COMPONENTSET);
        if (set != nullptr) {
            CHECK(test, set->VariantCount() == 2);
            CHECK(test, set->VariantAt(0) != nullptr && set->VariantAt(0)->Variant() == "Primary");
            CHECK(test, set->VariantAt(1) != nullptr && set->VariantAt(1)->Variant() == "Danger");
            CHECK(test, set->VariantAt(2) == nullptr && set->VariantAt(-1) == nullptr);
            // A variant that is asked for is found wherever it sits; a variant that is not is
            // answered with the first definition rather than with nothing.
            CHECK(test, set->VariantNamed("Danger") == set->VariantAt(1));
            CHECK(test, set->VariantNamed("Missing") == set->VariantAt(0));
        }
        InstanceElement* danger = dynamic_cast<InstanceElement*>(varied->FindById("dangerCopy"));
        CHECK(test, danger != nullptr && danger->Resolved() && danger->Variant() == "Danger");
        if (danger != nullptr) {
            CHECK(test, danger->FindById("dangerLabel") != nullptr);
            CHECK(test, danger->FindById("primaryLabel") == nullptr);
        }
    }

    // A copy replaces the children its definition named, so one copy of a definition can read
    // differently from another without a second definition being written.
    std::unique_ptr<Element> overridden = Layout(
        "<window width=\"300\" height=\"160\"><frame id=\"overrideStage\" width=\"300\" height=\"160\">"
        "<component id=\"chipMaster\" name=\"Chip\" x=\"0\" y=\"0\" width=\"120\" height=\"32\">"
        "<text id=\"chipLabel\" name=\"Label\" text=\"Ok\" width=\"120\" height=\"32\"/></component>"
        "<instance id=\"savedChip\" component=\"Chip\" position=\"absolute\" x=\"140\" y=\"0\" width=\"120\" height=\"32\">"
        "<text name=\"Label\" text=\"Save\" width=\"120\" height=\"32\"/></instance>"
        "</frame></window>",
        300.0f, 160.0f);
    CHECK(test, overridden != nullptr);
    if (overridden != nullptr) {
        InstanceElement* saved = dynamic_cast<InstanceElement*>(overridden->FindById("savedChip"));
        CHECK(test, saved != nullptr && saved->Resolved());
        if (saved != nullptr) {
            CHECK(test, saved->OverrideCount() == 1);
            CHECK(test, saved->Children().size() == 1);
            const TextElement* text = saved->Children().empty() ? nullptr : dynamic_cast<TextElement*>(saved->Children().front().get());
            CHECK(test, text != nullptr && text->Text() == "Save");
        }
        // The definition is untouched by the copy: the master still reads the way it was written.
        ComponentElement* chip = dynamic_cast<ComponentElement*>(overridden->FindById("chipMaster"));
        CHECK(test, chip != nullptr);
        if (chip != nullptr && !chip->Children().empty()) {
            const TextElement* masterText = dynamic_cast<TextElement*>(chip->Children().front().get());
            CHECK(test, masterText != nullptr && masterText->Text() == "Ok");
        }
    }

    // A slot is the place inside a definition a copy fills with content of its own. A copy that
    // fills it holds the content it sent, and a copy that does not holds the slot and its fallback.
    std::unique_ptr<Element> slotted = Layout(
        "<window width=\"440\" height=\"220\"><frame id=\"slotStage\" width=\"440\" height=\"220\">"
        "<component id=\"panelMaster\" name=\"Panel\" x=\"0\" y=\"0\" width=\"200\" height=\"100\" layout=\"vertical\">"
        "<text id=\"panelTitle\" text=\"Title\" width=\"200\" height=\"20\"/>"
        "<slot id=\"slotBody\" name=\"body\" text=\"Drop content\" width=\"200\" height=\"80\"/></component>"
        "<instance id=\"filledPanel\" component=\"Panel\" position=\"absolute\" x=\"220\" y=\"0\" width=\"200\" height=\"100\">"
        "<frame slot=\"body\" id=\"panelFiller\" width=\"200\" height=\"80\" background=\"#010203\"/></instance>"
        "<instance id=\"emptyPanel\" component=\"Panel\" position=\"absolute\" x=\"220\" y=\"110\" width=\"200\" height=\"100\"/>"
        "</frame></window>",
        440.0f, 220.0f);
    CHECK(test, slotted != nullptr);
    if (slotted != nullptr) {
        ComponentElement* panel = dynamic_cast<ComponentElement*>(slotted->FindById("panelMaster"));
        CHECK(test, panel != nullptr && panel->Children().size() == 2);
        if (panel != nullptr && panel->Children().size() == 2) {
            SlotElement* body = dynamic_cast<SlotElement*>(panel->Children().back().get());
            CHECK(test, body != nullptr && body->Kind() == NodeKind::SLOT);
            if (body != nullptr) {
                CHECK(test, body->Name() == "body");
                CHECK(test, body->FallbackText() == "Drop content");
                CHECK(test, !body->IsFilled());
            }
        }
        InstanceElement* filled = dynamic_cast<InstanceElement*>(slotted->FindById("filledPanel"));
        CHECK(test, filled != nullptr && filled->Resolved());
        if (filled != nullptr) {
            CHECK(test, filled->Children().size() == 2);
            CHECK(test, filled->FindById("panelFiller") != nullptr);
            if (filled->Children().size() == 2) {
                // The slot was replaced by the content the copy sent to it, so no slot is left.
                CHECK(test, dynamic_cast<SlotElement*>(filled->Children().back().get()) == nullptr);
                CHECK(test, filled->Children().back()->Id() == "panelFiller");
            }
        }
        // An unfilled slot keeps its fallback, and the fallback is drawn rather than only kept.
        InstanceElement* empty = dynamic_cast<InstanceElement*>(slotted->FindById("emptyPanel"));
        CHECK(test, empty != nullptr && empty->Resolved());
        if (empty != nullptr && empty->Children().size() == 2) {
            SlotElement* body = dynamic_cast<SlotElement*>(empty->Children().back().get());
            CHECK(test, body != nullptr && !body->IsFilled() && body->FallbackText() == "Drop content");
            RecordingRenderer renderer;
            slotted->Paint(renderer);
            bool fallbackDrawn = false;
            for (const PaintRecord& record : renderer.records) {
                if (record.text == "Drop content") {
                    fallbackDrawn = true;
                }
            }
            CHECK(test, fallbackDrawn);
        }
    }

    // The slot of a child is the one part of the component language every component can hold, since
    // which places a definition offers is only known once the definition is placed. Everything else
    // a component does not declare is still refused.
    CHECK(test, CompileUi(Source("<window><frame slot=\"body\" width=\"10\" height=\"10\"/></window>")).Ok());
    CHECK(test, !CompileUi(Source("<window><frame nonsense=\"1\"/></window>")).Ok());
    CHECK(test, CompileUi(Source("<window><instance component=\"Chip\" custom=\"7\"/></window>")).Ok());

    // A copy names the layer it replaces by the id the layer carries, so one copy can change one
    // layer of a definition without the document having to invent a second name for it. The id of a
    // child of a copy is the id of the layer it stands for rather than the id of a new layer, so it
    // is allowed to repeat it.
    std::unique_ptr<Element> byId = Layout(
        "<window width=\"300\" height=\"160\"><frame id=\"idStage\" width=\"300\" height=\"160\">"
        "<component id=\"tagMaster\" name=\"Tag\" x=\"0\" y=\"0\" width=\"120\" height=\"32\">"
        "<text id=\"tagLabel\" text=\"Ok\" width=\"120\" height=\"32\"/></component>"
        "<instance id=\"renamedTag\" component=\"Tag\" position=\"absolute\" x=\"140\" y=\"0\" width=\"120\" height=\"32\">"
        "<text id=\"tagLabel\" text=\"Renamed\" width=\"120\" height=\"32\"/></instance>"
        "</frame></window>",
        300.0f, 160.0f);
    CHECK(test, byId != nullptr);
    if (byId != nullptr) {
        InstanceElement* renamed = dynamic_cast<InstanceElement*>(byId->FindById("renamedTag"));
        CHECK(test, renamed != nullptr && renamed->Resolved());
        if (renamed != nullptr) {
            CHECK(test, renamed->OverrideCount() == 1);
            CHECK(test, renamed->Children().size() == 1);
            const TextElement* text = renamed->Children().empty() ? nullptr : dynamic_cast<TextElement*>(renamed->Children().front().get());
            CHECK(test, text != nullptr && text->Text() == "Renamed");
        }
        // The layer of the definition is untouched, so the id names the definition's layer in one
        // place and the copy's replacement of it in the other.
        ComponentElement* tag = dynamic_cast<ComponentElement*>(byId->FindById("tagMaster"));
        CHECK(test, tag != nullptr && tag->FindById("tagLabel") != nullptr);
    }
    // Repeating an id outside a copy is still the same second name for one layer, and it is refused.
    CHECK(test, !CompileUi(Source("<window><frame id=\"twice\" width=\"10\" height=\"10\"/><frame id=\"twice\" width=\"10\" height=\"10\"/></window>")).Ok());

    // A copy replaces a layer wherever the definition keeps it, not only at the top, so a definition
    // can be built out of boxes the way a screen is and a copy can still reach inside one.
    std::unique_ptr<Element> nested = Layout(
        "<window width=\"300\" height=\"200\"><frame id=\"nestedStage\" width=\"300\" height=\"200\">"
        "<component id=\"cardMaster\" name=\"Card\" x=\"0\" y=\"0\" width=\"160\" height=\"100\">"
        "<frame id=\"cardBody\" width=\"160\" height=\"100\">"
        "<text id=\"cardTitle\" text=\"Title\" width=\"160\" height=\"20\"/></frame></component>"
        "<instance id=\"namedCard\" component=\"Card\" position=\"absolute\" x=\"0\" y=\"110\" width=\"160\" height=\"100\">"
        "<text id=\"cardTitle\" text=\"Named\" width=\"160\" height=\"20\"/></instance>"
        "</frame></window>",
        300.0f, 200.0f);
    CHECK(test, nested != nullptr);
    if (nested != nullptr) {
        InstanceElement* named = dynamic_cast<InstanceElement*>(nested->FindById("namedCard"));
        CHECK(test, named != nullptr && named->Resolved() && named->OverrideCount() == 1);
        if (named != nullptr) {
            Element* body = named->FindById("cardBody");
            CHECK(test, body != nullptr);
            const TextElement* title = body == nullptr ? nullptr : dynamic_cast<TextElement*>(body->FindById("cardTitle"));
            CHECK(test, title != nullptr && title->Text() == "Named");
        }
    }

    // The variants of one component stand beside each other rather than replacing one another, so a
    // usage that names a variant is drawn from the definition that draws it and a usage that names
    // none is drawn from the first one. A definition of two children is wrapped in a box, which is
    // what a usage of one becomes, so the whole of what a usage drew can be read off the tree.
    std::unique_ptr<Element> macros = Layout(
        "<window width=\"400\" height=\"220\"><frame id=\"macroStage\" width=\"400\" height=\"220\">"
        "<componentSet name=\"Pill\" layout=\"horizontal\" gap=\"8\" width=\"300\" height=\"40\">"
        "<component name=\"Pill\" variant=\"Primary\" width=\"140\" height=\"40\">"
        "<text id=\"primaryA\" text=\"A\" width=\"70\" height=\"40\"/><text id=\"primaryB\" text=\"B\" width=\"70\" height=\"40\"/>"
        "</component>"
        "<component name=\"Pill\" variant=\"Danger\" width=\"140\" height=\"40\">"
        "<text id=\"dangerA\" text=\"A\" width=\"70\" height=\"40\"/><text id=\"dangerB\" text=\"B\" width=\"70\" height=\"40\"/>"
        "</component>"
        "</componentSet>"
        "<Pill variant=\"Danger\" id=\"dangerUse\" position=\"absolute\" x=\"0\" y=\"120\" width=\"140\" height=\"40\"/>"
        "<Pill id=\"plainUse\" position=\"absolute\" x=\"160\" y=\"120\" width=\"140\" height=\"40\"/>"
        "</frame></window>",
        400.0f, 220.0f);
    CHECK(test, macros != nullptr);
    if (macros != nullptr) {
        Element* dangerUse = macros->FindById("dangerUse");
        Element* plainUse = macros->FindById("plainUse");
        CHECK(test, dangerUse != nullptr && plainUse != nullptr);
        if (dangerUse != nullptr && plainUse != nullptr) {
            CHECK(test, dangerUse->FindById("dangerA") != nullptr && dangerUse->FindById("dangerB") != nullptr);
            CHECK(test, dangerUse->FindById("primaryA") == nullptr);
            CHECK(test, plainUse->FindById("primaryA") != nullptr && plainUse->FindById("primaryB") != nullptr);
            CHECK(test, plainUse->FindById("dangerA") == nullptr);
        }
    }
    // A definition written twice is a definition one of which can never be placed, so it is reported
    // rather than the second silently replacing the first.
    CHECK(test, !CompileUi(Source("<window><component name=\"Twice\"><text text=\"a\"/></component>"
                                  "<component name=\"Twice\"><text text=\"b\"/></component></window>"))
                     .Ok());

    // What a definition is for is drawn as the tip it shows while it is hovered, so a description is
    // read off the canvas rather than only out of the node it was written on.
    std::unique_ptr<Element> described = Layout(
        "<window width=\"200\" height=\"100\"><frame id=\"describedStage\" width=\"200\" height=\"100\">"
        "<component id=\"noteMaster\" name=\"Note\" description=\"Shown in the layer list\" width=\"80\" height=\"40\">"
        "<text text=\"Note\" width=\"80\" height=\"40\"/></component>"
        "</frame></window>",
        200.0f, 100.0f);
    CHECK(test, described != nullptr);
    if (described != nullptr) {
        ComponentElement* note = dynamic_cast<ComponentElement*>(described->FindById("noteMaster"));
        CHECK(test, note != nullptr && note->Description() == "Shown in the layer list");
        if (note != nullptr) {
            note->SetPointerHover(true);
            RecordingRenderer renderer;
            described->Paint(renderer);
            bool tipDrawn = false;
            for (const PaintRecord& record : renderer.records) {
                if (record.text == "Shown in the layer list") {
                    tipDrawn = true;
                }
            }
            CHECK(test, tipDrawn);
            note->SetPointerHover(false);
        }
    }

    // A child of a copy that names no layer of its definition and no place of it cannot be placed by
    // anything, so it is reported. What is reported does not stop the document, because the rest of
    // it is still drawn: a copy with one child too many still draws the rest of what it holds.
    const std::string copyHost =
        "<component name=\"Note\" width=\"80\" height=\"40\"><text name=\"Text\" text=\"Note\" width=\"80\" height=\"40\"/></component>";
    const CompileResult mismatched = CompileUi(Source("<window width=\"200\" height=\"100\">" + copyHost +
                                                      "<instance component=\"Note\"><frame name=\"Nowhere\" width=\"10\" height=\"10\"/></instance></window>"));
    CHECK(test, mismatched.Ok());
    CHECK(test, !mismatched.diagnostics.empty());
    if (!mismatched.diagnostics.empty()) {
        CHECK(test, mismatched.diagnostics.front().code == DIAGNOSTIC_UNKNOWN_OVERRIDE);
        CHECK(test, mismatched.diagnostics.front().severity == DiagnosticSeverity::WARNING);
    }
    // A place the definition does not offer is reported on the child that named it, and a child that
    // names a layer or a place the definition does offer is reported as nothing at all.
    const CompileResult wrongPlace = CompileUi(Source("<window width=\"200\" height=\"100\">" + copyHost +
                                                      "<instance component=\"Note\"><frame slot=\"nowhere\" width=\"10\" height=\"10\"/></instance></window>"));
    CHECK(test, wrongPlace.Ok() && !wrongPlace.diagnostics.empty());
    if (!wrongPlace.diagnostics.empty()) {
        CHECK(test, wrongPlace.diagnostics.front().code == DIAGNOSTIC_UNKNOWN_OVERRIDE);
        CHECK(test, wrongPlace.diagnostics.front().message.find("nowhere") != std::string::npos);
    }
    const CompileResult replaced = CompileUi(Source("<window width=\"200\" height=\"100\">" + copyHost +
                                                    "<instance component=\"Note\"><text name=\"Text\" text=\"Save\" width=\"80\" height=\"40\"/></instance></window>"));
    CHECK(test, replaced.Ok());
    bool complained = false;
    for (const Diagnostic& diagnostic : replaced.diagnostics) {
        if (diagnostic.code == DIAGNOSTIC_UNKNOWN_OVERRIDE) {
            complained = true;
        }
    }
    CHECK(test, !complained);

    // The editor can add every one of the four, and what it adds is a document the compiler accepts:
    // a definition born with a body, a set born with two variants, a copy that names a definition,
    // and a slot that names the place it fills.
    DesignDocument editor;
    CHECK(test, editor.LoadText("<window width=\"800\" height=\"600\"><frame name=\"Home\"/></window>", "memory.xml"));
    const char* addedNodes[] = {"component", "componentSet", "instance", "slot", "embed", "link"};
    for (const char* nodeName : addedNodes) {
        std::vector<int> added;
        CHECK(test, editor.InsertChild({0}, nodeName, added));
        CHECK(test, editor.NodeAt(added) != nullptr);
        if (editor.NodeAt(added) != nullptr) {
            CHECK(test, editor.NodeAt(added)->name == nodeName);
        }
    }
    // A definition the editor adds is born with a body and a set with its variants, which is what
    // makes the document a new node produces one the compiler can draw.
    CHECK(test, CompileUi(Source(editor.Text())).Ok());

    Finish(test, "components", before);
}

// A document, and the documents it is made of. A fixture directory holds a screen that imports two
// libraries, embeds a part, and links to a screen of its own; two documents that import each other;
// and one that names a file that is not there. Every one of them is a real file, because what is
// being tested is the reading of files rather than a tree built in memory.
std::filesystem::path Fixture(const char* name) {
    return std::filesystem::path(RUNTIME_SOURCE_DIR) / "tests" / "fixtures" / name;
}

bool HasDiagnostic(const std::vector<Diagnostic>& diagnostics, const char* code) {
    for (const Diagnostic& diagnostic : diagnostics) {
        if (diagnostic.code == code) {
            return true;
        }
    }
    return false;
}

std::unique_ptr<Element> LayoutFile(const std::filesystem::path& path, float width, float height,
                                    const LinkHandler& onLink = {}) {
    const CompileResult compiled = CompileUiFile(path);
    if (!compiled.Ok()) {
        return nullptr;
    }
    std::unique_ptr<Element> root = Instantiate(*compiled.root, {}, onLink);
    if (root == nullptr) {
        return nullptr;
    }
    FakeMeasurer measurer;
    root->SetViewport(width, height);
    root->Measure(measurer, width, height, true);
    root->Arrange(Rect{0.0f, 0.0f, width, height});
    return root;
}

void TestProjects(TestContext& test) {
    const int before = test.failed;

    // A library is a document written to be borrowed from, so it is read on its own as well as
    // imported. Reading it alone is what lets the file that holds the definitions be checked without
    // a screen wrapped around it.
    const CompileResult library = CompileUiFile(Fixture("Theme.xml"));
    CHECK(test, library.Ok());
    CHECK(test, library.root.has_value() && library.root->name == "library");
    CHECK(test, library.sources.size() == 1);

    // The screen is four files: itself, the two libraries it imports, and the part it embeds. Every
    // one of them is kept, so a diagnostic found in one is shown beside that one.
    const CompileResult screen = CompileUiFile(Fixture("Screen.xml"));
    CHECK(test, screen.Ok());
    if (!screen.Ok()) {
        PrintDiagnostics(screen.sources, screen.diagnostics, std::cerr);
    }
    CHECK(test, screen.sources.size() == 4);
    CHECK(test, screen.diagnostics.empty());

    // What an import borrows is available: the screen places definitions that were written in another
    // file, without that file being drawn. What it borrows is not drawn here, so the definitions the
    // libraries hold are not nodes of the screen: the screen holds the two copies and nothing else.
    std::unique_ptr<Element> root = LayoutFile(Fixture("Screen.xml"), 400.0f, 320.0f);
    CHECK(test, root != nullptr);
    if (root != nullptr) {
        Element* card = root->FindById("card");
        CHECK(test, card != nullptr);
        if (card != nullptr) {
            CHECK(test, card->Children().size() == 5);
        }
        CHECK(test, root->FindById("heroBadge") != nullptr);
        CHECK(test, root->FindById("tagChip") != nullptr);
        // The copy holds the content of the definition the library wrote: a component imported from a
        // file is a component like any other, not a name that stands for nothing.
        InstanceElement* badge = dynamic_cast<InstanceElement*>(root->FindById("heroBadge"));
        CHECK(test, badge != nullptr && badge->Resolved());
        if (badge != nullptr) {
            const TextElement* label = dynamic_cast<TextElement*>(badge->FindById("badgeLabel"));
            CHECK(test, label != nullptr && label->Text() == "New");
        }

        // An embed is a document inlined where it stands: the content of the named file is part of
        // the tree, and the node keeps the name of the file it came from.
        EmbedElement* embed = dynamic_cast<EmbedElement*>(root->FindById("partBox"));
        CHECK(test, embed != nullptr);
        if (embed != nullptr) {
            CHECK(test, embed->Source() == "Part.xml");
            const TextElement* part = dynamic_cast<TextElement*>(embed->FindById("partLabel"));
            CHECK(test, part != nullptr && part->Text() == "Embedded part");
        }

        // A link is a frame that carries somewhere to go. What going there means is the program's to
        // decide, so what is checked here is that the names arrived rather than that anything was
        // opened.
        LinkElement* docs = dynamic_cast<LinkElement*>(root->FindById("docsLink"));
        CHECK(test, docs != nullptr);
        if (docs != nullptr) {
            CHECK(test, docs->Href() == "https://example.com/docs");
            CHECK(test, !docs->HasTo());
            CHECK(test, docs->Label() == "Docs");
            CHECK(test, docs->WantsHandCursor());
        }
        LinkElement* settings = dynamic_cast<LinkElement*>(root->FindById("settingsLink"));
        CHECK(test, settings != nullptr);
        if (settings != nullptr) {
            CHECK(test, settings->To() == "Settings");
            CHECK(test, !settings->HasHref());
        }
        // The screen the link names is a screen of the same document, so it is a window the runtime
        // knows about rather than a name that leads nowhere.
        CHECK(test, root->FindById("Settings") != nullptr);
    }

    // Activating a link hands its names to the program's handler, and a link with no handler is
    // still a link: a document can be shown before the program behind it exists.
    std::string activatedHref;
    std::string activatedTo;
    int activations = 0;
    std::unique_ptr<Element> wired = LayoutFile(Fixture("Screen.xml"), 400.0f, 320.0f,
                                                [&](const std::string& href, const std::string& to, const std::string& target) {
                                                    ++activations;
                                                    activatedHref = href;
                                                    activatedTo = to;
                                                    (void)target;
                                                });
    CHECK(test, wired != nullptr);
    if (wired != nullptr) {
        LinkElement* docs = dynamic_cast<LinkElement*>(wired->FindById("docsLink"));
        CHECK(test, docs != nullptr);
        if (docs != nullptr) {
            docs->Activate();
            CHECK(test, activations == 1);
            CHECK(test, activatedHref == "https://example.com/docs");
            CHECK(test, activatedTo.empty());
        }
        LinkElement* settings = dynamic_cast<LinkElement*>(wired->FindById("settingsLink"));
        CHECK(test, settings != nullptr);
        if (settings != nullptr) {
            settings->Activate();
            CHECK(test, activations == 2);
            CHECK(test, activatedTo == "Settings");
        }
    }

    // A document that imports a document that imports it back has no first file to read, so the
    // cycle is refused rather than followed until the stack runs out.
    const CompileResult cycle = CompileUiFile(Fixture("CycleOne.xml"));
    CHECK(test, !cycle.Ok());
    CHECK(test, HasDiagnostic(cycle.diagnostics, DIAGNOSTIC_IMPORT_CYCLE));

    // A name that leads nowhere is reported where it was written, which is the file that named it
    // rather than the file it would have named.
    const CompileResult missing = CompileUiFile(Fixture("MissingImport.xml"));
    CHECK(test, !missing.Ok());
    CHECK(test, HasDiagnostic(missing.diagnostics, DIAGNOSTIC_CANNOT_READ_FILE));
    if (!missing.diagnostics.empty()) {
        CHECK(test, missing.diagnostics.front().location.path.find("MissingImport.xml") != std::string::npos);
    }

    // A library named twice is one library rather than two, so the definitions it holds are offered
    // once rather than reported as written twice. A name that repeats is a guard, not a mistake.
    const CompileResult twice = CompileUiFile(Fixture("Twice.xml"));
    CHECK(test, twice.Ok());
    CHECK(test, twice.sources.size() == 2);

    // A link is a link to somewhere: one that names nowhere, and one that names a screen the document
    // does not hold, are both refused where they were written.
    const CompileResult nowhere = CompileUi(Source("<window id=\"w\" width=\"100\" height=\"100\"><link text=\"x\"/></window>"));
    CHECK(test, !nowhere.Ok());
    CHECK(test, HasDiagnostic(nowhere.diagnostics, DIAGNOSTIC_LINK_TARGET));
    const CompileResult unknown = CompileUi(Source("<window id=\"w\" width=\"100\" height=\"100\"><link to=\"Nope\" text=\"x\"/></window>"));
    CHECK(test, !unknown.Ok());
    CHECK(test, HasDiagnostic(unknown.diagnostics, DIAGNOSTIC_LINK_UNKNOWN));
    const CompileResult same = CompileUi(Source("<window id=\"w\" width=\"100\" height=\"100\"><link to=\"w\" text=\"x\"/></window>"));
    CHECK(test, same.Ok());

    // A library that holds something a definition cannot be is reported rather than quietly kept.
    const CompileResult loose = CompileUi(Source("<library><frame name=\"NotADefinition\"/></library>"));
    CHECK(test, !loose.Ok());
    CHECK(test, HasDiagnostic(loose.diagnostics, DIAGNOSTIC_IMPORT_IGNORED));

    // A definition is a definition wherever a document keeps it. A screen holds its components inside
    // the boxes it lays out, so borrowing from a screen reaches into them, while what the screen's
    // content is stays behind: what is taken is the name, not the screen.
    const CompileResult borrowed = CompileUiFile(Fixture("UsesScreenParts.xml"));
    CHECK(test, borrowed.Ok());
    if (!borrowed.Ok()) {
        PrintDiagnostics(borrowed.sources, borrowed.diagnostics, std::cerr);
    }
    std::unique_ptr<Element> nested = LayoutFile(Fixture("UsesScreenParts.xml"), 320.0f, 200.0f);
    CHECK(test, nested != nullptr);
    if (nested != nullptr) {
        CHECK(test, nested->Children().size() == 1);
        InstanceElement* place = dynamic_cast<InstanceElement*>(nested->FindById("inner"));
        CHECK(test, place != nullptr && place->Resolved());
        if (place != nullptr) {
            const TextElement* label = dynamic_cast<TextElement*>(place->FindById("innerLabel"));
            CHECK(test, label != nullptr && label->Text() == "Inner");
        }
    }

    // What a link means, decided without a window: a page is opened, a screen is shown where the link
    // stands, and a screen asked for in a window of its own is brought forward. A link that carries
    // both opens the page, because a page is the thing that cannot be shown here.
    CHECK(test, RouteLink("https://example.com", "", "").action == LinkAction::OPEN);
    CHECK(test, RouteLink("https://example.com", "Settings", "").action == LinkAction::OPEN);
    CHECK(test, RouteLink("", "Settings", "").action == LinkAction::SHOW);
    CHECK(test, RouteLink("", "Settings", "self").action == LinkAction::SHOW);
    CHECK(test, RouteLink("", "Settings", "window").action == LinkAction::RAISE);
    CHECK(test, RouteLink("", "Settings", "blank").action == LinkAction::RAISE);
    CHECK(test, RouteLink("", "", "").action == LinkAction::NONE);
    CHECK(test, RouteLink("", "Settings", "").name == "Settings");
    CHECK(test, RouteLink("  ", "  ", "  ").action == LinkAction::NONE);
    CHECK(test, RouteLink(" https://example.com/docs ", "", "").name == "https://example.com/docs");

    // A node that names a handler is wired to it, and the same event serves the mouse and the
    // keyboard: what the handler does is the program's, and which handler it is is the document's.
    int calls = 0;
    const CompileResult clickable =
        CompileUi(Source("<window id=\"w\" width=\"100\" height=\"100\"><frame id=\"b\" onClick=\"go\" width=\"40\" height=\"20\"/></window>"));
    CHECK(test, clickable.Ok());
    std::unique_ptr<Element> wiredRoot = Instantiate(*clickable.root, {{"go", [&calls]() { ++calls; }}});
    CHECK(test, wiredRoot != nullptr);
    if (wiredRoot != nullptr) {
        Element* button = wiredRoot->FindById("b");
        CHECK(test, button != nullptr);
        if (button != nullptr) {
            button->Activate();
            CHECK(test, calls == 1);
        }
    }

    Finish(test, "projects", before);
}

int RunUnitTests() {
    TestContext test;
    TestParserAndSerializer(test);
    TestTypeMismatch(test);
    TestSuggestions(test);
    TestFrameProperties(test);
    TestFrameLayout(test);
    TestPrimitives(test);
    TestComponents(test);
    TestProjects(test);
    TestGroup(test);
    TestRectangle(test);
    TestEllipse(test);
    TestPolygon(test);
    TestStar(test);
    TestVector(test);
    TestSvg(test);
    TestRichText(test);
    TestImage(test);
    TestVideo(test);
    TestVideoContainers(test);
    TestLine(test);
    TestArrow(test);
    TestBoolean(test);
    TestBoards(test);
    TestDesignDocumentEditsXml(test);
    TestDesignDocumentGroups(test);
    TestCompilerSurface(test);
    TestFoundations(test);
    TestTypography(test);
    TestTextSizing(test);
    TestWindow(test);
    TestBoard(test);
    TestRemainingSurface(test);
    TestIrDump(test);
    if (test.failed == 0) {
        std::cout << "✓ All tests passed\n";
        return 0;
    }
    std::cout << test.failed << " failed\n";
    return 1;
}

}  // namespace Runtime

int main() {
    // The reader the platform has is offered to the runtime here, so the tests exercise the same
    // route a program takes: the readers written into the runtime first, and the machine's own
    // reader only for a picture none of them can read.
    Runtime::RegisterWicImageProvider();
    return Runtime::RunUnitTests();
}
