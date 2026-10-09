#pragma once

#include "window.h"

struct SDL_Window;

class SDL3Window: public Window
{
    SDL_Window *window = nullptr;
    bool should_close = false;
public:
    bool init(const WindowDesc &desc);
    void shutdown();
    bool get_should_close() const;
    void poll_event();
    void get_size(int &w, int &h) const;
    void get_frame_buffer_size(int &w, int &h) const;
    NativeWindowHandle get_native_handle() const;
};