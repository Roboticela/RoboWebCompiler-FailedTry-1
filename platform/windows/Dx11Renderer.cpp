#include "platform/windows/Dx11Renderer.h"

#include "engine/core/Log.h"
#include "engine/theme/DevKitTheme.h"
#include "platform/windows/Win32Window.h"
#include "rendering/path/PathCurve.h"

#include <d2d1_1helper.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <sstream>

namespace Runtime {
namespace {

bool LogHr(HRESULT result, const char* action) {
    if (SUCCEEDED(result)) {
        return true;
    }
    std::ostringstream message;
    message << action << " failed (0x" << std::hex << std::uppercase << static_cast<unsigned int>(result) << ")";
    LogError(message.str());
    return false;
}

D2D1_COLOR_F ToD2d(Color color, float opacity) {
    return D2D1::ColorF(color.r, color.g, color.b, color.a * opacity);
}

DWRITE_TEXT_ALIGNMENT ToHorizontal(TextAlign align) {
    switch (align) {
    case TextAlign::CENTER:
        return DWRITE_TEXT_ALIGNMENT_CENTER;
    case TextAlign::END:
        return DWRITE_TEXT_ALIGNMENT_TRAILING;
    case TextAlign::JUSTIFY:
        return DWRITE_TEXT_ALIGNMENT_JUSTIFIED;
    case TextAlign::START:
        return DWRITE_TEXT_ALIGNMENT_LEADING;
    }
    return DWRITE_TEXT_ALIGNMENT_LEADING;
}

DWRITE_PARAGRAPH_ALIGNMENT ToVertical(TextAlign align) {
    switch (align) {
    case TextAlign::CENTER:
        return DWRITE_PARAGRAPH_ALIGNMENT_CENTER;
    case TextAlign::END:
        return DWRITE_PARAGRAPH_ALIGNMENT_FAR;
    case TextAlign::JUSTIFY:
        return DWRITE_PARAGRAPH_ALIGNMENT_NEAR;
    case TextAlign::START:
        return DWRITE_PARAGRAPH_ALIGNMENT_NEAR;
    }
    return DWRITE_PARAGRAPH_ALIGNMENT_NEAR;
}

}  // namespace

Dx11Renderer::~Dx11Renderer() {
    if (d2dContext) {
        d2dContext->SetTarget(nullptr);
    }
}

bool Dx11Renderer::CreateDevice() {
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
    auto attempt = [&](D3D_DRIVER_TYPE driver, UINT flags) {
        device.Reset();
        deviceContext.Reset();
        const HRESULT result = D3D11CreateDevice(nullptr, driver, nullptr, flags, levels, 2, D3D11_SDK_VERSION,
                                                 device.GetAddressOf(), nullptr, deviceContext.GetAddressOf());
        return SUCCEEDED(result);
    };
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#ifdef _DEBUG
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
    if (!attempt(D3D_DRIVER_TYPE_HARDWARE, flags)) {
        flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
        if (!attempt(D3D_DRIVER_TYPE_HARDWARE, flags) && !attempt(D3D_DRIVER_TYPE_WARP, flags)) {
            LogError("Direct3D 11 device creation failed.");
            return false;
        }
    }

    D2D1_FACTORY_OPTIONS options{};
    HRESULT result = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, options, d2dFactory.GetAddressOf());
    if (FAILED(result)) {
        return LogHr(result, "Direct2D factory creation");
    }
    Microsoft::WRL::ComPtr<IDXGIDevice> dxgiDevice;
    result = device.As(&dxgiDevice);
    if (!LogHr(result, "DXGI device query")) {
        return false;
    }
    result = d2dFactory->CreateDevice(dxgiDevice.Get(), d2dDevice.GetAddressOf());
    if (!LogHr(result, "Direct2D device creation")) {
        return false;
    }
    result = d2dDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, d2dContext.GetAddressOf());
    if (!LogHr(result, "Direct2D context creation")) {
        return false;
    }
    d2dContext->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    result = d2dContext->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f), brush.GetAddressOf());
    if (!LogHr(result, "Brush creation")) {
        return false;
    }
    result = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                 reinterpret_cast<IUnknown**>(writeFactory.GetAddressOf()));
    return LogHr(result, "DirectWrite factory creation");
}

bool Dx11Renderer::CreateSwapChain(HWND window, int width, int height) {
    Microsoft::WRL::ComPtr<IDXGIDevice> dxgiDevice;
    if (FAILED(device.As(&dxgiDevice))) {
        return false;
    }
    Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
    if (FAILED(dxgiDevice->GetAdapter(adapter.GetAddressOf()))) {
        return false;
    }
    Microsoft::WRL::ComPtr<IDXGIFactory2> factory;
    if (FAILED(adapter->GetParent(IID_PPV_ARGS(factory.GetAddressOf())))) {
        return false;
    }
    DXGI_SWAP_CHAIN_DESC1 description{};
    description.Width = static_cast<UINT>(width);
    description.Height = static_cast<UINT>(height);
    description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    description.SampleDesc.Count = 1;
    description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    description.BufferCount = 2;
    description.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    description.Scaling = DXGI_SCALING_STRETCH;
    description.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
    const HRESULT result =
        factory->CreateSwapChainForHwnd(device.Get(), window, &description, nullptr, nullptr, swapChain.GetAddressOf());
    if (!LogHr(result, "Swap chain creation")) {
        return false;
    }
    factory->MakeWindowAssociation(window, DXGI_MWA_NO_ALT_ENTER);
    return true;
}

