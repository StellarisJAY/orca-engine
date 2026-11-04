#include "glfw_window.h"
#include "input.h"

#ifdef ORCA_RENDERER_VULKAN
#include <vulkan/vulkan_core.h> // 必须在 glfw3.h 之前：glfwCreateWindowSurface 由 VK_VERSION_1_0 守卫
#endif
#include <GLFW/glfw3.h>
// 暴露 Win32 原生句柄（HWND）与 WGL 上下文（HGLRC），让渲染后端可以自行创建 surface/上下文。
#define GLFW_EXPOSE_NATIVE_WIN32
#define GLFW_EXPOSE_NATIVE_WGL
#include <GLFW/glfw3native.h>

namespace
{

// 原生键码 -> 引擎键码。未知键返回 KeyboardKeyCode::None，由回调丢弃。
KeyboardKeyCode map_key_code(int key)
{
    // 约定：A~Z、Num0~Num9、F1~F12 在 KeyboardKeyCode 中连续。
    if (key >= GLFW_KEY_A && key <= GLFW_KEY_Z)
    {
        return static_cast<KeyboardKeyCode>(static_cast<int>(KeyboardKeyCode::A) + (key - GLFW_KEY_A));
    }
    if (key >= GLFW_KEY_0 && key <= GLFW_KEY_9)
    {
        return static_cast<KeyboardKeyCode>(static_cast<int>(KeyboardKeyCode::Num0) + (key - GLFW_KEY_0));
    }
    if (key >= GLFW_KEY_F1 && key <= GLFW_KEY_F12)
    {
        return static_cast<KeyboardKeyCode>(static_cast<int>(KeyboardKeyCode::F1) + (key - GLFW_KEY_F1));
    }

    switch (key)
    {
    case GLFW_KEY_ESCAPE: return KeyboardKeyCode::Escape;
    case GLFW_KEY_ENTER: return KeyboardKeyCode::Enter;
    case GLFW_KEY_TAB: return KeyboardKeyCode::Tab;
    case GLFW_KEY_BACKSPACE: return KeyboardKeyCode::Backspace;
    case GLFW_KEY_DELETE: return KeyboardKeyCode::Delete;
    case GLFW_KEY_INSERT: return KeyboardKeyCode::Insert;
    case GLFW_KEY_SPACE: return KeyboardKeyCode::Space;
    case GLFW_KEY_LEFT: return KeyboardKeyCode::Left;
    case GLFW_KEY_RIGHT: return KeyboardKeyCode::Right;
    case GLFW_KEY_UP: return KeyboardKeyCode::Up;
    case GLFW_KEY_DOWN: return KeyboardKeyCode::Down;
    case GLFW_KEY_HOME: return KeyboardKeyCode::Home;
    case GLFW_KEY_END: return KeyboardKeyCode::End;
    case GLFW_KEY_PAGE_UP: return KeyboardKeyCode::PageUp;
    case GLFW_KEY_PAGE_DOWN: return KeyboardKeyCode::PageDown;
    case GLFW_KEY_LEFT_SHIFT:
    case GLFW_KEY_RIGHT_SHIFT: return KeyboardKeyCode::Shift;
    case GLFW_KEY_LEFT_CONTROL:
    case GLFW_KEY_RIGHT_CONTROL: return KeyboardKeyCode::Ctrl;
    case GLFW_KEY_LEFT_ALT:
    case GLFW_KEY_RIGHT_ALT: return KeyboardKeyCode::Alt;
    // CapsLock / Super 目前没有对应的引擎键码，只通过 modifiers 表达其状态。
    default: return KeyboardKeyCode::None;
    }
}

uint16_t map_modifiers(int mods)
{
    uint16_t out = WINDOW_MOD_NONE;
    if (mods & GLFW_MOD_SHIFT) out |= WINDOW_MOD_SHIFT;
    if (mods & GLFW_MOD_CONTROL) out |= WINDOW_MOD_CTRL;
    if (mods & GLFW_MOD_ALT) out |= WINDOW_MOD_ALT;
    if (mods & GLFW_MOD_SUPER) out |= WINDOW_MOD_SUPER;
    if (mods & GLFW_MOD_CAPS_LOCK) out |= WINDOW_MOD_CAPS_LOCK;
    if (mods & GLFW_MOD_NUM_LOCK) out |= WINDOW_MOD_NUM_LOCK;
    return out;
}

MouseButton map_mouse_button(int button)
{
    switch (button)
    {
    case GLFW_MOUSE_BUTTON_LEFT: return MouseButton::ButtonLeft;
    case GLFW_MOUSE_BUTTON_RIGHT: return MouseButton::ButtonRight;
    case GLFW_MOUSE_BUTTON_MIDDLE: return MouseButton::ButtonMiddle;
    default: return MouseButton::None;
    }
}

uint32_t mouse_button_mask_bit(MouseButton button)
{
    switch (button)
    {
    case MouseButton::ButtonLeft: return WINDOW_MOUSE_MASK_LEFT;
    case MouseButton::ButtonRight: return WINDOW_MOUSE_MASK_RIGHT;
    case MouseButton::ButtonMiddle: return WINDOW_MOUSE_MASK_MIDDLE;
    default: return WINDOW_MOUSE_MASK_NONE;
    }
}

} // namespace

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

    // ---- 键盘 ----
    glfwSetKeyCallback(window, [](GLFWwindow *w, int key, int sc, int act, int mods) {
        auto window = static_cast<GLFWWindow *>(glfwGetWindowUserPointer(w));
        window->key_callback(key, sc, act, mods);
    });

    glfwSetCharCallback(window, [](GLFWwindow *w, unsigned int codepoint) {
        auto window = static_cast<GLFWWindow *>(glfwGetWindowUserPointer(w));
        window->char_callback(codepoint);
    });

    // ---- 鼠标 ----
    glfwSetMouseButtonCallback(window, [](GLFWwindow *w, int btn, int act, int mods) {
        auto window = static_cast<GLFWWindow *>(glfwGetWindowUserPointer(w));
        window->mouse_button_callback(btn, act, mods);
    });

    glfwSetCursorPosCallback(window, [](GLFWwindow *w, double x, double y) {
        auto window = static_cast<GLFWWindow *>(glfwGetWindowUserPointer(w));
        window->cursor_pos_callback(x, y);
    });

    glfwSetScrollCallback(window, [](GLFWwindow *w, double xoffset, double yoffset) {
        auto window = static_cast<GLFWWindow *>(glfwGetWindowUserPointer(w));
        window->scroll_callback(xoffset, yoffset);
    });

    glfwSetCursorEnterCallback(window, [](GLFWwindow *w, int entered) {
        auto window = static_cast<GLFWWindow *>(glfwGetWindowUserPointer(w));
        window->cursor_enter_callback(entered);
    });

    // ---- 窗口 ----
    glfwSetWindowFocusCallback(window, [](GLFWwindow *w, int focused) {
        auto window = static_cast<GLFWWindow *>(glfwGetWindowUserPointer(w));
        window->window_focus_callback(focused);
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

void GLFWWindow::set_mouse_mode(MouseMode mode)
{
    switch(mode)
    {
    case MouseMode::Hidden:
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_HIDDEN);
        break;
    case MouseMode::Captured:
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_CAPTURED);
        break;
    case MouseMode::Visible:
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        break;
    }
}

