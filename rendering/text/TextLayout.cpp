#include "rendering/text/TextLayout.h"

#include <windows.h>

#include <dwrite_2.h>
#include <wrl/client.h>

#include <algorithm>
#include <string>

namespace Runtime {
namespace {

std::wstring ToWide(std::string_view text) {
    if (text.empty()) {
        return {};
    }
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring wide(size > 0 ? static_cast<std::size_t>(size) : 0, L'\0');
    if (size > 0) {
        MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), size);
    }
    return wide;
}

std::wstring ToWideString(const std::string& text, const wchar_t* fallback) {
    if (text.empty()) {
        return fallback;
    }
    return ToWide(text);
}

}  // namespace

ShapedText ShapeText(std::string_view text, const TextStyle& style, float maxWidth) {
    ShapedText shaped;
    shaped.rtl = style.rtl;
    Microsoft::WRL::ComPtr<IDWriteFactory> factory;
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                   reinterpret_cast<IUnknown**>(factory.GetAddressOf())))) {
        return shaped;
    }
    const std::wstring family = ToWideString(style.fontFamily, L"Segoe UI");
    const std::wstring locale = ToWideString(style.locale, L"en-us");
    Microsoft::WRL::ComPtr<IDWriteTextFormat> format;
    const DWRITE_FONT_STYLE fontStyle = style.italic ? DWRITE_FONT_STYLE_ITALIC : DWRITE_FONT_STYLE_NORMAL;
    const int weight = style.fontWeight < 100 ? 400 : style.fontWeight;
    if (FAILED(factory->CreateTextFormat(family.c_str(), nullptr, static_cast<DWRITE_FONT_WEIGHT>(weight), fontStyle,
                                         DWRITE_FONT_STRETCH_NORMAL, style.fontSize, locale.c_str(), format.GetAddressOf()))) {
        return shaped;
    }
    format->SetReadingDirection(style.rtl ? DWRITE_READING_DIRECTION_RIGHT_TO_LEFT : DWRITE_READING_DIRECTION_LEFT_TO_RIGHT);
    const std::wstring wide = ToWide(text);
    const float layoutWidth = maxWidth < 100000.0f ? std::max(1.0f, maxWidth) : 100000.0f;
    Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
    if (FAILED(factory->CreateTextLayout(wide.c_str(), static_cast<UINT32>(wide.size()), format.Get(), layoutWidth, 100000.0f,
                                        layout.GetAddressOf()))) {
        return shaped;
    }
    Microsoft::WRL::ComPtr<IDWriteFactory2> factory2;
    if (SUCCEEDED(factory.As(&factory2))) {
        Microsoft::WRL::ComPtr<IDWriteFontFallback> fallback;
        if (SUCCEEDED(factory2->GetSystemFontFallback(fallback.GetAddressOf()))) {
            Microsoft::WRL::ComPtr<IDWriteTextLayout2> layout2;
            if (SUCCEEDED(layout.As(&layout2))) {
                layout2->SetFontFallback(fallback.Get());
                shaped.fallback = true;
            }
        }
    }
    layout->SetWordWrapping(style.wrap ? DWRITE_WORD_WRAPPING_WRAP : DWRITE_WORD_WRAPPING_NO_WRAP);
    // A line height the document asked for spaces the lines the way the draw will space them, so
    // the metrics below are the box the lines cover rather than the box the font would like.
    Microsoft::WRL::ComPtr<IDWriteTextLayout1> layout1;
    if (SUCCEEDED(layout.As(&layout1)) && style.lineHeight > 0.0f && style.fontSize > 0.0f) {
        const float height = style.fontSize * style.lineHeight;
        layout1->SetLineSpacing(DWRITE_LINE_SPACING_METHOD_UNIFORM, height, height * 0.8f);
    }
    DWRITE_TEXT_METRICS metrics{};
    if (FAILED(layout->GetMetrics(&metrics))) {
        return shaped;
    }
    shaped.width = metrics.width;
    shaped.height = metrics.height;
    shaped.shaped = metrics.width > 0.0f || text.empty();
    return shaped;
}

}  // namespace Runtime