bool Dx11Renderer::CreateTarget() {
    if (d2dContext) {
        d2dContext->SetTarget(nullptr);
    }
    targetBitmap.Reset();
    Microsoft::WRL::ComPtr<IDXGISurface> surface;
    HRESULT result = swapChain->GetBuffer(0, IID_PPV_ARGS(surface.GetAddressOf()));
    if (!LogHr(result, "Back buffer query")) {
        return false;
    }
    const D2D1_BITMAP_PROPERTIES1 properties = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
    result = d2dContext->CreateBitmapFromDxgiSurface(surface.Get(), &properties, targetBitmap.GetAddressOf());
    if (!LogHr(result, "Direct2D target creation")) {
        return false;
    }
    d2dContext->SetTarget(targetBitmap.Get());
    d2dContext->SetDpi(dpi, dpi);
    return true;
}

bool Dx11Renderer::Initialize(HWND window, int width, int height) {
    if (width < 1) {
        width = 1;
    }
    if (height < 1) {
        height = 1;
    }
    pixelWidth = width;
    pixelHeight = height;
    if (!CreateDevice() || !CreateSwapChain(window, width, height) || !CreateTarget()) {
        return false;
    }
    return true;
}

void Dx11Renderer::Resize(int width, int height) {
    if (!swapChain || width < 1 || height < 1) {
        return;
    }
    if (width == pixelWidth && height == pixelHeight && targetBitmap) {
        return;
    }
    pixelWidth = width;
    pixelHeight = height;
    if (d2dContext) {
        d2dContext->SetTarget(nullptr);
    }
    targetBitmap.Reset();
    const HRESULT result = swapChain->ResizeBuffers(0, static_cast<UINT>(width), static_cast<UINT>(height),
                                                    DXGI_FORMAT_UNKNOWN, 0);
    if (!LogHr(result, "Swap chain resize")) {
        return;
    }
    CreateTarget();
}

void Dx11Renderer::SetDpi(float value) {
    if (value < 1.0f) {
        value = 96.0f;
    }
    dpi = value;
    if (d2dContext) {
        d2dContext->SetDpi(dpi, dpi);
    }
}

void Dx11Renderer::SetTransform(float originX, float originY, float scale) {
    if (!d2dContext) {
        return;
    }
    const D2D1_MATRIX_3X2_F transform =
        D2D1::Matrix3x2F::Scale(scale, scale) * D2D1::Matrix3x2F::Translation(originX, originY);
    d2dContext->SetTransform(transform);
}

void Dx11Renderer::BeginFrame(Color clearColor) {
    if (!d2dContext) {
        return;
    }
    d2dContext->SetTransform(D2D1::Matrix3x2F::Identity());
    transformStack.clear();
    opacityStack.clear();
    opacityStack.push_back(1.0f);
    d2dContext->BeginDraw();
    d2dContext->Clear(ToD2d(clearColor, 1.0f));
}

bool Dx11Renderer::EndFrame() {
    if (!d2dContext || !swapChain) {
        return false;
    }
    const HRESULT drawn = d2dContext->EndDraw();
    if (FAILED(drawn)) {
        return LogHr(drawn, "Direct2D EndDraw");
    }
    const HRESULT presented = swapChain->Present(1, 0);
    return LogHr(presented, "Swap chain present");
}

void Dx11Renderer::StrokeRectangle(const Rect& rect, const Color& color, float thickness, float radius) {
    if (!d2dContext || !brush || thickness <= 0.0f) {
        return;
    }
    brush->SetColor(ToD2d(color, opacityStack.back()));
    const float inset = thickness * 0.5f;
    const D2D1_RECT_F box = D2D1::RectF(rect.x + inset, rect.y + inset, rect.x + rect.width - inset, rect.y + rect.height - inset);
    if (radius > 0.0f) {
        const float corner = std::max(0.0f, radius - inset);
        const D2D1_ROUNDED_RECT rounded{box, corner, corner};
        d2dContext->DrawRoundedRectangle(rounded, brush.Get(), thickness);
        return;
    }
    d2dContext->DrawRectangle(box, brush.Get(), thickness);
}

void Dx11Renderer::FillRectangle(const Rect& rect, const Color& color, float radius) {
    if (!d2dContext || !brush) {
        return;
    }
    brush->SetColor(ToD2d(color, opacityStack.back()));
    const D2D1_RECT_F box = D2D1::RectF(rect.x, rect.y, rect.x + rect.width, rect.y + rect.height);
    if (radius > 0.0f) {
        const D2D1_ROUNDED_RECT rounded{box, radius, radius};
        d2dContext->FillRoundedRectangle(rounded, brush.Get());
        return;
    }
    d2dContext->FillRectangle(box, brush.Get());
}

std::wstring WidenAscii(const std::string& text) {
    return std::wstring(text.begin(), text.end());
}

bool LooksLikeFontFile(const std::string& text) {
    if (text.find('/') != std::string::npos || text.find('\\') != std::string::npos) {
        return true;
    }
    const std::string lower = text;
    return lower.size() >= 4 && (lower.rfind(".ttf") == lower.size() - 4 || lower.rfind(".otf") == lower.size() - 4 ||
                                 lower.rfind(".ttc") == lower.size() - 4);
}

