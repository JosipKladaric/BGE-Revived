#pragma once

#include <cstdint>
#include <string>

#include "bge/math/math.h"
#include "bge/platform/input.h"

namespace bge {

struct WindowDesc {
    const char* title = "BGE Revived";
    int width = 1280;
    int height = 720;
    bool vsync = true;
    bool hidden = false;
    bool resizable = true;
    int gl_major = 4;
    int gl_minor = 6;
};

class Window {
public:
    Window() = default;
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    bool create(const WindowDesc& desc);
    void destroy();

    void poll_events();
    void swap_buffers();

    bool should_close() const { return should_close_; }
    void request_close() { should_close_ = true; }

    int width() const { return width_; }
    int height() const { return height_; }
    bool resized() const { return resized_; }
    void clear_resized() { resized_ = false; }

    void set_title(const char* title);
    void set_cursor_captured(bool captured);
    bool cursor_captured() const { return cursor_captured_; }

    // Resizes the window (and therefore the swap chain). Used by the preview
    // so the engine can render at the resolution the authoring tool asked for.
    bool resize(int width, int height);

    Input& input() { return input_; }
    const Input& input() const { return input_; }

    void* native_handle() const { return hwnd_; }
    void* device_context() const { return hdc_; }
    void* gl_context() const { return hglrc_; }

    double refresh_rate() const { return refresh_rate_; }

    // Platform message entry point (kept opaque so the header stays portable).
    intptr_t handle_message(void* hwnd, unsigned int msg, uintptr_t wparam, intptr_t lparam);

private:
    void* hwnd_ = nullptr;
    void* hdc_ = nullptr;
    void* hglrc_ = nullptr;
    Input input_{};
    bool should_close_ = false;
    bool cursor_captured_ = false;
    bool resized_ = false;
    bool cursor_visible_ = true;
    int width_ = 0;
    int height_ = 0;
    double refresh_rate_ = 60.0;
    std::string title_;

    friend struct WindowProc;
};

} // namespace bge
