#include "glfw_window.h"
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

bool GLFWWindow::init(const WindowDesc &desc)
{
    if(glfwInit() == GLFW_FALSE) return false;
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, desc.resizable ? GLFW_TRUE : GLFW_FALSE);
    glfwWindowHint(GLFW_VISIBLE, desc.visible ? GLFW_TRUE : GLFW_FALSE);

    GLFWwindow *window = nullptr;
    if(desc.fullscreen)
    {
        auto p_monitor = glfwGetPrimaryMonitor();
        auto video_mode = glfwGetVideoMode(p_monitor);
        window = glfwCreateWindow(video_mode->width, video_mode->height, desc.title, p_monitor, nullptr);
    } else {
        window = glfwCreateWindow(desc.width, desc.height, desc.title, nullptr, nullptr);
    }
    
    if(window == nullptr) return false;
    
    glfwSetWindowUserPointer(window, this);

    glfwSetKeyCallback(window, [](GLFWwindow* w, int key, int sc, int act, int mods) {
        auto window = static_cast<GLFWWindow*>(glfwGetWindowUserPointer(w));
        window->key_callback(key, sc, act, mods);
    });

    glfwSetMouseButtonCallback(window, [](GLFWwindow *w, int btn, int act, int mods) {
        auto window = static_cast<GLFWWindow*>(glfwGetWindowUserPointer(w));
        window->mouse_button_callback(btn, act, mods);
    });

    glfwSetCursorPosCallback(window, [](GLFWwindow *w, double x, double y)
    {
        auto window = static_cast<GLFWWindow*>(glfwGetWindowUserPointer(w));
        window->cursor_pos_callaback(x, y);
    });

    glfwSetWindowCloseCallback(window, [](GLFWwindow *w){
        auto window = static_cast<GLFWWindow*>(glfwGetWindowUserPointer(w));
        window->window_close_callback();
    });

    glfwSetWindowSizeCallback(window, [](GLFWwindow *w, int width, int height){
        auto window = static_cast<GLFWWindow*>(glfwGetWindowUserPointer(w));
        window->window_size_callback(width, height);
    });

    glfwSetFramebufferSizeCallback(window, [](GLFWwindow *w, int width, int height){
        auto window = static_cast<GLFWWindow*>(glfwGetWindowUserPointer(w));
        window->framebuffer_size_callback(width, height);
    });

    this->window = window;
    this->desc = desc;
    return true;
}

void GLFWWindow::shutdown()
{
    if(window == nullptr) return;
    glfwDestroyWindow(window);
    glfwTerminate();
}

bool GLFWWindow::get_should_close() const
{
    if(window == nullptr) return false;
    return glfwWindowShouldClose(window);
}

void GLFWWindow::poll_event()
{
    glfwPollEvents();
}

void GLFWWindow::get_size(int &w, int &h) const
{
    glfwGetWindowSize(window, &w, &h);
}

void GLFWWindow::get_frame_buffer_size(int &w, int &h) const
{
    glfwGetFramebufferSize(window, &w, &h);
}

NativeWindowHandle GLFWWindow::get_native_handle() const
{
    NativeWindowHandle nativeHandle;
    nativeHandle.window = window;
    return nativeHandle;
}

void GLFWWindow::key_callback(int key, int sc, int act, int mods) const
{
    WindowEvent event;
    event.type = WindowEventType::Key;
    emit(event);
}

void GLFWWindow::mouse_button_callback(int btn, int act, int mods) const
{
    WindowEvent event;
    event.type = WindowEventType::MouseButton;
    emit(event);
}

void GLFWWindow::cursor_pos_callaback(double x, double y) const
{
    WindowEvent event;
    event.type = WindowEventType::Key;
    emit(event);
}

void GLFWWindow::window_size_callback(int w, int h) const
{
    WindowEvent event;
    event.type = WindowEventType::WindowSize;
    event.w = w; event.h = h;
    emit(event);
}

void GLFWWindow::framebuffer_size_callback(int w, int h) const
{
    WindowEvent event;
    event.type = WindowEventType::FramebufferSize;
    event.w = w; event.h = h;
    emit(event);
}

void GLFWWindow::window_close_callback() const
{
    WindowEvent event;
    event.type = WindowEventType::Close;
    emit(event);
}

std::unique_ptr<Window> Window::create()
{
    GLFWWindow window;
    return std::make_unique<GLFWWindow>(std::move(window));
}
