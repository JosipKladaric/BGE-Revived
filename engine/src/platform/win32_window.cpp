#include "bge/platform/window.h"

#include "bge/core/log.h"

#include <algorithm>
#include <string>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <GL/gl.h>

namespace bge {
namespace {

constexpr wchar_t kWindowClass[] = L"BGERevivedWindow";
const LPCWSTR kArrowCursor = MAKEINTRESOURCEW(32512); // IDC_ARROW

// WGL extension constants (avoid pulling in glext.h).
constexpr int WGL_CONTEXT_MAJOR_VERSION_ARB = 0x2091;
constexpr int WGL_CONTEXT_MINOR_VERSION_ARB = 0x2092;
constexpr int WGL_CONTEXT_PROFILE_MASK_ARB = 0x9126;
constexpr int WGL_CONTEXT_CORE_PROFILE_BIT_ARB = 0x00000001;

using PFNWGLCREATECONTEXTATTRIBSARBPROC = HGLRC(WINAPI*)(HDC, HGLRC, const int*);
using PFNWGLSWAPINTERVALEXTPROC = BOOL(WINAPI*)(int);

Key map_vk(WPARAM vk) {
    if (vk >= 'A' && vk <= 'Z') return static_cast<Key>(static_cast<int>(Key::A) + static_cast<int>(vk - 'A'));
    if (vk >= '0' && vk <= '9') return static_cast<Key>(static_cast<int>(Key::Num0) + static_cast<int>(vk - '0'));
    switch (vk) {
        case VK_ESCAPE: return Key::Escape;
        case VK_SPACE: return Key::Space;
        case VK_LSHIFT: return Key::LeftShift;
        case VK_RSHIFT: return Key::RightShift;
        case VK_LCONTROL: return Key::LeftControl;
        case VK_RCONTROL: return Key::RightControl;
        case VK_LMENU: return Key::LeftAlt;
        case VK_RMENU: return Key::RightAlt;
        case VK_RETURN: return Key::Enter;
        case VK_TAB: return Key::Tab;
        case VK_BACK: return Key::Backspace;
        case VK_DELETE: return Key::Delete;
        case VK_INSERT: return Key::Insert;
        case VK_HOME: return Key::Home;
        case VK_END: return Key::End;
        case VK_PRIOR: return Key::PageUp;
        case VK_NEXT: return Key::PageDown;
        case VK_LEFT: return Key::Left;
        case VK_RIGHT: return Key::Right;
        case VK_UP: return Key::Up;
        case VK_DOWN: return Key::Down;
        case VK_F1: return Key::F1;
        case VK_F2: return Key::F2;
        case VK_F3: return Key::F3;
        case VK_F4: return Key::F4;
        case VK_F5: return Key::F5;
        case VK_F6: return Key::F6;
        case VK_F7: return Key::F7;
        case VK_F8: return Key::F8;
        case VK_F9: return Key::F9;
        case VK_F10: return Key::F10;
        case VK_F11: return Key::F11;
        case VK_F12: return Key::F12;
        default: return Key::Unknown;
    }
}

std::wstring widen(const char* text) {
    if (!text) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, text, -1, nullptr, 0);
    if (len <= 0) return {};
    std::wstring out(static_cast<size_t>(len), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text, -1, out.data(), len);
    out.resize(static_cast<size_t>(len - 1));
    return out;
}

} // namespace

