#include "compiler/semantic/UiCompiler.h"
#include "components/basic/Button.h"
#include "components/basic/TextBlock.h"
#include "designer/model/DesignDocument.h"
#include "rendering/text/TextMeasurer.h"
#include "tests/TestHarness.h"
#include "ui/element/Element.h"
#include "ui/factory/UiFactory.h"
#include "xml/parser/XmlParser.h"
#include "xml/serializer/XmlSerializer.h"

#include <cmath>
#include <string>

namespace Runtime {
namespace {

class FakeMeasurer : public TextMeasurer {
public:
    Size MeasureText(std::string_view text, float fontSize, float) override {
        return Size{static_cast<float>(text.size()) * fontSize * 0.5f, fontSize};
    }
};

SourceFile Source(std::string text) {
    return SourceFile{"memory.xml", std::move(text)};
}

void TestParserAndSerializer(TestContext& test) {
    const ParseResult parsed = ParseXml(Source("<window width=\"800\"><button text=\"Hello\"/></window>"));
    CHECK(test, parsed.root.has_value());
    CHECK(test, parsed.diagnostics.empty());
    CHECK(test, parsed.root->name == "window");
    CHECK(test, parsed.root->children.size() == 1);
    CHECK(test, parsed.root->children[0].name == "button");
    const std::string serialized = SerializeXml(*parsed.root);
    CHECK(test, serialized.find("button") != std::string::npos);
    CHECK(test, serialized.find("Hello") != std::string::npos);
    const ParseResult again = ParseXml(Source(serialized));
    CHECK(test, again.root.has_value());
    CHECK(test, again.root->children.size() == 1);
}

void TestTypeMismatch(TestContext& test) {
    const CompileResult compiled = CompileUi(Source("<window><button width=\"hello\"/></window>"));
    CHECK(test, !compiled.Ok());
    CHECK(test, !compiled.diagnostics.empty());
    CHECK(test, compiled.diagnostics.front().code == DIAGNOSTIC_TYPE_MISMATCH);
    CHECK(test, compiled.diagnostics.front().message.find("Length") != std::string::npos);
}

void TestSuggestions(TestContext& test) {
    const CompileResult component = CompileUi(Source("<window><buton/></window>"));
    CHECK(test, !component.Ok());
    CHECK(test, component.diagnostics.front().code == DIAGNOSTIC_UNKNOWN_COMPONENT);
    CHECK(test, component.diagnostics.front().message.find("button") != std::string::npos);

    const CompileResult property = CompileUi(Source("<window><text colour=\"red\"/></window>"));
    CHECK(test, !property.Ok());
    CHECK(test, property.diagnostics.front().code == DIAGNOSTIC_UNKNOWN_PROPERTY);
    CHECK(test, property.diagnostics.front().message.find("color") != std::string::npos);
}

void TestTypedLength(TestContext& test) {
    const CompileResult compiled = CompileUi(Source("<window width=\"120px\" height=\"80\"/>"));
    CHECK(test, compiled.Ok());
    const UiValue* width = FindProperty(*compiled.root, "width");
    CHECK(test, width != nullptr);
    CHECK(test, width->kind == TypeKind::LENGTH);
    CHECK(test, width->length.unit == LengthUnit::PIXELS);
    CHECK(test, std::fabs(width->length.value - 120.0f) < 0.01f);
}

void TestDuplicateIdAndEvent(TestContext& test) {
    const CompileResult duplicate = CompileUi(Source("<window><text id=\"name\"/><text id=\"name\"/></window>"));
    CHECK(test, !duplicate.Ok());
    CHECK(test, duplicate.diagnostics.front().code == DIAGNOSTIC_DUPLICATE_ID);

    const CompileResult event = CompileUi(Source("<window><button onClick=\"1bad\"/></window>"));
    CHECK(test, !event.Ok());
    CHECK(test, event.diagnostics.front().code == DIAGNOSTIC_EVENT_SIGNATURE);
}

void TestLayoutCentersButton(TestContext& test) {
    const CompileResult compiled =
        CompileUi(Source("<window width=\"800\" height=\"600\"><column alignment=\"center\"><button text=\"Hi\" "
                         "width=\"100px\" height=\"40px\"/></column></window>"));
    CHECK(test, compiled.Ok());
    std::unique_ptr<Element> root = Instantiate(*compiled.root, {});
    CHECK(test, root != nullptr);
    FakeMeasurer measurer;
    root->Measure(measurer, 800.0f, 600.0f, true);
    root->Arrange(Rect{0.0f, 0.0f, 800.0f, 600.0f});
    Button* button = root->FindFirstButton();
    CHECK(test, button != nullptr);
    CHECK(test, std::fabs(button->Bounds().x - 350.0f) < 0.01f);
    CHECK(test, std::fabs(button->Bounds().width - 100.0f) < 0.01f);
    CHECK(test, std::fabs(button->Bounds().height - 40.0f) < 0.01f);
}

void TestDesignDocumentEditsXml(TestContext& test) {
    DesignDocument document;
    CHECK(test, document.LoadText("<window width=\"800\" height=\"600\"><column><button text=\"Hello\"/></column></window>", "memory.xml"));
    CHECK(test, document.TextSynced());
    std::vector<int> buttonPath{0, 0};
    CHECK(test, document.SetAttribute(buttonPath, "text", "Designed"));
    CHECK(test, document.Text().find("Designed") != std::string::npos);
    CHECK(test, document.Attribute(buttonPath, "text") == "Designed");
    std::vector<int> inserted;
    CHECK(test, document.InsertChild({}, "text", inserted));
    CHECK(test, document.NodeAt(inserted) != nullptr);
    CHECK(test, document.NodeAt(inserted)->name == "text");
    CHECK(test, document.DeleteNode(inserted));
    CHECK(test, document.NodeAt(inserted) == nullptr);
    const std::string valid = document.Text();
    CHECK(test, !document.ReplaceText("<window><button width=\"hello\"></window>"));
    CHECK(test, !document.TextSynced());
    CHECK(test, document.Root() != nullptr);
    CHECK(test, document.Root()->name == "window");
    CHECK(test, !document.SetAttribute(buttonPath, "text", "Blocked"));
    CHECK(test, document.ReplaceText(valid));
    CHECK(test, document.TextSynced());
}

void TestFlexGridAndZIndex(TestContext& test) {
    const CompileResult rowCompiled = CompileUi(Source(
        "<window width=\"400\" height=\"200\"><row spacing=\"10\"><button width=\"40px\" height=\"40px\"/><button width=\"40px\" "
        "height=\"40px\"/></row></window>"));
    CHECK(test, rowCompiled.Ok());
    std::unique_ptr<Element> rowRoot = Instantiate(*rowCompiled.root, {});
    FakeMeasurer measurer;
    rowRoot->Measure(measurer, 400.0f, 200.0f, true);
    rowRoot->Arrange(Rect{0.0f, 0.0f, 400.0f, 200.0f});
    CHECK(test, rowRoot->Children().size() == 1);
    CHECK(test, rowRoot->Children()[0]->Children().size() == 2);
    CHECK(test, std::fabs(rowRoot->Children()[0]->Children()[1]->Bounds().x - 50.0f) < 0.01f);

    const CompileResult gridCompiled = CompileUi(Source(
        "<window width=\"300\" height=\"200\"><grid columns=\"3\"><button width=\"40px\" height=\"40px\"/><button width=\"40px\" "
        "height=\"40px\"/><button width=\"40px\" height=\"40px\"/></grid></window>"));
    CHECK(test, gridCompiled.Ok());
    std::unique_ptr<Element> gridRoot = Instantiate(*gridCompiled.root, {});
    gridRoot->Measure(measurer, 300.0f, 200.0f, true);
    gridRoot->Arrange(Rect{0.0f, 0.0f, 300.0f, 200.0f});
    const auto& cells = gridRoot->Children()[0]->Children();
    CHECK(test, cells.size() == 3);
    CHECK(test, std::fabs(cells[0]->Bounds().x - 0.0f) < 0.01f);
    CHECK(test, std::fabs(cells[1]->Bounds().x - 100.0f) < 0.01f);
    CHECK(test, std::fabs(cells[2]->Bounds().x - 200.0f) < 0.01f);

    const CompileResult named = CompileUi(Source("<window><text zIndex=\"tooltip\"/></window>"));
    CHECK(test, named.Ok());
    std::unique_ptr<Element> namedRoot = Instantiate(*named.root, {});
    CHECK(test, namedRoot->Children()[0]->ZIndex() == 1000);

    const CompileResult numeric = CompileUi(Source("<window><stack><text zIndex=\"4\"/></stack></window>"));
    CHECK(test, numeric.Ok());
    std::unique_ptr<Element> numericRoot = Instantiate(*numeric.root, {});
    CHECK(test, numericRoot->Children()[0]->Children()[0]->ZIndex() == 4);
}

void TestBoxModelAndControls(TestContext& test) {
    const CompileResult compiled = CompileUi(Source(
        "<window width=\"400\" height=\"300\"><column>"
        "<button text=\"Hi\" minWidth=\"80\" margin=\"10\"/>"
        "<text text=\"Half\" width=\"50%\"/>"
        "<scroll height=\"100\"><button width=\"40px\" height=\"80px\"/><button width=\"40px\" height=\"80px\"/></scroll>"
        "</column></window>"));
    CHECK(test, compiled.Ok());
    std::unique_ptr<Element> root = Instantiate(*compiled.root, {});
    FakeMeasurer measurer;
    root->Measure(measurer, 400.0f, 300.0f, true);
    root->Arrange(Rect{0.0f, 0.0f, 400.0f, 300.0f});
    Element* button = root->Children()[0]->Children()[0].get();
    Element* text = root->Children()[0]->Children()[1].get();
    Element* scroll = root->Children()[0]->Children()[2].get();
    CHECK(test, std::fabs(button->Measured().width - 100.0f) < 0.01f);
    CHECK(test, std::fabs(button->Bounds().width - 80.0f) < 0.01f);
    CHECK(test, std::fabs(text->Bounds().width - 200.0f) < 0.01f);
    CHECK(test, scroll->OnWheel(-120));
    root->Arrange(Rect{0.0f, 0.0f, 400.0f, 300.0f});
    CHECK(test, scroll->Children()[0]->Bounds().y < scroll->Bounds().y);

    const CompileResult edges = CompileUi(Source("<window><panel padding=\"16 8 4 2\"/></window>"));
    CHECK(test, edges.Ok());
    const CompileResult hidden = CompileUi(Source("<window><text visibility=\"hidden\" text=\"Gone\"/></window>"));
    CHECK(test, hidden.Ok());
    std::unique_ptr<Element> hiddenRoot = Instantiate(*hidden.root, {});
    hiddenRoot->Measure(measurer, 200.0f, 100.0f, true);
    CHECK(test, hiddenRoot->Children()[0]->Measured().width == 0.0f);
}

void TestStylesComponentsAndBindings(TestContext& test) {
    const CompileResult compiled = CompileUi(Source(
        "<window width=\"400\" height=\"300\">"
        "<style name=\"quiet\" color=\"$color.muted\" fontSize=\"$type.caption\"/>"
        "<state name=\"note\" value=\"Ready\"/>"
        "<component name=\"Hint\"><text style=\"quiet\" text=\"{label}\"/></component>"
        "<Hint label=\"Hello\"/>"
        "<text id=\"note\" text=\"{note}\"/>"
        "<button id=\"save\" text=\"Save\" onClick=\"set:note=Saved\"/>"
        "<row width=\"300\">"
        "<button width=\"40px\" height=\"20px\" grow=\"1\"/>"
        "<button width=\"40px\" height=\"20px\"/>"
        "</row>"
        "</window>"));
    CHECK(test, compiled.Ok());
    std::unique_ptr<Element> root = Instantiate(*compiled.root, {});
    FakeMeasurer measurer;
    root->SetViewport(400.0f, 300.0f);
    root->Measure(measurer, 400.0f, 300.0f, true);
    root->Arrange(Rect{0.0f, 0.0f, 400.0f, 300.0f});
    TextBlock* hint = root->Children()[0]->AsText();
    CHECK(test, hint != nullptr);
    CHECK(test, hint->Text() == "Hello");
    TextBlock* note = dynamic_cast<TextBlock*>(root->FindById("note"));
    CHECK(test, note != nullptr);
    CHECK(test, note->Text() == "Ready");
    Button* save = dynamic_cast<Button*>(root->FindById("save"));
    CHECK(test, save != nullptr);
    save->Activate();
    CHECK(test, note->Text() == "Saved");
    Element* row = root->Children()[3].get();
    CHECK(test, row->Children().size() == 2);
    CHECK(test, std::fabs(row->Children()[0]->Bounds().width - 260.0f) < 0.01f);
    CHECK(test, std::fabs(row->Children()[1]->Bounds().x - 260.0f) < 0.01f);
}

void TestScrollFillAndOverflow(TestContext& test) {
    const CompileResult compiled = CompileUi(Source(
        "<window width=\"400\" height=\"300\"><column width=\"fill\" height=\"fill\" spacing=\"0\">"
        "<spacer height=\"40\"/>"
        "<scroll height=\"fill\">"
        "<button width=\"40px\" height=\"80px\"/>"
        "<button width=\"40px\" height=\"80px\"/>"
        "<button width=\"40px\" height=\"80px\"/>"
        "<button width=\"40px\" height=\"80px\"/>"
        "</scroll></column></window>"));
    CHECK(test, compiled.Ok());
    std::unique_ptr<Element> root = Instantiate(*compiled.root, {});
    FakeMeasurer measurer;
    root->SetViewport(400.0f, 300.0f);
    root->Measure(measurer, 400.0f, 300.0f, true);
    root->Arrange(Rect{0.0f, 0.0f, 400.0f, 300.0f});
    Element* scroll = root->Children()[0]->Children()[1].get();
    CHECK(test, std::fabs(scroll->Bounds().height - 260.0f) < 0.01f);
    CHECK(test, scroll->OnWheel(-120));
    root->Arrange(Rect{0.0f, 0.0f, 400.0f, 300.0f});
    CHECK(test, scroll->Children()[0]->Bounds().y < scroll->Bounds().y);
}

void TestIrDump(TestContext& test) {
    const CompileResult compiled = CompileUi(Source("<window><button id=\"save\" text=\"Save\" onClick=\"save\"/></window>"));
    CHECK(test, compiled.Ok());
    const std::string dump = FormatIr(*compiled.root);
    CHECK(test, dump.find("button") != std::string::npos);
    CHECK(test, dump.find("save") != std::string::npos);
}

}  // namespace

int RunUnitTests() {
    TestContext test;
    TestParserAndSerializer(test);
    TestTypeMismatch(test);
    TestSuggestions(test);
    TestTypedLength(test);
    TestDuplicateIdAndEvent(test);
    TestLayoutCentersButton(test);
    TestFlexGridAndZIndex(test);
    TestBoxModelAndControls(test);
    TestStylesComponentsAndBindings(test);
    TestScrollFillAndOverflow(test);
    TestDesignDocumentEditsXml(test);
    TestIrDump(test);
    if (test.failed == 0) {
        std::cout << "All tests passed\n";
        return 0;
    }
    std::cout << test.failed << " failed\n";
    return 1;
}

}  // namespace Runtime

int main() {
    return Runtime::RunUnitTests();
}
