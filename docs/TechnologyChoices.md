# Technology choices

| Choice | Decision |
| --- | --- |
| Language | C++20 |
| Build | CMake, Windows only |
| Compiler | MSVC 2022 |
| Windowing | Win32 |
| Graphics | Direct3D 11 device, DXGI flip swap chain |
| 2D drawing | Direct2D 1.1 on that device |
| Text | DirectWrite, Segoe UI |
| COM | `Microsoft::WRL::ComPtr` |
| XML parser | Hand-written lexer and parser |
| Tests | Small in-tree harness, CTest |
| Dependencies | Windows SDK only |

Direct2D records 2D draws against the Direct3D 11 device and presents through DXGI. UI code calls `Renderer`. Replacing the backend does not require rewriting `Button` or `TextBlock`.

The device is created with `D3D11_CREATE_DEVICE_BGRA_SUPPORT`. Hardware is preferred. If that fails, the renderer retries without the debug layer and then uses WARP. Debug builds try the D3D11 debug layer first and fall back when it is not installed.

DPI awareness is per-monitor v2. Layout units are DIPs. The swap chain is sized in physical pixels.

No third-party XML, UI, or math libraries are linked. The parser is small on purpose so diagnostics can point at the attribute value.

Shadows and gradients are not part of the renderer. Buttons change solid fill color for hover and pressed.

The process is single-threaded for the UI and the device. Handlers run on that thread.