IDWriteFontCollection* Dx11Renderer::CollectionForFile(const std::string& path, std::wstring& familyName) {
    const auto found = fileCollections.find(path);
    if (found != fileCollections.end()) {
        familyName = fileFamilies[path];
        return found->second.Get();
    }
    Microsoft::WRL::ComPtr<IDWriteFactory5> factory5;
    if (!writeFactory || FAILED(writeFactory.As(&factory5))) {
        return nullptr;
    }
    const std::wstring widePath = WidenAscii(path);
    Microsoft::WRL::ComPtr<IDWriteFontFile> fontFile;
    if (FAILED(writeFactory->CreateFontFileReference(widePath.c_str(), nullptr, fontFile.GetAddressOf()))) {
        return nullptr;
    }
    Microsoft::WRL::ComPtr<IDWriteFontSetBuilder1> builder;
    if (FAILED(factory5->CreateFontSetBuilder(builder.GetAddressOf()))) {
        return nullptr;
    }
    if (FAILED(builder->AddFontFile(fontFile.Get()))) {
        return nullptr;
    }
    Microsoft::WRL::ComPtr<IDWriteFontSet> fontSet;
    if (FAILED(builder->CreateFontSet(fontSet.GetAddressOf())) || fontSet->GetFontCount() == 0) {
        return nullptr;
    }
    Microsoft::WRL::ComPtr<IDWriteFontFaceReference> faceReference;
    Microsoft::WRL::ComPtr<IDWriteFontFace3> face;
    if (SUCCEEDED(fontSet->GetFontFaceReference(0, faceReference.GetAddressOf())) && SUCCEEDED(faceReference->CreateFontFace(face.GetAddressOf()))) {
        Microsoft::WRL::ComPtr<IDWriteLocalizedStrings> names;
        if (SUCCEEDED(face->GetFamilyNames(names.GetAddressOf())) && names && names->GetCount() > 0) {
            UINT32 length = 0;
            if (SUCCEEDED(names->GetStringLength(0, &length))) {
                std::wstring name(static_cast<std::size_t>(length) + 1, L'\0');
                if (SUCCEEDED(names->GetString(0, name.data(), length + 1))) {
                    name.resize(length);
                    familyName = name;
                }
            }
        }
    }
    if (familyName.empty()) {
        familyName = L"Inter";
    }
    Microsoft::WRL::ComPtr<IDWriteFontCollection1> collection;
    if (FAILED(factory5->CreateFontCollectionFromFontSet(fontSet.Get(), collection.GetAddressOf()))) {
        return nullptr;
    }
    fileFamilies[path] = familyName;
    fileCollections.emplace(path, collection);
    return fileCollections[path].Get();
}

IDWriteFontCollection* Dx11Renderer::InterCollection() {
    if (interCollection) {
        return interCollection.Get();
    }
    Microsoft::WRL::ComPtr<IDWriteFactory5> factory5;
    if (!writeFactory || FAILED(writeFactory.As(&factory5))) {
        return nullptr;
    }
    const std::filesystem::path root = std::filesystem::path(RUNTIME_SOURCE_DIR) / "assets" / "fonts";
    const char* files[] = {"Inter-Regular.ttf", "Inter-Medium.ttf", "Inter-SemiBold.ttf", "Inter-Italic.ttf"};
    Microsoft::WRL::ComPtr<IDWriteFontSetBuilder1> builder;
    if (FAILED(factory5->CreateFontSetBuilder(builder.GetAddressOf()))) {
        return nullptr;
    }
    int added = 0;
    for (const char* file : files) {
        const std::filesystem::path path = root / file;
        if (!std::filesystem::exists(path)) {
            continue;
        }
        Microsoft::WRL::ComPtr<IDWriteFontFile> fontFile;
        const std::wstring wide = path.wstring();
        if (SUCCEEDED(writeFactory->CreateFontFileReference(wide.c_str(), nullptr, fontFile.GetAddressOf())) &&
            SUCCEEDED(builder->AddFontFile(fontFile.Get()))) {
            ++added;
        }
    }
    if (added == 0) {
        return nullptr;
    }
    Microsoft::WRL::ComPtr<IDWriteFontSet> fontSet;
    Microsoft::WRL::ComPtr<IDWriteFontCollection1> collection;
    if (FAILED(builder->CreateFontSet(fontSet.GetAddressOf())) ||
        FAILED(factory5->CreateFontCollectionFromFontSet(fontSet.Get(), collection.GetAddressOf()))) {
        return nullptr;
    }
    interCollection = collection;
    return interCollection.Get();
}

void Dx11Renderer::ApplyTextLayout(IDWriteTextLayout* layout, IDWriteTextFormat* format, const TextStyle& style, unsigned length) {
    if (layout == nullptr) {
        return;
    }
    layout->SetWordWrapping(style.wrap ? DWRITE_WORD_WRAPPING_WRAP : DWRITE_WORD_WRAPPING_NO_WRAP);
    layout->SetTextAlignment(ToHorizontal(style.horizontal));
    layout->SetParagraphAlignment(ToVertical(style.vertical));
    const DWRITE_TEXT_RANGE range{0, length};
    Microsoft::WRL::ComPtr<IDWriteTextLayout1> layout1;
    if (SUCCEEDED(layout->QueryInterface(IID_PPV_ARGS(layout1.GetAddressOf())))) {
        if (style.letterSpacing != 0.0f || style.wordSpacing != 0.0f) {
            layout1->SetCharacterSpacing(style.letterSpacing, style.letterSpacing, style.wordSpacing, range);
        }
        if (style.lineHeight > 0.0f && style.fontSize > 0.0f) {
            const float height = style.fontSize * style.lineHeight;
            layout1->SetLineSpacing(DWRITE_LINE_SPACING_METHOD_UNIFORM, height, height * 0.8f);
        }
    }
    if (style.underline) {
        layout->SetUnderline(TRUE, range);
    }
    if (style.lineThrough) {
        layout->SetStrikethrough(TRUE, range);
    }
    if (style.maxLines > 0 && style.lineHeight > 0.0f) {
        layout->SetMaxHeight(style.fontSize * style.lineHeight * static_cast<float>(style.maxLines));
    }
    DWRITE_TRIMMING trimming{};
    trimming.granularity = style.ellipsis || style.maxLines > 0 ? DWRITE_TRIMMING_GRANULARITY_CHARACTER : DWRITE_TRIMMING_GRANULARITY_NONE;
    Microsoft::WRL::ComPtr<IDWriteInlineObject> sign;
    if (trimming.granularity != DWRITE_TRIMMING_GRANULARITY_NONE) {
        writeFactory->CreateEllipsisTrimmingSign(format, sign.GetAddressOf());
    }
    layout->SetTrimming(&trimming, sign.Get());
    if (Microsoft::WRL::ComPtr<IDWriteTypography> typography = TypographyFor(style.fontFeatures)) {
        layout->SetTypography(typography.Get(), range);
    }
}

