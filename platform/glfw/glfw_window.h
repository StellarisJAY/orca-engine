#pragma once
#include "window.h"

struct GLFWwindow;

class GLFWWindow : public Window
{
    GLFWwindow *window = nullptr;
    WindowDesc desc;
    RenderingDriverType driver = RenderingDriverType::Vulkan;

    // 光标与鼠标键状态：GLFW 不直接提供相对位移和按键掩码，这里按事件增量维护。
    double last_cursor_x = 0.0;
    double last_cursor_y = 0.0;
    bool cursor_initialized = false;
    uint32_t mouse_button_mask = WINDOW_MOUSE_MASK_NONE;

public:
    GLFWWindow() = default;
    bool init(const WindowDesc &desc);
    void shutdown();
    bool get_should_close() const;
    RenderingWindowInfo get_rendering_window_info() const;
    void poll_event();
    void get_size(int &w, int &h) const;
    void get_frame_buffer_size(int &w, int &h) const;
    void swap_buffers();
    void set_mouse_mode(MouseMode mode);
private:
    // 以下回调由 init() 里注册的 GLFW 静态回调转发进来。
    void key_callback(int key, int sc, int act, int mods);
    void char_callback(unsigned int codepoint);
    void mouse_button_callback(int btn, int act, int mods);
    void cursor_pos_callback(double x, double y);
    void scroll_callback(double xoffset, double yoffset);
    void cursor_enter_callback(int entered);
    void window_focus_callback(int focused);
    void window_size_callback(int w, int h);
    void framebuffer_size_callback(int w, int h);
    void window_close_callback();
};