void GLFWWindow::key_callback(int key, int sc, int act, int mods)
{
    (void)sc;
    KeyboardKeyCode mapped = map_key_code(key);
    // 未知键直接丢弃：宁可少一个事件，也不要把无效键码传给上层。
    if (mapped == KeyboardKeyCode::None)
        return;

    WindowEvent event;
    event.type = WindowEventType::Key;
    event.key_code = mapped;
    event.pressed = act != GLFW_RELEASE;
    event.press_strength = act != GLFW_RELEASE ? 1.0 : 0.0; // 键盘只有0或1的力度
    event.repeat = act == GLFW_REPEAT;
    event.modifiers = map_modifiers(mods);
    emit(event);
}

void GLFWWindow::char_callback(unsigned int codepoint)
{
    WindowEvent event;
    event.type = WindowEventType::TextInput;
    event.unicode = static_cast<char32_t>(codepoint);
    emit(event);
}

void GLFWWindow::mouse_button_callback(int btn, int act, int mods)
{
    MouseButton mapped = map_mouse_button(btn);
    if (mapped == MouseButton::None)
        return;

    WindowEvent event;
    event.type = WindowEventType::MouseButton;
    event.mouse_button = mapped;
    event.pressed = act != GLFW_RELEASE;
    event.press_strength = act != GLFW_RELEASE ? 1.0 : 0.0; // 鼠标只有0或1的力度
    event.modifiers = map_modifiers(mods);

    // 自己维护掩码：在回调内查询 glfwGetMouseButton 不一定反映本次事件。
    uint32_t bit = mouse_button_mask_bit(mapped);
    if (event.pressed)
        mouse_button_mask |= bit;
    else
        mouse_button_mask &= ~bit;
    event.button_mask = mouse_button_mask;

    double x = 0.0, y = 0.0;
    glfwGetCursorPos(window, &x, &y);
    event.x = static_cast<float>(x);
    event.y = static_cast<float>(y);
    emit(event);
}

