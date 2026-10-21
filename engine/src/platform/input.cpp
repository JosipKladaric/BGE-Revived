#include "bge/platform/input.h"

namespace bge {

void Input::begin_frame() {
    keys_pressed_.fill(false);
    keys_released_.fill(false);
    mouse_pressed_.fill(false);
    mouse_released_.fill(false);
    mouse_delta_ = {};
    scroll_delta_ = 0.0f;
}

void Input::set_key(int vk, bool down) {
    // vk is already mapped to Key by the platform layer.
    if (vk <= 0 || vk >= static_cast<int>(Key::Count)) return;
    size_t i = static_cast<size_t>(vk);
    if (down) {
        if (!keys_down_[i]) keys_pressed_[i] = true;
        keys_down_[i] = true;
    } else {
        if (keys_down_[i]) keys_released_[i] = true;
        keys_down_[i] = false;
    }
}

void Input::set_mouse_button(int button, bool down) {
    if (button < 0 || button >= 5) return;
    if (down) {
        if (!mouse_down_[button]) mouse_pressed_[button] = true;
        mouse_down_[button] = true;
    } else {
        if (mouse_down_[button]) mouse_released_[button] = true;
        mouse_down_[button] = false;
    }
}

void Input::clear_keys() {
    keys_down_.fill(false);
    keys_pressed_.fill(false);
    keys_released_.fill(false);
    mouse_down_.fill(false);
    mouse_pressed_.fill(false);
    mouse_released_.fill(false);
    mouse_delta_ = {};
}

} // namespace bge
