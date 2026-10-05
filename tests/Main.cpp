#include "compiler/semantic/UiCompiler.h"
#include "compiler/types/ValueParser.h"
#include "components/frame/Frame.h"
#include "components/primitive/PrimitiveElement.h"
#include "designer/model/DesignDocument.h"
#include "engine/motion/Motion.h"
#include "rendering/image/ImageDecoder.h"
#include "rendering/path/PathCurve.h"
#include "rendering/text/TextLayout.h"
#include "rendering/text/TextMeasurer.h"
#include "tests/TestHarness.h"
#include "t/button/Button.h"
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
        auto* name = dynamic_cast<ShapeElement*>(root->FindById("name"));
        auto* once = dynamic_cast<ShapeElement*>(root->FindById("once"));
        auto* age = dynamic_cast<ShapeElement*>(root->FindById("age"));
        auto* full = dynamic_cast<ShapeElement*>(root->FindById("full"));
        auto* edit = dynamic_cast<ShapeElement*>(root->FindById("edit"));
        auto* live = dynamic_cast<ShapeElement*>(root->FindById("live"));
        auto* later = dynamic_cast<ShapeElement*>(root->FindById("later"));
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

void TestButton(TestContext& test) {
    const int before = test.failed;
    const std::filesystem::path path = std::filesystem::path(RUNTIME_SOURCE_DIR) / "tests" / "Button.test.xml";
    const CompileResult compiled = CompileUiFile(path);
    CHECK(test, compiled.Ok());
    if (!compiled.root.has_value()) {
        Finish(test, "button", before);
        return;
    }
    int clicks = 0;
    std::unique_ptr<Element> root = Instantiate(*compiled.root, {{"handleClick", [&] { ++clicks; }}});
    CHECK(test, root != nullptr);
    if (root == nullptr) {
        Finish(test, "button", before);
        return;
    }
    FakeMeasurer measurer;
    root->SetViewport(400.0f, 1400.0f);
    root->Measure(measurer, 400.0f, 1400.0f, true);
    root->Arrange(Rect{0.0f, 0.0f, 400.0f, 1400.0f});

    const char* largeIds[] = {"primaryLarge", "secondaryLarge", "tertiaryLarge", "destructiveLarge", "destructiveSecondaryLarge", "linkLarge"};
    const char* mediumIds[] = {"primaryMedium", "secondaryMedium", "tertiaryMedium", "destructiveMedium", "destructiveSecondaryMedium", "linkMedium"};
    const char* smallIds[] = {"primarySmall", "secondarySmall", "tertiarySmall", "destructiveSmall", "destructiveSecondarySmall", "linkSmall"};
    for (const char* id : largeIds) {
        Element* button = root->FindById(id);
        CHECK(test, button != nullptr && std::fabs(button->Measured().height - 50.0f) < 0.5f);
    }
    for (const char* id : mediumIds) {
        Element* button = root->FindById(id);
        CHECK(test, button != nullptr && std::fabs(button->Measured().height - 44.0f) < 0.5f);
    }
    for (const char* id : smallIds) {
        Element* button = root->FindById(id);
        CHECK(test, button != nullptr && std::fabs(button->Measured().height - 32.0f) < 0.5f);
    }

    auto* primary = dynamic_cast<Button*>(root->FindById("primaryMedium"));
    CHECK(test, primary != nullptr && primary->Variant() == ButtonVariant::PRIMARY && primary->Label() == "Primary");
    if (primary != nullptr) {
        auto* face = dynamic_cast<Frame*>(primary->FindById("root"));
        CHECK(test, face != nullptr && NearColor(face->BackgroundColor(), *ParseColor("$color.primary")));
        const Rect box = primary->Bounds();
        PointerInput input;
        input.OnMove(*root, Point{box.x + 4.0f, box.y + 4.0f});
        CHECK(test, face != nullptr && NearColor(face->BackgroundColor(), *ParseColor("$color.primaryHover")));
        input.OnDown(*root, Point{box.x + 4.0f, box.y + 4.0f});
        CHECK(test, face != nullptr && NearColor(face->BackgroundColor(), *ParseColor("$color.primaryActive")));
        input.OnUp(*root, Point{box.x + 4.0f, box.y + 4.0f});
    }

    auto* secondary = dynamic_cast<Button*>(root->FindById("secondaryMedium"));
    if (secondary != nullptr) {
        auto* face = dynamic_cast<Frame*>(secondary->FindById("root"));
        CHECK(test, face != nullptr && face->BackgroundColor().a < 0.05f);
    }

    Element* live = root->FindById("live");
    CHECK(test, live != nullptr);
    if (live != nullptr) {
        const Rect box = live->Bounds();
        PointerInput input;
        input.OnDown(*root, Point{box.x + 4.0f, box.y + 4.0f});
        input.OnUp(*root, Point{box.x + 4.0f, box.y + 4.0f});
        CHECK(test, clicks == 1);
    }
    Element* disabled = root->FindById("disabled");
    Element* busy = root->FindById("busy");
    if (disabled != nullptr) {
        const Rect box = disabled->Bounds();
        PointerInput input;
        input.OnDown(*root, Point{box.x + 4.0f, box.y + 4.0f});
        input.OnUp(*root, Point{box.x + 4.0f, box.y + 4.0f});
    }
    if (busy != nullptr) {
        const Rect box = busy->Bounds();
        PointerInput input;
        input.OnDown(*root, Point{box.x + 4.0f, box.y + 4.0f});
        input.OnUp(*root, Point{box.x + 4.0f, box.y + 4.0f});
    }
    CHECK(test, clicks == 1);

    Element* wide = root->FindById("wide");
    CHECK(test, wide != nullptr && std::fabs(wide->Measured().width - 400.0f) < 1.0f);
    Element* icon = root->FindById("iconOnly");
    CHECK(test, icon != nullptr && std::fabs(icon->Measured().width - 32.0f) < 0.5f && std::fabs(icon->Measured().height - 32.0f) < 0.5f);
    DesignDocument componentFile;
    const std::filesystem::path componentPath = std::filesystem::path(RUNTIME_SOURCE_DIR) / "t" / "button" / "Button.xml";
    CHECK(test, componentFile.LoadFile(componentPath));
    CHECK(test, componentFile.Text().find("<component") != std::string::npos);
    const CompileResult previewCompiled = CompileUi(SourceFile{componentPath.string(), componentFile.PreviewText()});
    CHECK(test, previewCompiled.Ok());
    if (previewCompiled.root.has_value()) {
        std::unique_ptr<Element> preview = Instantiate(*previewCompiled.root, {});
        CHECK(test, preview != nullptr);
        if (preview != nullptr) {
            FakeMeasurer previewMeasurer;
            preview->SetViewport(800.0f, 600.0f);
            preview->Measure(previewMeasurer, 800.0f, 600.0f, true);
            preview->Arrange(Rect{0.0f, 0.0f, 800.0f, 600.0f});
            Element* face = preview->FindById("root");
            CHECK(test, face != nullptr && std::fabs(face->Measured().height - 44.0f) < 0.5f);
            auto* frame = dynamic_cast<Frame*>(face);
            CHECK(test, frame != nullptr && NearColor(frame->BackgroundColor(), *ParseColor("$color.primary")));
        }
    }
    Element* spinner = busy == nullptr ? nullptr : busy->FindById("spinner");
    CHECK(test, spinner != nullptr && spinner->Visible());
    Element* busyLabel = busy == nullptr ? nullptr : busy->FindById("label");
    CHECK(test, busyLabel != nullptr && !busyLabel->Visible());
    Finish(test, "button", before);
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
    TestPrimitives(test);
    TestBoards(test);
    TestDesignDocumentEditsXml(test);
    TestCompilerSurface(test);
    TestRemainingSurface(test);
    TestButton(test);
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
