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
#include <dwrite.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

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
    Size MeasureText(std::string_view text, float fontSize, float maxWidth) override;
    void PushClip(const Rect& rect) override;
    void PopClip() override;
    void PushOpacity(float opacity) override;
    void PopOpacity() override;

private:
    bool CreateDevice();
    bool CreateSwapChain(HWND window, int width, int height);
    bool CreateTarget();
    IDWriteTextFormat* FormatFor(float fontSize, int fontWeight);

    Microsoft::WRL::ComPtr<ID3D11Device> device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> deviceContext;
    Microsoft::WRL::ComPtr<IDXGISwapChain1> swapChain;
    Microsoft::WRL::ComPtr<ID2D1Factory1> d2dFactory;
    Microsoft::WRL::ComPtr<ID2D1Device> d2dDevice;
    Microsoft::WRL::ComPtr<ID2D1DeviceContext> d2dContext;
    Microsoft::WRL::ComPtr<ID2D1Bitmap1> targetBitmap;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush;
    Microsoft::WRL::ComPtr<IDWriteFactory> writeFactory;
    std::unordered_map<int, Microsoft::WRL::ComPtr<IDWriteTextFormat>> formats;
    std::vector<float> opacityStack{1.0f};
    float dpi = 96.0f;
    int pixelWidth = 0;
    int pixelHeight = 0;
};

}  // namespace Runtime
