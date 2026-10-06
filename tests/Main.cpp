#include "compiler/ir/UiIr.h"
#include "compiler/semantic/UiCompiler.h"
#include "compiler/types/ValueParser.h"
#include "components/frame/Frame.h"
#include "components/group/GroupElement.h"
#include "components/layout/BoardElement.h"
#include "components/layout/WindowElement.h"
#include "components/primitive/PrimitiveElement.h"
#include "components/text/TextElement.h"
#include "designer/model/DesignDocument.h"
#include "engine/motion/Motion.h"
#include "rendering/image/ImageDecoder.h"
#include "rendering/renderer/Renderer.h"
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
#include <iostream>
#include <string>
#include <vector>

namespace Runtime {
namespace {

class FakeMeasurer : public TextMeasurer {
public:
    Size MeasureText(std::string_view, float fontSize, float) override {
        return Size{fontSize, fontSize};
    }
};

struct PaintRecord {
    Rect rect;
    float opacity = 1.0f;
    int clipDepth = 0;
    int rotationDepth = 0;
    float rotation = 0.0f;
};

// Records what a paint pass handed to each draw, so a group can be checked for passing its
// own opacity, clip, rotation, and translation down to every child it holds.
class RecordingRenderer : public Renderer {
public:
    Size MeasureText(std::string_view, float fontSize, float) override { return Size{fontSize, fontSize}; }
    void SetDpi(float) override {}
    void SetTransform(float, float, float) override {}
    void BeginFrame(Color) override {}
    bool EndFrame() override { return true; }
    void FillRectangle(const Rect& rect, const Color&, float) override { Record(rect); }
    void StrokeRectangle(const Rect& rect, const Color&, float, float) override { Record(rect); }
    void DrawString(std::string_view, const Rect& rect, const TextStyle&) override { Record(rect); }
    void DrawCachedImage(std::string_view, const Rect& rect, const unsigned char*, int, int) override { Record(rect); }
    void DrawVector(const VectorPaint& paint) override { Record(paint.bounds); }
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

    // Shift+wheel on a vertical-only scroller still scrolls vertically instead of doing nothing.
    std::unique_ptr<Element> shifted = Layout(
        "<window width=\"100\" height=\"50\"><frame id=\"shifted\" overflow=\"scroll\" layout=\"vertical\" width=\"100\" height=\"50\">"
        "<frame id=\"shiftedFirst\" width=\"40\" height=\"200\"/></frame></window>",
        100.0f, 50.0f);
    CHECK(test, shifted != nullptr);
    CHECK(test, shifted->FindById("shifted")->OnWheel(-120, true));
    shifted->Arrange(Rect{0.0f, 0.0f, 100.0f, 50.0f});
    CHECK(test, shifted->FindById("shiftedFirst")->Bounds().y < shifted->FindById("shifted")->Bounds().y);

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
    Finish(test, "design document", before);
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
    Finish(test, "typography", before);
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
    TestBoards(test);
    TestDesignDocumentEditsXml(test);
    TestCompilerSurface(test);
    TestFoundations(test);
    TestTypography(test);
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
