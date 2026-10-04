#pragma once

#include "engine/math/Geometry.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <functional>
#include <string>
#include <string_view>

namespace Runtime {

enum class WindowCursor {
    ARROW,
    HAND,
    IBEAM,
    CROSS,
    SIZE_ALL,
    SIZE_NS,
    SIZE_WE,
    SIZE_NWSE,
    SIZE_NESW,
};

class Win32Window {
public:
    ~Win32Window();

    bool Create(const std::wstring& title, int dipWidth, int dipHeight);
    void Pump(bool& running);
    void WaitForMessage();

    HWND Hwnd() const;
    float Dpi() const;
    int PixelWidth() const;
    int PixelHeight() const;
    float ClientWidthDip() const;
    float ClientHeightDip() const;
    void SetHandCursor(bool enabled);
    void SetCursorShape(WindowCursor cursor);

    void SetResize(std::function<void(int pixelWidth, int pixelHeight)> callback);
    void SetMouseMove(std::function<void(Point point)> callback);
    void SetMouseDown(std::function<void(Point point)> callback);
    void SetMouseUp(std::function<void(Point point)> callback);
    void SetMouseLeave(std::function<void()> callback);
    void SetMiddleDown(std::function<void(Point point)> callback);
    void SetMiddleUp(std::function<void(Point point)> callback);
    void SetMouseWheel(std::function<void(Point point, int delta)> callback);
    void SetKeyDown(std::function<bool(unsigned key)> callback);
    void SetChar(std::function<void(wchar_t character)> callback);

private:
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);
    LRESULT Handle(UINT message, WPARAM wparam, LPARAM lparam);
    Point ToDip(LPARAM lparam) const;
    void RefreshClientSize();
    void ApplyCursor();

    HWND hwnd = nullptr;
    float dpi = 96.0f;
    int pixelWidth = 0;
    int pixelHeight = 0;
    bool handCursor = false;
    WindowCursor cursorShape = WindowCursor::ARROW;
    bool trackingMouse = false;
    std::function<void(int, int)> onResize;
    std::function<void(Point)> onMouseMove;
    std::function<void(Point)> onMouseDown;
    std::function<void(Point)> onMouseUp;
    std::function<void()> onMouseLeave;
    std::function<void(Point)> onMiddleDown;
    std::function<void(Point)> onMiddleUp;
    std::function<void(Point, int)> onMouseWheel;
    std::function<bool(unsigned)> onKeyDown;
    std::function<void(wchar_t)> onChar;
};

void EnablePerMonitorDpi();
std::wstring ToWide(std::string_view text);

}  // namespace Runtime
