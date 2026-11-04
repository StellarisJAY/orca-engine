#pragma once

#include "window.h"

struct SDL_Window;
struct SDL_GLContextState;

class SDL3Window : public Window
{
    SDL_Window *window = nullptr;
    SDL_GLContextState *gl_context = nullptr;
    bool should_close = false;
    bool sdl_initialized = false;
    RenderingDriverType driver = RenderingDriverType::Vulkan;

    // SDL3 的按键/按钮事件里不保证都带按住掩码，这里和 GLFW 后端一样自行维护。
    uint32_t mouse_button_mask = WINDOW_MOUSE_MASK_NONE;

public:
    bool init(const WindowDesc &desc);
    void shutdown();
    bool get_should_close() const;
    RenderingWindowInfo get_rendering_window_info() const;
    void poll_event();
    void get_size(int &w, int &h) const;
    void get_frame_buffer_size(int &w, int &h) const;
    void swap_buffers();
    void set_mouse_mode(MouseMode mode);
};