// The features a run is drawn with, read from the four-letter tags a document writes them as. They
// live apart from the layout so the features of a whole block and the features of one run of it are
// read the same way: both are handed to the range they belong to as one typography.
Microsoft::WRL::ComPtr<IDWriteTypography> Dx11Renderer::TypographyFor(const std::string& features) {
    Microsoft::WRL::ComPtr<IDWriteTypography> typography;
    if (features.empty() || !writeFactory || FAILED(writeFactory->CreateTypography(typography.GetAddressOf()))) {
        return typography;
    }
    std::string tag;
    for (std::size_t index = 0; index <= features.size(); ++index) {
        const char character = index == features.size() ? ',' : features[index];
        if (character == ',' || character == ' ') {
            if (tag.size() == 4) {
                const UINT32 name = static_cast<unsigned char>(tag[0]) | (static_cast<unsigned char>(tag[1]) << 8) |
                                    (static_cast<unsigned char>(tag[2]) << 16) | (static_cast<unsigned char>(tag[3]) << 24);
                typography->AddFontFeature(DWRITE_FONT_FEATURE{static_cast<DWRITE_FONT_FEATURE_TAG>(name), 1});
            }
            tag.clear();
        } else {
            tag.push_back(character);
        }
    }
    return typography;
}

IDWriteTextFormat* Dx11Renderer::FormatFor(const TextStyle& style) {
    if (!writeFactory) {
        return nullptr;
    }
    const int weight = style.fontWeight < 100 ? 400 : style.fontWeight;
    std::string family = style.fontFamily.empty() ? "Inter" : style.fontFamily;
    std::string file = style.fontFile;
    if (file.empty() && LooksLikeFontFile(family)) {
        file = family;
        family = "Inter";
    }
    const std::string key = family + "|" + file + "|" + style.locale + "|" + std::to_string(weight) + "|" +
                            std::to_string(static_cast<int>(style.fontSize * 100.0f)) + (style.italic ? "|i" : "|n") +
                            (style.rtl ? "|r" : "|l") + (style.verticalWriting ? "|v" : "|h");
    const auto found = formats.find(key);
    if (found != formats.end()) {
        return found->second.Get();
    }
    std::wstring familyName = WidenAscii(family);
    IDWriteFontCollection* collection = nullptr;
    if (!file.empty()) {
        collection = CollectionForFile(file, familyName);
    } else if (family == "Inter") {
        collection = InterCollection();
    }
    const std::wstring locale = style.locale.empty() ? L"en-us" : WidenAscii(style.locale);
    Microsoft::WRL::ComPtr<IDWriteTextFormat> format;
    const DWRITE_FONT_STYLE fontStyle = style.italic ? DWRITE_FONT_STYLE_ITALIC : DWRITE_FONT_STYLE_NORMAL;
    HRESULT result = writeFactory->CreateTextFormat(familyName.c_str(), collection, static_cast<DWRITE_FONT_WEIGHT>(weight), fontStyle,
                                                    DWRITE_FONT_STRETCH_NORMAL, style.fontSize, locale.c_str(), format.GetAddressOf());
    if (FAILED(result)) {
        result = writeFactory->CreateTextFormat(L"Segoe UI", nullptr, static_cast<DWRITE_FONT_WEIGHT>(weight), fontStyle, DWRITE_FONT_STRETCH_NORMAL,
                                                style.fontSize, locale.c_str(), format.GetAddressOf());
    }
    if (FAILED(result)) {
        return nullptr;
    }
    format->SetReadingDirection(style.rtl ? DWRITE_READING_DIRECTION_RIGHT_TO_LEFT : DWRITE_READING_DIRECTION_LEFT_TO_RIGHT);
    format->SetWordWrapping(style.wrap ? DWRITE_WORD_WRAPPING_WRAP : DWRITE_WORD_WRAPPING_NO_WRAP);
    Microsoft::WRL::ComPtr<IDWriteTextFormat1> format1;
    if (style.verticalWriting && SUCCEEDED(format.As(&format1))) {
        format1->SetVerticalGlyphOrientation(DWRITE_VERTICAL_GLYPH_ORIENTATION_DEFAULT);
    }
    formats.emplace(key, format);
    return format.Get();
}

