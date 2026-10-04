#include "platform/windows/Win32Window.h"

#include <windowsx.h>

namespace Runtime {
namespace {

constexpr wchar_t WINDOW_CLASS_NAME[] = L"RuntimeWindow";

}  // namespace

void EnablePerMonitorDpi() {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
}

std::wstring ToWide(std::string_view text) {
    if (text.empty()) {
        return {};
    }
    if (text.size() > static_cast<std::size_t>(INT_MAX)) {
        return {};
    }
    const int sourceLength = static_cast<int>(text.size());
    const int wideLength = MultiByteToWideChar(CP_UTF8, 0, text.data(), sourceLength, nullptr, 0);
    if (wideLength <= 0) {
        return {};
    }
    std::wstring wide(static_cast<std::size_t>(wideLength), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), sourceLength, wide.data(), wideLength);
    return wide;
}

Win32Window::~Win32Window() {
    if (hwnd != nullptr) {
        DestroyWindow(hwnd);
        hwnd = nullptr;
    }
}

bool Win32Window::Create(const std::wstring& title, int dipWidth, int dipHeight) {
    EnablePerMonitorDpi();
    HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = &Win32Window::WindowProc;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.lpszClassName = WINDOW_CLASS_NAME;
    if (RegisterClassExW(&windowClass) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return false;
    }

    dpi = static_cast<float>(GetDpiForSystem());
    if (dpi < 1.0f) {
        dpi = 96.0f;
    }
    const float scale = dpi / 96.0f;
    RECT rect{0, 0, static_cast<LONG>(static_cast<float>(dipWidth) * scale),
              static_cast<LONG>(static_cast<float>(dipHeight) * scale)};
    const DWORD style = WS_OVERLAPPEDWINDOW;
    AdjustWindowRectExForDpi(&rect, style, FALSE, 0, static_cast<UINT>(dpi));
    hwnd = CreateWindowExW(0, WINDOW_CLASS_NAME, title.c_str(), style, CW_USEDEFAULT, CW_USEDEFAULT,
                           rect.right - rect.left, rect.bottom - rect.top, nullptr, nullptr, instance, this);
    if (hwnd == nullptr) {
        return false;
    }
    dpi = static_cast<float>(GetDpiForWindow(hwnd));
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
    RefreshClientSize();
    return true;
}

void Win32Window::Pump(bool& running) {
    MSG message;
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        if (message.message == WM_QUIT) {
            running = false;
            return;
        }
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}

void Win32Window::WaitForMessage() {
    WaitMessage();
}

HWND Win32Window::Hwnd() const {
    return hwnd;
}

float Win32Window::Dpi() const {
    return dpi;
}

int Win32Window::PixelWidth() const {
    return pixelWidth;
}

int Win32Window::PixelHeight() const {
    return pixelHeight;
}

float Win32Window::ClientWidthDip() const {
    const float scale = dpi / 96.0f;
    return scale > 0.0f ? static_cast<float>(pixelWidth) / scale : static_cast<float>(pixelWidth);
}

float Win32Window::ClientHeightDip() const {
    const float scale = dpi / 96.0f;
    return scale > 0.0f ? static_cast<float>(pixelHeight) / scale : static_cast<float>(pixelHeight);
}

void Win32Window::SetHandCursor(bool enabled) {
    handCursor = enabled;
}

void Win32Window::SetCursorShape(WindowCursor cursor) {
    cursorShape = cursor;
    handCursor = false;
    ApplyCursor();
}

void Win32Window::ApplyCursor() {
    const wchar_t* cursor = IDC_ARROW;
    if (handCursor) {
        cursor = IDC_HAND;
    } else {
        switch (cursorShape) {
        case WindowCursor::HAND:
            cursor = IDC_HAND;
            break;
        case WindowCursor::IBEAM:
            cursor = IDC_IBEAM;
            break;
        case WindowCursor::CROSS:
            cursor = IDC_CROSS;
            break;
        case WindowCursor::SIZE_ALL:
            cursor = IDC_SIZEALL;
            break;
        case WindowCursor::SIZE_NS:
            cursor = IDC_SIZENS;
            break;
        case WindowCursor::SIZE_WE:
            cursor = IDC_SIZEWE;
            break;
        case WindowCursor::SIZE_NWSE:
            cursor = IDC_SIZENWSE;
            break;
        case WindowCursor::SIZE_NESW:
            cursor = IDC_SIZENESW;
            break;
        case WindowCursor::ARROW:
            cursor = IDC_ARROW;
            break;
        }
    }
    SetCursor(LoadCursorW(nullptr, cursor));
}

void Win32Window::SetResize(std::function<void(int, int)> callback) {
    onResize = std::move(callback);
}

void Win32Window::SetMouseMove(std::function<void(Point)> callback) {
    onMouseMove = std::move(callback);
}

void Win32Window::SetMouseDown(std::function<void(Point)> callback) {
    onMouseDown = std::move(callback);
}