struct WindowProc {
    static LRESULT CALLBACK thunk(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
        Window* self = reinterpret_cast<Window*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (msg == WM_NCCREATE) {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lparam);
            self = reinterpret_cast<Window*>(cs->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        if (!self) return DefWindowProcW(hwnd, msg, wparam, lparam);
        return static_cast<LRESULT>(self->handle_message(hwnd, msg, wparam, lparam));
    }
};

bool Window::create(const WindowDesc& desc) {
    if (hwnd_) destroy();
    title_ = desc.title ? desc.title : "BGE Revived";

    // Per-monitor V2 DPI awareness when available.
    using SetDpiContextFn = BOOL(WINAPI*)(HANDLE);
    if (auto* user32 = GetModuleHandleW(L"user32.dll")) {
        auto set_dpi = reinterpret_cast<SetDpiContextFn>(GetProcAddress(user32, "SetProcessDpiAwarenessContext"));
        if (set_dpi) set_dpi(reinterpret_cast<HANDLE>(static_cast<intptr_t>(-4)));
    }

    HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_OWNDC;
    wc.lpfnWndProc = WindowProc::thunk;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, kArrowCursor);
    wc.lpszClassName = kWindowClass;
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        BGE_ERROR("RegisterClassExW failed: %lu", GetLastError());
        return false;
    }

    DWORD style = desc.hidden ? WS_POPUP : WS_OVERLAPPEDWINDOW;
    if (!desc.hidden && !desc.resizable) style &= ~(WS_THICKFRAME | WS_MAXIMIZEBOX);
    RECT rect{0, 0, desc.width, desc.height};
    AdjustWindowRectEx(&rect, style, FALSE, 0);

    std::wstring wide_title = widen(desc.title);
    HWND hwnd = CreateWindowExW(0, kWindowClass, wide_title.c_str(), style,
                                CW_USEDEFAULT, CW_USEDEFAULT,
                                rect.right - rect.left, rect.bottom - rect.top,
                                nullptr, nullptr, instance, this);
    if (!hwnd) {
        BGE_ERROR("CreateWindowExW failed: %lu", GetLastError());
        return false;
    }
    hwnd_ = hwnd;

    RECT client{};
    GetClientRect(hwnd, &client);
    width_ = std::max(1L, client.right - client.left);
    height_ = std::max(1L, client.bottom - client.top);

    HDC hdc = GetDC(hwnd);
    if (!hdc) {
        BGE_ERROR("GetDC failed");
        destroy();
        return false;
    }
    hdc_ = hdc;

    PIXELFORMATDESCRIPTOR pfd{};
    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cDepthBits = 24;
    pfd.cStencilBits = 8;
    pfd.iLayerType = PFD_MAIN_PLANE;

    int pf = ChoosePixelFormat(hdc, &pfd);
    if (!pf || !SetPixelFormat(hdc, pf, &pfd)) {
        BGE_ERROR("Failed to set pixel format: %lu", GetLastError());
        destroy();
        return false;
    }

    HGLRC temp = wglCreateContext(hdc);
    if (!temp || !wglMakeCurrent(hdc, temp)) {
        BGE_ERROR("Failed to create legacy OpenGL context");
        destroy();
        return false;
    }

    auto create_context_attribs = reinterpret_cast<PFNWGLCREATECONTEXTATTRIBSARBPROC>(wglGetProcAddress("wglCreateContextAttribsARB"));
    HGLRC core = nullptr;
    if (create_context_attribs) {
        int versions[][2] = {{desc.gl_major, desc.gl_minor}, {4, 5}, {4, 3}};
        for (auto& v : versions) {
            int attribs[] = {
                WGL_CONTEXT_MAJOR_VERSION_ARB, v[0],
                WGL_CONTEXT_MINOR_VERSION_ARB, v[1],
                WGL_CONTEXT_PROFILE_MASK_ARB, WGL_CONTEXT_CORE_PROFILE_BIT_ARB,
                0
            };
            core = create_context_attribs(hdc, nullptr, attribs);
            if (core) break;
        }
    }

    wglMakeCurrent(nullptr, nullptr);
    wglDeleteContext(temp);

    if (!core) {
        BGE_ERROR("Failed to create an OpenGL %d.%d core context", desc.gl_major, desc.gl_minor);
        destroy();
        return false;
    }

    if (!wglMakeCurrent(hdc, core)) {
        BGE_ERROR("wglMakeCurrent failed");
        wglDeleteContext(core);
        destroy();
        return false;
    }
    hglrc_ = core;

    auto swap_interval = reinterpret_cast<PFNWGLSWAPINTERVALEXTPROC>(wglGetProcAddress("wglSwapIntervalEXT"));
    if (swap_interval) swap_interval(desc.vsync ? 1 : 0);

    HDC screen = GetDC(nullptr);
    if (screen) {
        int rate = GetDeviceCaps(screen, VREFRESH);
        if (rate > 1) refresh_rate_ = static_cast<double>(rate);
        ReleaseDC(nullptr, screen);
    }

    const char* version = reinterpret_cast<const char*>(glGetString(GL_VERSION));
    const char* renderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    BGE_INFO("OpenGL context: %s", version ? version : "unknown");
    BGE_INFO("Renderer: %s", renderer ? renderer : "unknown");

    if (!desc.hidden) {
        ShowWindow(hwnd, SW_SHOW);
        UpdateWindow(hwnd);
    }
    BGE_INFO("Window created: %dx%d (%.0f Hz)", width_, height_, refresh_rate_);
    return true;
}

void Window::destroy() {
    if (cursor_captured_) set_cursor_captured(false);
    if (hglrc_) {
        wglMakeCurrent(nullptr, nullptr);
        wglDeleteContext(static_cast<HGLRC>(hglrc_));
        hglrc_ = nullptr;
    }
    if (hdc_ && hwnd_) {
        ReleaseDC(static_cast<HWND>(hwnd_), static_cast<HDC>(hdc_));
        hdc_ = nullptr;
    }
    if (hwnd_) {
        DestroyWindow(static_cast<HWND>(hwnd_));
        hwnd_ = nullptr;
    }
}

Window::~Window() { destroy(); }

void Window::poll_events() {
    input_.begin_frame();
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) {
            should_close_ = true;
            continue;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (cursor_captured_ && hwnd_) {
        POINT center{width_ / 2, height_ / 2};
        ClientToScreen(static_cast<HWND>(hwnd_), &center);
        POINT p{};
        if (GetCursorPos(&p)) {
            input_.add_mouse_delta(static_cast<float>(p.x - center.x), static_cast<float>(p.y - center.y));
        }
        SetCursorPos(center.x, center.y);
    }
}

void Window::swap_buffers() {
    if (hdc_) SwapBuffers(static_cast<HDC>(hdc_));
}

void Window::set_title(const char* title) {
    title_ = title ? title : "";
    if (hwnd_) SetWindowTextW(static_cast<HWND>(hwnd_), widen(title_.c_str()).c_str());
}