void Dx11Renderer::DrawCachedImage(std::string_view key, const Rect& rect, const unsigned char* bgra, int width, int height) {
    if (!d2dContext || bgra == nullptr || width < 1 || height < 1 || rect.width <= 0.0f || rect.height <= 0.0f) {
        return;
    }
    const std::string id(key);
    auto found = imageCache.find(id);
    if (found == imageCache.end()) {
        const D2D1_BITMAP_PROPERTIES1 properties = D2D1::BitmapProperties1(
            D2D1_BITMAP_OPTIONS_NONE, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
        Microsoft::WRL::ComPtr<ID2D1Bitmap1> bitmap;
        const HRESULT result = d2dContext->CreateBitmap(D2D1::SizeU(static_cast<UINT32>(width), static_cast<UINT32>(height)), bgra,
                                                        static_cast<UINT32>(width * 4), properties, bitmap.GetAddressOf());
        if (FAILED(result)) {
            return;
        }
        found = imageCache.emplace(id, bitmap).first;
    }
    const D2D1_RECT_F destination = D2D1::RectF(rect.x, rect.y, rect.x + rect.width, rect.y + rect.height);
    d2dContext->DrawBitmap(found->second.Get(), destination, opacityStack.back(), D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC);
}

void Dx11Renderer::DrawVector(const VectorPaint& paint) {
    if (!d2dContext || !d2dFactory || !brush || paint.data.empty() || paint.bounds.width <= 0.0f || paint.bounds.height <= 0.0f) {
        return;
    }
    const std::vector<PathContour> contours = SampleContours(paint.data);
    if (contours.empty()) {
        return;
    }
    Microsoft::WRL::ComPtr<ID2D1PathGeometry> geometry;
    if (FAILED(d2dFactory->CreatePathGeometry(geometry.GetAddressOf()))) {
        return;
    }
    Microsoft::WRL::ComPtr<ID2D1GeometrySink> sink;
    if (FAILED(geometry->Open(sink.GetAddressOf()))) {
        return;
    }
    sink->SetFillMode(paint.evenOdd ? D2D1_FILL_MODE_ALTERNATE : D2D1_FILL_MODE_WINDING);
    bool drew = false;
    for (const PathContour& contour : contours) {
        if (contour.points.size() < 2) {
            continue;
        }
        const bool sameEnd = std::fabs(contour.points.front().x - contour.points.back().x) < 0.01f && std::fabs(contour.points.front().y - contour.points.back().y) < 0.01f;
        const bool closed = contour.closed || sameEnd;
        const std::size_t count = closed && sameEnd ? contour.points.size() - 1 : contour.points.size();
        if (count < 2) {
            continue;
        }
        sink->BeginFigure(D2D1::Point2F(contour.points.front().x, contour.points.front().y), D2D1_FIGURE_BEGIN_FILLED);
        std::vector<D2D1_POINT_2F> points;
        points.reserve(count - 1);
        for (std::size_t index = 1; index < count; ++index) {
            points.push_back(D2D1::Point2F(contour.points[index].x, contour.points[index].y));
        }
        if (!points.empty()) {
            sink->AddLines(points.data(), static_cast<UINT32>(points.size()));
        }
        sink->EndFigure(closed ? D2D1_FIGURE_END_CLOSED : D2D1_FIGURE_END_OPEN);
        drew = true;
    }
    if (FAILED(sink->Close()) || !drew) {
        return;
    }
    float viewX = paint.viewX;
    float viewY = paint.viewY;
    float scaleX = 1.0f;
    float scaleY = 1.0f;
    if (paint.viewWidth > 0.0f && paint.viewHeight > 0.0f) {
        scaleX = paint.bounds.width / paint.viewWidth;
        scaleY = paint.bounds.height / paint.viewHeight;
    } else {
        viewX = 0.0f;
        viewY = 0.0f;
    }
    D2D1_MATRIX_3X2_F previous = D2D1::Matrix3x2F::Identity();
    d2dContext->GetTransform(&previous);
    const D2D1_MATRIX_3X2_F local = D2D1::Matrix3x2F::Translation(-viewX, -viewY) * D2D1::Matrix3x2F::Scale(scaleX, scaleY) *
                                    D2D1::Matrix3x2F::Translation(paint.bounds.x, paint.bounds.y);
    d2dContext->SetTransform(local * previous);
    if (paint.fillEnabled && paint.fill.a > 0.0f) {
        brush->SetColor(ToD2d(paint.fill, opacityStack.back()));
        d2dContext->FillGeometry(geometry.Get(), brush.Get());
    }
    if (paint.stroke.a > 0.0f && paint.strokeWidth > 0.0f) {
        D2D1_STROKE_STYLE_PROPERTIES style = D2D1::StrokeStyleProperties();
        const D2D1_CAP_STYLE cap = paint.lineCap == 'r' ? D2D1_CAP_STYLE_ROUND : paint.lineCap == 's' ? D2D1_CAP_STYLE_SQUARE : D2D1_CAP_STYLE_FLAT;
        style.startCap = cap;
        style.endCap = cap;
        style.dashCap = cap;
        style.lineJoin = paint.lineJoin == 'r' ? D2D1_LINE_JOIN_ROUND : paint.lineJoin == 'b' ? D2D1_LINE_JOIN_BEVEL : D2D1_LINE_JOIN_MITER;
        style.miterLimit = 4.0f;
        if (!paint.dashes.empty()) {
            style.dashStyle = D2D1_DASH_STYLE_CUSTOM;
            // The pattern is written in pixels and so is the offset, but the platform measures an
            // offset in multiples of the stroke width, so it is converted on the way in.
            style.dashOffset = paint.strokeWidth > 0.0f ? paint.dashOffset / paint.strokeWidth : 0.0f;
        }
        Microsoft::WRL::ComPtr<ID2D1StrokeStyle> strokeStyle;
        d2dFactory->CreateStrokeStyle(style, paint.dashes.empty() ? nullptr : paint.dashes.data(),
                                      static_cast<UINT32>(paint.dashes.size()), strokeStyle.GetAddressOf());
        brush->SetColor(ToD2d(paint.stroke, opacityStack.back()));
        d2dContext->DrawGeometry(geometry.Get(), brush.Get(), paint.strokeWidth, strokeStyle.Get());
    }
    d2dContext->SetTransform(previous);
}

void Dx11Renderer::DrawString(std::string_view text, const Rect& rect, const TextStyle& style) {
    if (!d2dContext || !brush || !writeFactory || rect.width <= 0.0f || rect.height <= 0.0f) {
        return;
    }
    IDWriteTextFormat* format = FormatFor(style);
    if (format == nullptr) {
        return;
    }
    Microsoft::WRL::ComPtr<IDWriteTextLayout> layout = LayoutFor(text, rect.width, rect.height, style);
    if (!layout) {
        return;
    }
    brush->SetColor(ToD2d(style.color, opacityStack.back()));
    const D2D1_DRAW_TEXT_OPTIONS options = style.maxLines > 0 || style.ellipsis ? D2D1_DRAW_TEXT_OPTIONS_CLIP : D2D1_DRAW_TEXT_OPTIONS_NONE;
    d2dContext->DrawTextLayout(D2D1::Point2F(rect.x, rect.y), layout.Get(), brush.Get(), options);
    if (style.overline && rect.width > 0.0f) {
        brush->SetColor(ToD2d(style.decorationColor.a > 0.0f ? style.decorationColor : style.color, opacityStack.back()));
        const float thickness = style.decorationWidth > 0.0f ? style.decorationWidth : 1.0f;
        d2dContext->FillRectangle(D2D1::RectF(rect.x, rect.y, rect.x + rect.width, rect.y + thickness), brush.Get());
    }
}

Microsoft::WRL::ComPtr<IDWriteTextLayout> Dx11Renderer::ParagraphFor(const std::vector<TextRun>& runs, const TextStyle& paragraph,
                                                                   float width, float height,
                                                                   std::vector<RunRange>& ranges,
                                                                   std::vector<Microsoft::WRL::ComPtr<ID2D1SolidColorBrush>>* brushes) {
    ranges.clear();
    Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
    if (!writeFactory) {
        return layout;
    }
    IDWriteTextFormat* format = FormatFor(paragraph);
    if (format == nullptr) {
        return layout;
    }
    // The runs of a paragraph are one string to the platform, and each run is a stretch of it. So the
    // text is written out in the order the runs are set and the place each one begins is kept, which
    // is what says which stretch of the paragraph a run's own values belong to.
    std::wstring text;
    float largest = paragraph.fontSize;
    for (const TextRun& run : runs) {
        const std::wstring piece = ToWide(run.text);
        RunRange range;
        range.start = static_cast<unsigned>(text.size());
        range.length = static_cast<unsigned>(piece.size());
        ranges.push_back(range);
        text += piece;
        // The tallest run is what the lines of the paragraph have to hold, so a run set larger than
        // the block it sits in makes the lines taller rather than hanging out of them.
        largest = std::max(largest, run.style.fontSize);
    }
    if (FAILED(writeFactory->CreateTextLayout(text.c_str(), static_cast<UINT32>(text.size()), format, width, height,
                                              layout.GetAddressOf()))) {
        ranges.clear();
        return layout;
    }
    // The fallback the platform keeps for the characters the family of the paragraph does not hold.
    Microsoft::WRL::ComPtr<IDWriteFactory2> factory2;
    if (SUCCEEDED(writeFactory.As(&factory2))) {
        Microsoft::WRL::ComPtr<IDWriteFontFallback> fallback;
        Microsoft::WRL::ComPtr<IDWriteTextLayout2> layout2;
        if (SUCCEEDED(factory2->GetSystemFontFallback(fallback.GetAddressOf())) && SUCCEEDED(layout.As(&layout2))) {
            layout2->SetFontFallback(fallback.Get());
        }
    }
    // The paragraph first, so the values every run of it shares are laid over the whole of it. The
    // lines are spaced for the tallest run, because a line holds every run on it and the tallest one
    // is the one that decides how much room the line needs.
    TextStyle whole = paragraph;
    whole.fontSize = largest;
    ApplyTextLayout(layout.Get(), format, whole, static_cast<unsigned>(text.size()));
    for (std::size_t index = 0; index < runs.size(); ++index) {
        const TextStyle& style = runs[index].style;
        const RunRange& range = ranges[index];
        if (range.length == 0) {
            continue;
        }
        const DWRITE_TEXT_RANGE span{range.start, range.length};
        layout->SetFontSize(style.fontSize, span);
        layout->SetFontWeight(static_cast<DWRITE_FONT_WEIGHT>(style.fontWeight < 100 ? 400 : style.fontWeight), span);
        layout->SetFontFamilyName(WidenAscii(style.fontFamily.empty() ? std::string("Inter") : style.fontFamily).c_str(), span);
        layout->SetFontStyle(style.italic ? DWRITE_FONT_STYLE_ITALIC : DWRITE_FONT_STYLE_NORMAL, span);
        // The lines a run is drawn with are the run's own, so a run that says it is underlined is
        // underlined even where the block around it is not, and one that says nothing keeps whatever
        // the block said about the whole of its text.
        layout->SetUnderline(style.underline ? TRUE : FALSE, span);
        layout->SetStrikethrough(style.lineThrough ? TRUE : FALSE, span);
        if (!style.locale.empty()) {
            layout->SetLocaleName(WidenAscii(style.locale).c_str(), span);
        }
        if (style.letterSpacing != 0.0f || style.wordSpacing != 0.0f) {
            Microsoft::WRL::ComPtr<IDWriteTextLayout1> spaced;
            if (SUCCEEDED(layout.As(&spaced))) {
                spaced->SetCharacterSpacing(style.letterSpacing, style.letterSpacing, style.wordSpacing, span);
            }
        }
        if (Microsoft::WRL::ComPtr<IDWriteTypography> typography = TypographyFor(style.fontFeatures)) {
            layout->SetTypography(typography.Get(), span);
        }
        if (brushes != nullptr && d2dContext) {
            // A run's colour reaches the draw as the effect the range is drawn with, because a layout
            // is drawn in one colour unless a range is handed something else to be drawn in. The
            // brush is kept by the caller so it outlives the draw that reads it.
            const float opacity = opacityStack.empty() ? 1.0f : opacityStack.back();
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> inked;
            if (SUCCEEDED(d2dContext->CreateSolidColorBrush(ToD2d(style.color, opacity), inked.GetAddressOf()))) {
                layout->SetDrawingEffect(inked.Get(), span);
                brushes->push_back(std::move(inked));
            }
        }
    }
    return layout;
}

std::vector<Rect> Dx11Renderer::RunBoxes(IDWriteTextLayout* layout, const std::vector<RunRange>& ranges) const {
    // Every run is given a box, so the boxes answer for the runs one for one: a run with nothing in
    // it covers nothing and is given the box it covers rather than being left out, which is what
    // keeps the place of a run in the line the place of its box among the boxes.
    std::vector<Rect> boxes(ranges.size(), Rect{0.0f, 0.0f, 0.0f, 0.0f});
    if (layout == nullptr) {
        return boxes;
    }
    for (std::size_t index = 0; index < ranges.size(); ++index) {
        if (ranges[index].length == 0) {
            continue;
        }
        UINT32 count = 0;
        if (FAILED(layout->HitTestTextRange(ranges[index].start, ranges[index].length, 0.0f, 0.0f, nullptr, 0, &count)) || count == 0) {
            continue;
        }
        std::vector<DWRITE_HIT_TEST_METRICS> metrics(count);
        UINT32 fetched = 0;
        if (FAILED(layout->HitTestTextRange(ranges[index].start, ranges[index].length, 0.0f, 0.0f, metrics.data(), count, &fetched))) {
            continue;
        }
        // A run that wraps covers one rectangle per line, and the box it is given is the box around
        // all of them: the stretch of text it holds reaches from the first line to the last.
        float left = metrics[0].left;
        float top = metrics[0].top;
        float right = metrics[0].left + metrics[0].width;
        float bottom = metrics[0].top + metrics[0].height;
        for (UINT32 found = 1; found < fetched; ++found) {
            left = std::min(left, metrics[found].left);
            top = std::min(top, metrics[found].top);
            right = std::max(right, metrics[found].left + metrics[found].width);
            bottom = std::max(bottom, metrics[found].top + metrics[found].height);
        }
        boxes[index] = Rect{left, top, std::max(0.0f, right - left), std::max(0.0f, bottom - top)};
    }
    return boxes;
}

TextParagraph Dx11Renderer::MeasureParagraph(const std::vector<TextRun>& runs, const TextStyle& paragraph, float width, float height) {
    TextParagraph measured;
    if (!writeFactory || runs.empty()) {
        measured.size = Size{0.0f, paragraph.fontSize};
        measured.runs.assign(runs.size(), Rect{0.0f, 0.0f, 0.0f, 0.0f});
        return measured;
    }
    // The width a caller names holds the lines of the paragraph; a width left open is a paragraph
    // that keeps to one line, whatever the style would rather do. The height is the box the lines are
    // placed in, which is what the alignment of the paragraph is against.
    const bool wrap = paragraph.wrap && width < UNBOUNDED_MEASURE;
    TextStyle measuring = paragraph;
    measuring.wrap = wrap;
    const float layoutWidth = wrap ? std::max(1.0f, width) : UNBOUNDED_MEASURE;
    const float layoutHeight = height > 0.0f ? height : UNBOUNDED_MEASURE;
    std::vector<RunRange> ranges;
    Microsoft::WRL::ComPtr<IDWriteTextLayout> layout = ParagraphFor(runs, measuring, layoutWidth, layoutHeight, ranges, nullptr);
    if (!layout) {
        measured.size = Size{runs.front().text.size() * paragraph.fontSize * 0.5f, paragraph.fontSize};
        measured.runs.assign(runs.size(), Rect{0.0f, 0.0f, 0.0f, 0.0f});
        return measured;
    }
    DWRITE_TEXT_METRICS metrics{};
    if (FAILED(layout->GetMetrics(&metrics))) {
        measured.size = Size{0.0f, paragraph.fontSize};
    } else {
        measured.size = Size{metrics.width, std::max(metrics.height, paragraph.fontSize)};
    }
    measured.runs = RunBoxes(layout.Get(), ranges);
    return measured;
}

void Dx11Renderer::DrawParagraph(const std::vector<TextRun>& runs, const Rect& rect, const TextStyle& paragraph) {
    if (!d2dContext || !brush || !writeFactory || runs.empty() || rect.width <= 0.0f || rect.height <= 0.0f) {
        return;
    }
    std::vector<RunRange> ranges;
    std::vector<Microsoft::WRL::ComPtr<ID2D1SolidColorBrush>> brushes;
    Microsoft::WRL::ComPtr<IDWriteTextLayout> layout = ParagraphFor(runs, paragraph, rect.width, rect.height, ranges, &brushes);
    if (!layout) {
        return;
    }
    // The colour of the paragraph draws every run that was not handed a colour of its own.
    brush->SetColor(ToD2d(paragraph.color, opacityStack.empty() ? 1.0f : opacityStack.back()));
    const D2D1_DRAW_TEXT_OPTIONS options = paragraph.maxLines > 0 || paragraph.ellipsis ? D2D1_DRAW_TEXT_OPTIONS_CLIP : D2D1_DRAW_TEXT_OPTIONS_NONE;
    d2dContext->DrawTextLayout(D2D1::Point2F(rect.x, rect.y), layout.Get(), brush.Get(), options);
    if (paragraph.overline && rect.width > 0.0f) {
        brush->SetColor(ToD2d(paragraph.decorationColor.a > 0.0f ? paragraph.decorationColor : paragraph.color, opacityStack.empty() ? 1.0f : opacityStack.back()));
        const float thickness = paragraph.decorationWidth > 0.0f ? paragraph.decorationWidth : 1.0f;
        d2dContext->FillRectangle(D2D1::RectF(rect.x, rect.y, rect.x + rect.width, rect.y + thickness), brush.Get());
    }
}

Microsoft::WRL::ComPtr<IDWriteTextLayout> Dx11Renderer::LayoutFor(std::string_view text, float width, float height,
                                                                   const TextStyle& style) {
    Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
    if (!writeFactory) {
        return layout;
    }
    IDWriteTextFormat* format = FormatFor(style);
    if (format == nullptr) {
        return layout;
    }
    const std::wstring wide = ToWide(text);
    if (FAILED(writeFactory->CreateTextLayout(wide.c_str(), static_cast<UINT32>(wide.size()), format, width, height,
                                              layout.GetAddressOf()))) {
        return layout;
    }
    Microsoft::WRL::ComPtr<IDWriteFactory2> factory2;
    if (SUCCEEDED(writeFactory.As(&factory2))) {
        Microsoft::WRL::ComPtr<IDWriteFontFallback> fallback;
        Microsoft::WRL::ComPtr<IDWriteTextLayout2> layout2;
        if (SUCCEEDED(factory2->GetSystemFontFallback(fallback.GetAddressOf())) && SUCCEEDED(layout.As(&layout2))) {
            layout2->SetFontFallback(fallback.Get());
        }
    }
    ApplyTextLayout(layout.Get(), format, style, static_cast<unsigned>(wide.size()));
    return layout;
}

Size Dx11Renderer::MeasureText(std::string_view text, const TextStyle& style, float maxWidth) {
    if (!writeFactory || text.empty()) {
        return Size{0.0f, style.fontSize};
    }
    // The width that was given is the width the lines are held in, so a width that was left open
    // is a block that keeps to one line, whatever the style would rather do.
    const bool wrap = style.wrap && maxWidth < UNBOUNDED_MEASURE;
    TextStyle measuring = style;
    measuring.wrap = wrap;
    const float layoutWidth = wrap ? std::max(1.0f, maxWidth) : UNBOUNDED_MEASURE;
    Microsoft::WRL::ComPtr<IDWriteTextLayout> layout = LayoutFor(text, layoutWidth, UNBOUNDED_MEASURE, measuring);
    if (!layout) {
        return Size{text.size() * style.fontSize * 0.5f, style.fontSize};
    }
    DWRITE_TEXT_METRICS metrics{};
    if (FAILED(layout->GetMetrics(&metrics))) {
        return Size{0.0f, style.fontSize};
    }
    // The box the layout covers is the box the lines fill, because the spacing and the cap on the
    // lines were applied to it above, which is where the line height of the document reaches the
    // height of the block.
    return Size{metrics.width, std::max(metrics.height, style.fontSize)};
}

void Dx11Renderer::PushClip(const Rect& rect) {
    if (!d2dContext) {
        return;
    }
    d2dContext->PushAxisAlignedClip(D2D1::RectF(rect.x, rect.y, rect.x + rect.width, rect.y + rect.height),
                                    D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
}

void Dx11Renderer::PopClip() {
    if (d2dContext) {
        d2dContext->PopAxisAlignedClip();
    }
}

void Dx11Renderer::PushRotation(float degrees, float centerX, float centerY) {
    if (!d2dContext) {
        return;
    }
    D2D1_MATRIX_3X2_F previous = D2D1::Matrix3x2F::Identity();
    d2dContext->GetTransform(&previous);
    transformStack.push_back(previous);
    const D2D1_MATRIX_3X2_F spin = D2D1::Matrix3x2F::Rotation(degrees, D2D1::Point2F(centerX, centerY));
    d2dContext->SetTransform(spin * previous);
}

void Dx11Renderer::PopTransform() {
    if (!d2dContext || transformStack.empty()) {
        return;
    }
    d2dContext->SetTransform(transformStack.back());
    transformStack.pop_back();
}

void Dx11Renderer::PushOpacity(float opacity) {
    const float parent = opacityStack.empty() ? 1.0f : opacityStack.back();
    const float next = opacity < 0.0f ? 0.0f : (opacity > 1.0f ? 1.0f : opacity);
    opacityStack.push_back(parent * next);
}

void Dx11Renderer::PopOpacity() {
    if (opacityStack.size() > 1) {
        opacityStack.pop_back();
    }
}

}  // namespace Runtime
