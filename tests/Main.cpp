#include "compiler/ir/UiIr.h"
#include "compiler/semantic/ComponentCatalog.h"
#include "compiler/semantic/UiCompiler.h"
#include "compiler/types/ValueParser.h"
#include "components/arrow/ArrowElement.h"
#include "components/boolean/BooleanElement.h"
#include "components/ellipse/EllipseElement.h"
#include "components/frame/Frame.h"
#include "components/group/GroupElement.h"
#include "components/line/LineElement.h"
#include "components/layout/BoardElement.h"
#include "components/polygon/PolygonElement.h"
#include "components/layout/WindowElement.h"
#include "components/primitive/PrimitiveElement.h"
#include "components/rectangle/RectangleElement.h"
#include "components/star/StarElement.h"
#include "components/svg/SvgElement.h"
#include "components/text/TextElement.h"
#include "components/vector/VectorElement.h"
#include "designer/model/DesignDocument.h"
#include "designer/tools/EditorTool.h"
#include "engine/motion/Motion.h"
#include "rendering/image/ImageDecoder.h"
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

class FakeMeasurer : public TextMeasurer {
public:
    Size MeasureText(std::string_view, const TextStyle& style, float) override {
        return Size{style.fontSize, style.fontSize};
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

    TextStyle style;
    std::string measuredText;
    float measuredWidth = 0.0f;
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
};

// Records what a paint pass handed to each draw, so a group can be checked for passing its
// own opacity, clip, rotation, and translation down to every child it holds.
class RecordingRenderer : public Renderer {
public:
    Size MeasureText(std::string_view, const TextStyle& style, float) override { return Size{style.fontSize, style.fontSize}; }
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
    void DrawCachedImage(std::string_view, const Rect& rect, const unsigned char*, int, int) override { Record(rect); }
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
    void PushClip(const Rect& rect) override { clips.push_back(rect); }
    void PopClip() override {
        if (!clips.empty()) {
            clips.pop_back();
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
        record.rotationDepth = static_cast<int>(rotations.size());
        record.rotation = rotations.empty() ? 0.0f : rotations.back();
        records.push_back(record);
    }

    std::vector<float> opacityStack;
    std::vector<Rect> clips;
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
    const EditorTool tools[] = {EditorTool::SELECT, EditorTool::FRAME,     EditorTool::RECTANGLE, EditorTool::ELLIPSE,
                                EditorTool::POLYGON, EditorTool::STAR,     EditorTool::TEXT,      EditorTool::IMAGE,
                                EditorTool::SVG,     EditorTool::LINE,     EditorTool::ARROW,     EditorTool::BOOLEAN,
                                EditorTool::HAND,    EditorTool::ZOOM};
    for (const EditorTool tool : tools) {
        CHECK(test, std::string(EditorToolName(tool)).size() > 0);
        if (const char* component = EditorToolComponent(tool)) {
            CHECK(test, FindComponent(component) != nullptr);
            CHECK(test, ComponentAcceptsProperty(component, "width"));
        }
    }
    Finish(test, "compiler surface", before);
}

bool EncodeBitmap(const GUID& format, std::vector<unsigned char>& bytes) {
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
    frame->SetSize(1, 1);
    WICPixelFormatGUID pixel = GUID_WICPixelFormat32bppRGBA;
    frame->SetPixelFormat(&pixel);
    const unsigned char pixelBytes[4] = {255, 0, 0, 255};
    if (FAILED(frame->WritePixels(1, 4, 4, const_cast<unsigned char*>(pixelBytes))) || FAILED(frame->Commit()) || FAILED(encoder->Commit())) {
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
    CHECK(test, DecodeImageBytes(avif, static_cast<int>(sizeof(avif)), decoded) && decoded.format == "avif" && decoded.frames.size() == 1);
    if (!decoded.frames.empty()) {
        CHECK(test, decoded.frames[0].width == 8 && decoded.frames[0].bgra.size() >= 4 && decoded.frames[0].bgra[2] > 200 &&
                        decoded.frames[0].bgra[0] < 40);
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

int RunUnitTests() {
    TestContext test;
    TestParserAndSerializer(test);
    TestTypeMismatch(test);
    TestSuggestions(test);
    TestFrameProperties(test);
    TestFrameLayout(test);
    TestPrimitives(test);
    TestGroup(test);
    TestRectangle(test);
    TestEllipse(test);
    TestPolygon(test);
    TestStar(test);
    TestVector(test);
    TestSvg(test);
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
    return Runtime::RunUnitTests();
}
