#include "platform/windows/Dx11Renderer.h"

#include "engine/core/Log.h"
#include "engine/theme/DevKitTheme.h"
#include "platform/windows/Win32Window.h"

#include <d2d1_1helper.h>

#include <algorithm>
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
        const D2D1_ROUNDED_RECT rounded{box, radius, radius};
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

IDWriteTextFormat* Dx11Renderer::FormatFor(float fontSize, int fontWeight) {
    if (!writeFactory) {
        return nullptr;
    }
    const int weight = fontWeight < 100 ? 400 : fontWeight;
    const int key = static_cast<int>(fontSize * 100.0f) * 1000 + weight;
    const auto found = formats.find(key);
    if (found != formats.end()) {
        return found->second.Get();
    }
    Microsoft::WRL::ComPtr<IDWriteTextFormat> format;
    const HRESULT result = writeFactory->CreateTextFormat(
        L"Segoe UI", nullptr, static_cast<DWRITE_FONT_WEIGHT>(weight), DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        fontSize, L"en-us", format.GetAddressOf());
    if (FAILED(result)) {
        return nullptr;
    }
    format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    formats.emplace(key, format);
    return format.Get();
}

void Dx11Renderer::DrawString(std::string_view text, const Rect& rect, const TextStyle& style) {
    if (!d2dContext || !brush || !writeFactory || rect.width <= 0.0f || rect.height <= 0.0f) {
        return;
    }
    IDWriteTextFormat* format = FormatFor(style.fontSize, style.fontWeight);
    if (format == nullptr) {
        return;
    }
    const std::wstring wide = ToWide(text);
    Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
    const HRESULT result =
        writeFactory->CreateTextLayout(wide.c_str(), static_cast<UINT32>(wide.size()), format, rect.width, rect.height,
                                       layout.GetAddressOf());
    if (FAILED(result)) {
        return;
    }
    layout->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
    layout->SetTextAlignment(ToHorizontal(style.horizontal));
    layout->SetParagraphAlignment(ToVertical(style.vertical));
    brush->SetColor(ToD2d(style.color, opacityStack.back()));
    d2dContext->DrawTextLayout(D2D1::Point2F(rect.x, rect.y), layout.Get(), brush.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

Size Dx11Renderer::MeasureText(std::string_view text, float fontSize, float maxWidth) {
    if (!writeFactory || text.empty()) {
        return Size{0.0f, fontSize};
    }
    IDWriteTextFormat* format = FormatFor(fontSize, WEIGHT_REGULAR);
    if (format == nullptr) {
        return Size{text.size() * fontSize * 0.5f, fontSize};
    }
    const std::wstring wide = ToWide(text);
    const bool wrap = maxWidth < UNBOUNDED_MEASURE;
    const float layoutWidth = wrap ? std::max(1.0f, maxWidth) : UNBOUNDED_MEASURE;
    Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
    const HRESULT result = writeFactory->CreateTextLayout(wide.c_str(), static_cast<UINT32>(wide.size()), format,
                                                          layoutWidth, UNBOUNDED_MEASURE, layout.GetAddressOf());
    if (FAILED(result)) {
        return Size{0.0f, fontSize};
    }
    layout->SetWordWrapping(wrap ? DWRITE_WORD_WRAPPING_WRAP : DWRITE_WORD_WRAPPING_NO_WRAP);
    DWRITE_TEXT_METRICS metrics{};
    layout->GetMetrics(&metrics);
    return Size{metrics.width, std::max(metrics.height, fontSize)};
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
