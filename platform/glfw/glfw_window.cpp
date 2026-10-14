#include "glfw_window.h"

#ifdef ORCA_RENDERER_VULKAN
#include <vulkan/vulkan_core.h> // 必须在 glfw3.h 之前：glfwCreateWindowSurface 由 VK_VERSION_1_0 守卫
#endif
#include <GLFW/glfw3.h>
// 暴露 Win32 原生句柄（HWND）与 WGL 上下文（HGLRC），让渲染后端可以自行创建 surface/上下文。
#define GLFW_EXPOSE_NATIVE_WIN32
#define GLFW_EXPOSE_NATIVE_WGL
#include <GLFW/glfw3native.h>

#include <cstdio>

bool GLFWWindow::init(const WindowDesc &desc)
{
    if (glfwInit() == GLFW_FALSE)
        return false;

    driver = desc.driver;

    // 按目标后端设置窗口创建提示。
    if (driver == RenderingDriverType::OpenGL)
    {
        glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_API);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    }
    else
    {
        // Vulkan：窗口本身不创建任何图形上下文，surface 由渲染后端创建。
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    }
    glfwWindowHint(GLFW_RESIZABLE, desc.resizable ? GLFW_TRUE : GLFW_FALSE);
    glfwWindowHint(GLFW_VISIBLE, desc.visible ? GLFW_TRUE : GLFW_FALSE);

    GLFWwindow *window = nullptr;
    if (desc.fullscreen)
    {
        auto p_monitor = glfwGetPrimaryMonitor();
        auto video_mode = glfwGetVideoMode(p_monitor);
        window = glfwCreateWindow(video_mode->width, video_mode->height, desc.title, p_monitor, nullptr);
    }
    else
    {
        window = glfwCreateWindow(desc.width, desc.height, desc.title, nullptr, nullptr);
    }

    if (window == nullptr)
    {
        glfwTerminate();
        return false;
    }

    glfwSetWindowUserPointer(window, this);

    glfwSetKeyCallback(window, [](GLFWwindow *w, int key, int sc, int act, int mods) {
        auto window = static_cast<GLFWWindow *>(glfwGetWindowUserPointer(w));
        window->key_callback(key, sc, act, mods);
    });

    glfwSetMouseButtonCallback(window, [](GLFWwindow *w, int btn, int act, int mods) {
        auto window = static_cast<GLFWWindow *>(glfwGetWindowUserPointer(w));
        window->mouse_button_callback(btn, act, mods);
    });

    glfwSetCursorPosCallback(window, [](GLFWwindow *w, double x, double y) {
        auto window = static_cast<GLFWWindow *>(glfwGetWindowUserPointer(w));
        window->cursor_pos_callaback(x, y);
    });

    glfwSetWindowCloseCallback(window, [](GLFWwindow *w) {
        auto window = static_cast<GLFWWindow *>(glfwGetWindowUserPointer(w));
        window->window_close_callback();
    });

    glfwSetWindowSizeCallback(window, [](GLFWwindow *w, int width, int height) {
        auto window = static_cast<GLFWWindow *>(glfwGetWindowUserPointer(w));
        window->window_size_callback(width, height);
    });

    glfwSetFramebufferSizeCallback(window, [](GLFWwindow *w, int width, int height) {
        auto window = static_cast<GLFWWindow *>(glfwGetWindowUserPointer(w));
        window->framebuffer_size_callback(width, height);
    });

    this->window = window;
    this->desc = desc;

    if (driver == RenderingDriverType::OpenGL)
    {
        glfwMakeContextCurrent(window);
        glfwSwapInterval(1);
    }
    return true;
}

void GLFWWindow::shutdown()
{
    if (window == nullptr)
        return;
    glfwDestroyWindow(window);
    window = nullptr;
    glfwTerminate();
}

bool GLFWWindow::get_should_close() const
{
    if (window == nullptr)
        return false;
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

RenderingWindowInfo GLFWWindow::get_rendering_window_info() const
{
    RenderingWindowInfo info;
    if (window == nullptr)
        return info;

    int fb_w = 0, fb_h = 0;
    glfwGetFramebufferSize(window, &fb_w, &fb_h);
    info.width = fb_w > 0 ? static_cast<uint32_t>(fb_w) : 0;
    info.height = fb_h > 0 ? static_cast<uint32_t>(fb_h) : 0;

#ifdef _WIN32
    info.window = glfwGetWin32Window(window);        // HWND
    info.instance = GetModuleHandle(nullptr);         // HINSTANCE
    info.context = driver == RenderingDriverType::OpenGL ? glfwGetWGLContext(window) : nullptr;
#else
    info.window = window;
#endif

    if (driver == RenderingDriverType::OpenGL)
    {
        info.get_proc_address = [](const char *name) -> void * {
            return reinterpret_cast<void *>(glfwGetProcAddress(name));
        };
    }
#ifdef ORCA_RENDERER_VULKAN
    else if (driver == RenderingDriverType::Vulkan)
    {
        // 把平台所需的 Vulkan 实例扩展交给后端，避免后端依赖 GLFW。
        uint32_t count = 0;
        const char **extensions = glfwGetRequiredInstanceExtensions(&count);
        if (extensions)
            info.vulkan_instance_extensions.assign(extensions, extensions + count);

        // 平台桥接：由 GLFW 负责把 HWND 与 VkInstance 组合成 VkSurfaceKHR。
        info.create_vulkan_surface = [this](void *vk_instance, uint64_t &out_surface) {
            VkSurfaceKHR surface = VK_NULL_HANDLE;
            if (glfwCreateWindowSurface(static_cast<VkInstance>(vk_instance), window, nullptr, &surface) != VK_SUCCESS)
                return false;
            out_surface = reinterpret_cast<uint64_t>(surface);
            return true;
        };
    }
#endif
    return info;
}

void GLFWWindow::swap_buffers()
{
    if (window != nullptr && driver == RenderingDriverType::OpenGL)
        glfwSwapBuffers(window);
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
    event.w = w;
    event.h = h;
    emit(event);
}

void GLFWWindow::framebuffer_size_callback(int w, int h) const
{
    WindowEvent event;
    event.type = WindowEventType::FramebufferSize;
    event.w = w;
    event.h = h;
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
