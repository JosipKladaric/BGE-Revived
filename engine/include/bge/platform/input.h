#pragma once

#include <array>
#include <cstdint>

#include "bge/math/math.h"

namespace bge {

enum class Key : uint16_t {
    Unknown = 0,
    A, B, C, D, E, F, G, H, I, J, K, L, M,
    N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
    Num0, Num1, Num2, Num3, Num4, Num5, Num6, Num7, Num8, Num9,
    Escape, Space, LeftShift, RightShift, LeftControl, RightControl,
    LeftAlt, RightAlt, Enter, Tab, Backspace, Delete, Insert,
    Home, End, PageUp, PageDown,
    Left, Right, Up, Down,
    F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
    Count
};

class Input {
public:
    void begin_frame();

    bool key_down(Key key) const { return keys_down_[index(key)]; }
    bool key_pressed(Key key) const { return keys_pressed_[index(key)]; }
    bool key_released(Key key) const { return keys_released_[index(key)]; }

    bool mouse_down(int button) const { return button >= 0 && button < 5 && mouse_down_[button]; }
    bool mouse_pressed(int button) const { return button >= 0 && button < 5 && mouse_pressed_[button]; }
    bool mouse_released(int button) const { return button >= 0 && button < 5 && mouse_released_[button]; }

    Vec2 mouse_position() const { return mouse_position_; }
    Vec2 mouse_delta() const { return mouse_delta_; }
    float scroll_delta() const { return scroll_delta_; }

    // Platform layer writes these.
    void set_key(int vk, bool down);
    void set_mouse_button(int button, bool down);
    void add_mouse_delta(float dx, float dy) { mouse_delta_ += Vec2{dx, dy}; }
    void add_scroll(float amount) { scroll_delta_ += amount; }
    void set_mouse_position(float x, float y) { mouse_position_ = {x, y}; }
    void clear_keys();

private:
    static constexpr size_t count = static_cast<size_t>(Key::Count);
    static constexpr size_t index(Key key) { return static_cast<size_t>(key); }

    std::array<bool, count> keys_down_{};
    std::array<bool, count> keys_pressed_{};
    std::array<bool, count> keys_released_{};
    std::array<bool, 5> mouse_down_{};
    std::array<bool, 5> mouse_pressed_{};
    std::array<bool, 5> mouse_released_{};
    Vec2 mouse_position_{};
    Vec2 mouse_delta_{};
    float scroll_delta_ = 0.0f;
};

} // namespace bge
