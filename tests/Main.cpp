#include "compiler/semantic/UiCompiler.h"
#include "components/frame/Frame.h"
#include "designer/model/DesignDocument.h"
#include "rendering/text/TextMeasurer.h"
#include "tests/TestHarness.h"
#include "ui/element/Element.h"
#include "ui/factory/UiFactory.h"
#include "xml/parser/XmlParser.h"
#include "xml/serializer/XmlSerializer.h"

#include <cmath>
#include <iostream>
#include <string>

namespace Runtime {
namespace {

class FakeMeasurer : public TextMeasurer {
public:
    Size MeasureText(std::string_view, float fontSize, float) override {
        return Size{fontSize, fontSize};
    }
};

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
    Finish(test, "frame layout", before);
}

void TestDesignDocumentEditsXml(TestContext& test) {
    const int before = test.failed;
    DesignDocument document;
    CHECK(test, document.LoadText("<window width=\"800\" height=\"600\"><frame name=\"Hello\"/></window>", "memory.xml"));
    CHECK(test, document.TextSynced());
    std::vector<int> path{0};
    CHECK(test, document.SetAttribute(path, "name", "Designed"));
    CHECK(test, document.Text().find("Designed") != std::string::npos);
    std::vector<int> inserted;
    CHECK(test, document.InsertChild({}, "frame", inserted));
    CHECK(test, document.NodeAt(inserted) != nullptr);
    CHECK(test, document.NodeAt(inserted)->name == "frame");
    CHECK(test, document.DeleteNode(inserted));
    Finish(test, "design document", before);
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

}  // namespace

int RunUnitTests() {
    TestContext test;
    TestParserAndSerializer(test);
    TestTypeMismatch(test);
    TestSuggestions(test);
    TestFrameProperties(test);
    TestFrameLayout(test);
    TestDesignDocumentEditsXml(test);
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
