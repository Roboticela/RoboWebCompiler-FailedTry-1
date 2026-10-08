#pragma once

#include "rendering/renderer/Renderer.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <d2d1_1.h>
#include <d3d11.h>
#include <dwrite_3.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include <string>
#include <unordered_map>
#include <vector>

namespace Runtime {

class Dx11Renderer : public Renderer {
public:
    ~Dx11Renderer() override;

    bool Initialize(HWND window, int width, int height);
    void Resize(int width, int height);

    void SetDpi(float value) override;
    void SetTransform(float originX, float originY, float scale) override;
    void BeginFrame(Color clearColor) override;
    bool EndFrame() override;
    void FillRectangle(const Rect& rect, const Color& color, float radius) override;
    void StrokeRectangle(const Rect& rect, const Color& color, float thickness, float radius) override;
    void DrawString(std::string_view text, const Rect& rect, const TextStyle& style) override;
    void DrawParagraph(const std::vector<TextRun>& runs, const Rect& rect, const TextStyle& paragraph) override;
    void DrawCachedImage(std::string_view key, const Rect& rect, const unsigned char* bgra, int width, int height) override;
    void DrawVector(const VectorPaint& paint) override;
    // The measuring form below is the one every element calls; this is the same measurement for a
    // caller that names only a font size, which is what the chrome of the designer does.
    using TextMeasurer::MeasureText;
    Size MeasureText(std::string_view text, const TextStyle& style, float maxWidth) override;
    TextParagraph MeasureParagraph(const std::vector<TextRun>& runs, const TextStyle& paragraph, float width, float height) override;
    void PushClip(const Rect& rect) override;
    void PushClipPath(const Rect& bounds, std::string_view path) override;
    void PopClip() override;
    void PushOpacity(float opacity) override;
    void PopOpacity() override;
    void PushRotation(float degrees, float centerX, float centerY) override;
    void PopTransform() override;

private:
    bool CreateDevice();
    bool CreateSwapChain(HWND window, int width, int height);
    bool CreateTarget();
    // The geometry one path covers, read the one way every path in the runtime is read: a shape's
    // outline, the mask a shaped clip cuts with, and the paths of an imported file are all the same
    // path, so building one is building them all.
    Microsoft::WRL::ComPtr<ID2D1PathGeometry> PathGeometryFor(std::string_view data, bool evenOdd);
    IDWriteTextFormat* FormatFor(const TextStyle& style);
    IDWriteFontCollection* InterCollection();
    IDWriteFontCollection* CollectionForFile(const std::string& path, std::wstring& familyName);
    void ApplyTextLayout(IDWriteTextLayout* layout, IDWriteTextFormat* format, const TextStyle& style, unsigned length);
    // The features a run of text is drawn with, read from the tags a document writes them as.
    Microsoft::WRL::ComPtr<IDWriteTypography> TypographyFor(const std::string& features);
    // The layout a block is both drawn and measured in. One builder for the two, so the box a
    // block covers is the box it fills.
    Microsoft::WRL::ComPtr<IDWriteTextLayout> LayoutFor(std::string_view text, float width, float height, const TextStyle& style);
    // Where one run begins in the text of a paragraph and how much of that text is the run's. The
    // runs of a paragraph are written as one string, so a run is a stretch of that string, and the
    // places they begin are what both the formatting of a run and the box it lands in are read from.
    struct RunRange {
        unsigned start = 0;
        unsigned length = 0;
    };
    // The layout a paragraph of runs is both drawn and measured in. One builder for the two, so the
    // placement a run is given is the placement it is drawn at. The place each run begins in the
    // text of the paragraph comes back with it, because the runs of a paragraph are one string to
    // the platform and a run is a stretch of it.
    Microsoft::WRL::ComPtr<IDWriteTextLayout> ParagraphFor(const std::vector<TextRun>& runs, const TextStyle& paragraph, float width,
                                                           float height, std::vector<RunRange>& ranges,
                                                           std::vector<Microsoft::WRL::ComPtr<ID2D1SolidColorBrush>>* brushes);
    // The box each run landed in, read back from the layout the runs were placed by: DirectWrite is
    // asked for the rectangles a run covers and the box the run is given is the box around all of
    // them, so a run that wraps is given the box around the lines it reaches.
    std::vector<Rect> RunBoxes(IDWriteTextLayout* layout, const std::vector<RunRange>& ranges) const;

    Microsoft::WRL::ComPtr<ID3D11Device> device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> deviceContext;
    Microsoft::WRL::ComPtr<IDXGISwapChain1> swapChain;
    Microsoft::WRL::ComPtr<ID2D1Factory1> d2dFactory;
    Microsoft::WRL::ComPtr<ID2D1Device> d2dDevice;
    Microsoft::WRL::ComPtr<ID2D1DeviceContext> d2dContext;
    Microsoft::WRL::ComPtr<ID2D1Bitmap1> targetBitmap;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush;
    Microsoft::WRL::ComPtr<IDWriteFactory> writeFactory;
    Microsoft::WRL::ComPtr<IDWriteFontCollection> interCollection;
    std::unordered_map<std::string, Microsoft::WRL::ComPtr<IDWriteFontCollection>> fileCollections;
    std::unordered_map<std::string, std::wstring> fileFamilies;
    std::unordered_map<std::string, Microsoft::WRL::ComPtr<IDWriteTextFormat>> formats;
    std::unordered_map<std::string, Microsoft::WRL::ComPtr<ID2D1Bitmap1>> imageCache;
    std::vector<float> opacityStack{1.0f};
    std::vector<D2D1_MATRIX_3X2_F> transformStack;
    // What kind of cut each push made, so a pop unwinds the one it belongs to: a box is cut by the
    // platform's own clip and a shape by a layer, and the two are undone by different calls.
    std::vector<bool> clipStack;
    // The layers shaped clips cut with, one per depth. A layer is kept rather than made and thrown
    // away, because a picture drawn every frame would otherwise build one every frame, and a layer
    // is reused by depth because a layer already in use cannot be pushed again.
    std::vector<Microsoft::WRL::ComPtr<ID2D1Layer>> layerPool;
    float dpi = 96.0f;
    int pixelWidth = 0;
    int pixelHeight = 0;
};

}  // namespace Runtime
