#pragma once

#include "window.h"

struct SDL_Window;
struct SDL_GLContextState;

class SDL3Window : public Window
{
    SDL_Window *window = nullptr;
    SDL_GLContextState *gl_context = nullptr;
    bool should_close = false;
    RenderingDriverType driver = RenderingDriverType::Vulkan;

public:
    bool init(const WindowDesc &desc);
    void shutdown();
    bool get_should_close() const;
    RenderingWindowInfo get_rendering_window_info() const;
    void poll_event();
    void get_size(int &w, int &h) const;
    void get_frame_buffer_size(int &w, int &h) const;
    void swap_buffers();
};