void GLFWWindow::cursor_pos_callback(double x, double y)
{
    WindowEvent event;
    event.type = WindowEventType::MouseMotion;
    event.x = static_cast<float>(x);
    event.y = static_cast<float>(y);
    if (cursor_initialized)
    {
        event.delta_x = static_cast<float>(x - last_cursor_x);
        event.delta_y = static_cast<float>(y - last_cursor_y);
    }
    last_cursor_x = x;
    last_cursor_y = y;
    cursor_initialized = true;
    event.button_mask = mouse_button_mask;
    emit(event);
}

void GLFWWindow::scroll_callback(double xoffset, double yoffset)
{
    WindowEvent event;
    event.type = WindowEventType::MouseWheel;
    event.scroll_x = static_cast<float>(xoffset);
    event.scroll_y = static_cast<float>(yoffset);
    event.button_mask = mouse_button_mask;

    // 滚轮事件本身不带坐标，补上当前光标位置，方便 GUI 直接命中测试。
    double x = 0.0, y = 0.0;
    glfwGetCursorPos(window, &x, &y);
    event.x = static_cast<float>(x);
    event.y = static_cast<float>(y);
    emit(event);
}

void GLFWWindow::cursor_enter_callback(int entered)
{
    WindowEvent event;
    event.type = WindowEventType::CursorEnter;
    event.entered = entered == GLFW_TRUE;
    if (!event.entered)
        cursor_initialized = false; // 离开窗口后不跨窗口边界计算相对位移
    emit(event);
}

void GLFWWindow::window_focus_callback(int focused)
{
    WindowEvent event;
    event.type = WindowEventType::Focus;
    event.focused = focused == GLFW_TRUE;

    // 失去焦点时 GLFW 未必再送出鼠标抬起事件，这里清空本地掩码，
    // 避免上层看到「鼠标键永远按着」。
    if (!event.focused)
    {
        mouse_button_mask = WINDOW_MOUSE_MASK_NONE;
        cursor_initialized = false;
    }
    emit(event);
}

void GLFWWindow::window_size_callback(int w, int h)
{
    WindowEvent event;
    event.type = WindowEventType::WindowSize;
    event.w = w;
    event.h = h;
    emit(event);
}

void GLFWWindow::framebuffer_size_callback(int w, int h)
{
    WindowEvent event;
    event.type = WindowEventType::FramebufferSize;
    event.w = w;
    event.h = h;
    emit(event);
}

void GLFWWindow::window_close_callback()
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