bool Window::resize(int width, int height) {
    if (!hwnd_ || width <= 0 || height <= 0) return false;
    RECT rect{0, 0, width, height};
    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
    if (!SetWindowPos(static_cast<HWND>(hwnd_), nullptr, 0, 0, rect.right - rect.left,
                      rect.bottom - rect.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE)) {
        return false;
    }
    // The WM_SIZE message updates the size, but a hidden window may not receive
    // it immediately, so keep the state in sync here.
    width_ = width;
    height_ = height;
    resized_ = true;
    return true;
}

void Window::set_cursor_captured(bool captured) {
    if (!hwnd_ || captured == cursor_captured_) return;
    cursor_captured_ = captured;
    if (captured) {
        SetCapture(static_cast<HWND>(hwnd_));
        if (cursor_visible_) {
            ShowCursor(FALSE);
            cursor_visible_ = false;
        }
        POINT center{width_ / 2, height_ / 2};
        ClientToScreen(static_cast<HWND>(hwnd_), &center);
        SetCursorPos(center.x, center.y);
        input_.begin_frame();
    } else {
        ReleaseCapture();
        if (!cursor_visible_) {
            ShowCursor(TRUE);
            cursor_visible_ = true;
        }
        SetCursor(LoadCursorW(nullptr, kArrowCursor));
    }
}

intptr_t Window::handle_message(void* hwnd_ptr, unsigned int msg, uintptr_t wparam, intptr_t lparam) {
    HWND hwnd = static_cast<HWND>(hwnd_ptr);
    switch (msg) {
        case WM_CLOSE:
            should_close_ = true;
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        case WM_SIZE:
            if (wparam != SIZE_MINIMIZED) {
                int w = static_cast<int>(LOWORD(lparam));
                int h = static_cast<int>(HIWORD(lparam));
                if (w > 0 && h > 0) {
                    width_ = w;
                    height_ = h;
                    resized_ = true;
                }
            }
            return 0;
        case WM_SYSKEYDOWN:
        case WM_KEYDOWN: {
            Key key = map_vk(wparam);
            if (key != Key::Unknown) input_.set_key(static_cast<int>(key), true);
            if (msg == WM_SYSKEYDOWN) return DefWindowProcW(hwnd, msg, wparam, lparam);
            return 0;
        }
        case WM_SYSKEYUP:
        case WM_KEYUP: {
            Key key = map_vk(wparam);
            if (key != Key::Unknown) input_.set_key(static_cast<int>(key), false);
            if (msg == WM_SYSKEYUP) return DefWindowProcW(hwnd, msg, wparam, lparam);
            return 0;
        }
        case WM_LBUTTONDOWN: input_.set_mouse_button(0, true); SetCapture(hwnd); return 0;
        case WM_LBUTTONUP: input_.set_mouse_button(0, false); if (!cursor_captured_) ReleaseCapture(); return 0;
        case WM_RBUTTONDOWN: input_.set_mouse_button(1, true); SetCapture(hwnd); return 0;
        case WM_RBUTTONUP: input_.set_mouse_button(1, false); if (!cursor_captured_) ReleaseCapture(); return 0;
        case WM_MBUTTONDOWN: input_.set_mouse_button(2, true); SetCapture(hwnd); return 0;
        case WM_MBUTTONUP: input_.set_mouse_button(2, false); if (!cursor_captured_) ReleaseCapture(); return 0;
        case WM_XBUTTONDOWN: input_.set_mouse_button(GET_XBUTTON_WPARAM(wparam) == XBUTTON1 ? 3 : 4, true); return 0;
        case WM_XBUTTONUP: input_.set_mouse_button(GET_XBUTTON_WPARAM(wparam) == XBUTTON1 ? 3 : 4, false); return 0;
        case WM_MOUSEWHEEL:
            input_.add_scroll(static_cast<float>(GET_WHEEL_DELTA_WPARAM(wparam)) / static_cast<float>(WHEEL_DELTA));
            return 0;
        case WM_MOUSEMOVE: {
            int x = static_cast<short>(LOWORD(lparam));
            int y = static_cast<short>(HIWORD(lparam));
            input_.set_mouse_position(static_cast<float>(x), static_cast<float>(y));
            return 0;
        }
        case WM_KILLFOCUS:
            input_.clear_keys();
            if (cursor_captured_) set_cursor_captured(false);
            return 0;
        case WM_SETCURSOR:
            if (cursor_captured_) {
                SetCursor(nullptr);
                return TRUE;
            }
            break;
        case WM_DPICHANGED: {
            RECT* suggested = reinterpret_cast<RECT*>(lparam);
            SetWindowPos(hwnd, nullptr, suggested->left, suggested->top,
                         suggested->right - suggested->left, suggested->bottom - suggested->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            return 0;
        }
        case WM_GETMINMAXINFO: {
            auto* info = reinterpret_cast<MINMAXINFO*>(lparam);
            info->ptMinTrackSize.x = 320;
            info->ptMinTrackSize.y = 180;
            return 0;
        }
        default:
            break;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

} // namespace bge