void Win32Window::SetMouseUp(std::function<void(Point)> callback) {
    onMouseUp = std::move(callback);
}

void Win32Window::SetMouseLeave(std::function<void()> callback) {
    onMouseLeave = std::move(callback);
}

void Win32Window::SetMiddleDown(std::function<void(Point)> callback) {
    onMiddleDown = std::move(callback);
}

void Win32Window::SetMiddleUp(std::function<void(Point)> callback) {
    onMiddleUp = std::move(callback);
}

void Win32Window::SetMouseWheel(std::function<void(Point, int)> callback) {
    onMouseWheel = std::move(callback);
}

void Win32Window::SetKeyDown(std::function<bool(unsigned)> callback) {
    onKeyDown = std::move(callback);
}

void Win32Window::SetChar(std::function<void(wchar_t)> callback) {
    onChar = std::move(callback);
}

Point Win32Window::ToDip(LPARAM lparam) const {
    const float scale = dpi / 96.0f;
    const float divider = scale > 0.0f ? scale : 1.0f;
    return Point{static_cast<float>(GET_X_LPARAM(lparam)) / divider, static_cast<float>(GET_Y_LPARAM(lparam)) / divider};
}

void Win32Window::RefreshClientSize() {
    RECT client{};
    if (hwnd != nullptr && GetClientRect(hwnd, &client)) {
        pixelWidth = client.right - client.left;
        pixelHeight = client.bottom - client.top;
    }
    if (hwnd != nullptr) {
        dpi = static_cast<float>(GetDpiForWindow(hwnd));
    }
}

LRESULT CALLBACK Win32Window::WindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
        auto* self = static_cast<Win32Window*>(create->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->hwnd = window;
        return TRUE;
    }
    auto* self = reinterpret_cast<Win32Window*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (self == nullptr) {
        return DefWindowProcW(window, message, wparam, lparam);
    }
    return self->Handle(message, wparam, lparam);
}

LRESULT Win32Window::Handle(UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case WM_SIZE:
        RefreshClientSize();
        if (onResize) {
            onResize(pixelWidth, pixelHeight);
        }
        return 0;
    case WM_DPICHANGED:
        dpi = static_cast<float>(HIWORD(wparam));
        if (auto* suggested = reinterpret_cast<RECT*>(lparam)) {
            SetWindowPos(hwnd, nullptr, suggested->left, suggested->top, suggested->right - suggested->left,
                         suggested->bottom - suggested->top, SWP_NOZORDER | SWP_NOACTIVATE);
        }
        return 0;
    case WM_MOUSEMOVE: {
        if (!trackingMouse) {
            TRACKMOUSEEVENT track{};
            track.cbSize = sizeof(track);
            track.dwFlags = TME_LEAVE;
            track.hwndTrack = hwnd;
            TrackMouseEvent(&track);
            trackingMouse = true;
        }
        if (onMouseMove) {
            onMouseMove(ToDip(lparam));
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        trackingMouse = false;
        if (onMouseLeave) {
            onMouseLeave();
        }
        return 0;
    case WM_LBUTTONDOWN:
        SetCapture(hwnd);
        if (onMouseDown) {
            onMouseDown(ToDip(lparam));
        }
        return 0;
    case WM_LBUTTONUP:
        if (hwnd == GetCapture()) {
            ReleaseCapture();
        }
        if (onMouseUp) {
            onMouseUp(ToDip(lparam));
        }
        return 0;
    case WM_MBUTTONDOWN:
        SetCapture(hwnd);
        if (onMiddleDown) {
            onMiddleDown(ToDip(lparam));
        }
        return 0;
    case WM_MBUTTONUP:
        if (hwnd == GetCapture()) {
            ReleaseCapture();
        }
        if (onMiddleUp) {
            onMiddleUp(ToDip(lparam));
        }
        return 0;
    case WM_MOUSEWHEEL: {
        POINT cursor{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
        ScreenToClient(hwnd, &cursor);
        const float scale = dpi / 96.0f;
        const float divider = scale > 0.0f ? scale : 1.0f;
        if (onMouseWheel) {
            onMouseWheel(Point{static_cast<float>(cursor.x) / divider, static_cast<float>(cursor.y) / divider},
                         GET_WHEEL_DELTA_WPARAM(wparam));
        }
        return 0;
    }
    case WM_SETCURSOR:
        if (LOWORD(lparam) == HTCLIENT) {
            ApplyCursor();
            return TRUE;
        }
        break;
    case WM_KEYDOWN:
        if (onKeyDown && onKeyDown(static_cast<unsigned>(wparam))) {
            return 0;
        }
        if (wparam == VK_ESCAPE) {
            DestroyWindow(hwnd);
            return 0;
        }
        break;
    case WM_CHAR:
        if (onChar && wparam >= 32) {
            onChar(static_cast<wchar_t>(wparam));
            return 0;
        }
        break;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        BeginPaint(hwnd, &paint);
        EndPaint(hwnd, &paint);
        return 0;
    }
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        hwnd = nullptr;
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

}  // namespace Runtime
