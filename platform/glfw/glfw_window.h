#pragma once
#include "window.h"

struct GLFWwindow;

class GLFWWindow: public Window
{
    GLFWwindow *window = nullptr;
    WindowDesc desc;
public:
    GLFWWindow() = default;
    bool init(const WindowDesc &desc);
    void shutdown();
    bool get_should_close() const;
    void poll_event();
    void get_size(int &w, int &h) const;
    void get_frame_buffer_size(int &w, int &h) const;
    NativeWindowHandle get_native_handle() const;
private:
    void key_callback(int key, int sc, int act, int mods) const;
    void mouse_button_callback(int btn, int act, int mods) const;
    void cursor_pos_callaback(double x, double y) const;
    void window_size_callback(int w, int h) const;
    void framebuffer_size_callback(int w, int h) const;
    void window_close_callback() const;
};