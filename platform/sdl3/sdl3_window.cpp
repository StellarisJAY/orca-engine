#include "sdl3_window.h"
#include <SDL3/SDL.h>
#ifdef ORCA_RENDERER_VULKAN
#include <SDL3/SDL_vulkan.h>
#endif

#include <cstdio>

namespace
{

    // SDL 虚拟键码 -> 引擎键码。未知键返回 KeyboardKeyCode::None，由事件循环丢弃。
    KeyboardKeyCode map_key_code(SDL_Keycode key)
    {
        // SDLK_A..SDLK_Z 是 'a'..'z'，SDLK_0..SDLK_9 是 '0'..'9'，小写/大写都归一化。
        if (key >= SDLK_A && key <= SDLK_Z)
        {
            return static_cast<KeyboardKeyCode>(static_cast<int>(KeyboardKeyCode::A) + (key - SDLK_A));
        }
        if (key >= 'A' && key <= 'Z')
        {
            return static_cast<KeyboardKeyCode>(static_cast<int>(KeyboardKeyCode::A) + (key - 'A'));
        }
        if (key >= SDLK_0 && key <= SDLK_9)
        {
            return static_cast<KeyboardKeyCode>(static_cast<int>(KeyboardKeyCode::Num0) + (key - SDLK_0));
        }
        if (key >= SDLK_F1 && key <= SDLK_F12)
        {
            return static_cast<KeyboardKeyCode>(static_cast<int>(KeyboardKeyCode::F1) + (key - SDLK_F1));
        }

        switch (key)
        {
        case SDLK_ESCAPE:
            return KeyboardKeyCode::Escape;
        case SDLK_RETURN:
            return KeyboardKeyCode::Enter;
        case SDLK_TAB:
            return KeyboardKeyCode::Tab;
        case SDLK_BACKSPACE:
            return KeyboardKeyCode::Backspace;
        case SDLK_DELETE:
            return KeyboardKeyCode::Delete;
        case SDLK_INSERT:
            return KeyboardKeyCode::Insert;
        case SDLK_SPACE:
            return KeyboardKeyCode::Space;
        case SDLK_LEFT:
            return KeyboardKeyCode::Left;
        case SDLK_RIGHT:
            return KeyboardKeyCode::Right;
        case SDLK_UP:
            return KeyboardKeyCode::Up;
        case SDLK_DOWN:
            return KeyboardKeyCode::Down;
        case SDLK_HOME:
            return KeyboardKeyCode::Home;
        case SDLK_END:
            return KeyboardKeyCode::End;
        case SDLK_PAGEUP:
            return KeyboardKeyCode::PageUp;
        case SDLK_PAGEDOWN:
            return KeyboardKeyCode::PageDown;
        case SDLK_LSHIFT:
        case SDLK_RSHIFT:
            return KeyboardKeyCode::Shift;
        case SDLK_LCTRL:
        case SDLK_RCTRL:
            return KeyboardKeyCode::Ctrl;
        case SDLK_LALT:
        case SDLK_RALT:
            return KeyboardKeyCode::Alt;
        // CapsLock / Super 目前没有对应的引擎键码，只通过 modifiers 表达其状态。
        default:
            return KeyboardKeyCode::None;
        }
    }

    uint16_t map_modifiers(SDL_Keymod mods)
    {
        uint16_t out = WINDOW_MOD_NONE;
        if (mods & SDL_KMOD_SHIFT)
            out |= WINDOW_MOD_SHIFT;
        if (mods & SDL_KMOD_CTRL)
            out |= WINDOW_MOD_CTRL;
        if (mods & SDL_KMOD_ALT)
            out |= WINDOW_MOD_ALT;
        if (mods & SDL_KMOD_GUI)
            out |= WINDOW_MOD_SUPER;
        if (mods & SDL_KMOD_CAPS)
            out |= WINDOW_MOD_CAPS_LOCK;
        if (mods & SDL_KMOD_NUM)
            out |= WINDOW_MOD_NUM_LOCK;
        return out;
    }

    MouseButton map_mouse_button(Uint8 button)
    {
        switch (button)
        {
        case SDL_BUTTON_LEFT:
            return MouseButton::ButtonLeft;
        case SDL_BUTTON_RIGHT:
            return MouseButton::ButtonRight;
        case SDL_BUTTON_MIDDLE:
            return MouseButton::ButtonMiddle;
        default:
            return MouseButton::None;
        }
    }

    uint32_t mouse_button_mask_bit(MouseButton button)
    {
        switch (button)
        {
        case MouseButton::ButtonLeft:
            return WINDOW_MOUSE_MASK_LEFT;
        case MouseButton::ButtonRight:
            return WINDOW_MOUSE_MASK_RIGHT;
        case MouseButton::ButtonMiddle:
            return WINDOW_MOUSE_MASK_MIDDLE;
        default:
            return WINDOW_MOUSE_MASK_NONE;
        }
    }

    // SDL_MOUSE_MOTION 自带按键状态位，转成引擎掩码。
    uint32_t map_button_state(SDL_MouseButtonFlags state)
    {
        uint32_t out = WINDOW_MOUSE_MASK_NONE;
        if (state & SDL_BUTTON_LMASK)
            out |= WINDOW_MOUSE_MASK_LEFT;
        if (state & SDL_BUTTON_RMASK)
            out |= WINDOW_MOUSE_MASK_RIGHT;
        if (state & SDL_BUTTON_MMASK)
            out |= WINDOW_MOUSE_MASK_MIDDLE;
        return out;
    }

    // 解出 UTF-8 的下一个码点，返回消耗的字节数，非法序列返回 0。
    int utf8_next(const unsigned char *s, char32_t &out)
    {
        if (s[0] < 0x80)
        {
            out = s[0];
            return 1;
        }
        if ((s[0] & 0xE0) == 0xC0 && (s[1] & 0xC0) == 0x80)
        {
            out = ((s[0] & 0x1Fu) << 6) | (s[1] & 0x3Fu);
            return 2;
        }
        if ((s[0] & 0xF0) == 0xE0 && (s[1] & 0xC0) == 0x80 && (s[2] & 0xC0) == 0x80)
        {
            out = ((s[0] & 0x0Fu) << 12) | ((s[1] & 0x3Fu) << 6) | (s[2] & 0x3Fu);
            return 3;
        }
        if ((s[0] & 0xF8) == 0xF0 && (s[1] & 0xC0) == 0x80 && (s[2] & 0xC0) == 0x80 && (s[3] & 0xC0) == 0x80)
        {
            out = ((s[0] & 0x07u) << 18) | ((s[1] & 0x3Fu) << 12) | ((s[2] & 0x3Fu) << 6) | (s[3] & 0x3Fu);
            return 4;
        }
        return 0;
    }

} // namespace

bool SDL3Window::init(const WindowDesc &desc)
{
    // SDL3 要求使用视频/事件子系统前显式初始化。
    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        std::printf("[ERROR] SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }
    sdl_initialized = true;

    driver = desc.driver;

    SDL_WindowFlags flags = 0;
    if (desc.fullscreen)
        flags |= SDL_WINDOW_FULLSCREEN;
    if (desc.resizable)
        flags |= SDL_WINDOW_RESIZABLE;
    if (!desc.visible)
        flags |= SDL_WINDOW_HIDDEN;

    if (driver == RenderingDriverType::OpenGL)
    {
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
        flags |= SDL_WINDOW_OPENGL;
    }
    else
    {
        flags |= SDL_WINDOW_VULKAN;
    }

    window = SDL_CreateWindow(desc.title, desc.width, desc.height, flags);
    if (window == nullptr)
    {
        std::printf("[ERROR] SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        sdl_initialized = false;
        return false;
    }

    if (driver == RenderingDriverType::OpenGL)
    {
        gl_context = SDL_GL_CreateContext(window);
        if (gl_context == nullptr)
        {
            SDL_DestroyWindow(window);
            window = nullptr;
            SDL_Quit();
            sdl_initialized = false;
            return false;
        }
        SDL_GL_SetSwapInterval(1);
    }

    // 与 GLFW 后端保持一致：GLFW 的 char 回调默认一直触发，SDL3 需要显式开启文本输入。
    SDL_StartTextInput(window);
    return true;
}

void SDL3Window::shutdown()
{
    if (gl_context != nullptr)
    {
        SDL_GL_DestroyContext(gl_context);
        gl_context = nullptr;
    }
    if (window != nullptr)
    {
        SDL_StopTextInput(window);
        SDL_DestroyWindow(window);
        window = nullptr;
    }
    if (sdl_initialized)
    {
        SDL_Quit();
        sdl_initialized = false;
    }
}

bool SDL3Window::get_should_close() const
{
    return should_close;
}

void SDL3Window::poll_event()
{
    SDL_Event event;
    while (SDL_PollEvent(&event))
    {
        switch (event.type)
        {
        case SDL_EVENT_QUIT:
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
        {
            should_close = true;
            WindowEvent close_event;
            close_event.type = WindowEventType::Close;
            emit(close_event);
            break;
        }
        case SDL_EVENT_WINDOW_RESIZED:
        {
            WindowEvent resize_event;
            resize_event.type = WindowEventType::WindowSize;
            resize_event.w = static_cast<int>(event.window.data1);
            resize_event.h = static_cast<int>(event.window.data2);
            emit(resize_event);
            break;
        }
        case SDL_EVENT_WINDOW_FOCUS_GAINED:
        case SDL_EVENT_WINDOW_FOCUS_LOST:
        {
            WindowEvent focus_event;
            focus_event.type = WindowEventType::Focus;
            focus_event.focused = event.type == SDL_EVENT_WINDOW_FOCUS_GAINED;
            // 失去焦点后不再保证收到鼠标抬起事件，清空本地掩码避免按键卡住。
            if (!focus_event.focused)
                mouse_button_mask = WINDOW_MOUSE_MASK_NONE;
            emit(focus_event);
            break;
        }
        case SDL_EVENT_WINDOW_MOUSE_ENTER:
        case SDL_EVENT_WINDOW_MOUSE_LEAVE:
        {
            WindowEvent enter_event;
            enter_event.type = WindowEventType::CursorEnter;
            enter_event.entered = event.type == SDL_EVENT_WINDOW_MOUSE_ENTER;
            emit(enter_event);
            break;
        }
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP:
        {
            KeyboardKeyCode key = map_key_code(event.key.key);
            if (key == KeyboardKeyCode::None)
                break;

            WindowEvent key_event;
            key_event.type = WindowEventType::Key;
            key_event.key_code = key;
            key_event.pressed = event.key.down;
            key_event.press_strength = event.key.down ? 1.0 : 0.0; // 键盘只有0或1的力度
            key_event.repeat = event.key.repeat;
            key_event.modifiers = map_modifiers(event.key.mod);
            emit(key_event);
            break;
        }
        case SDL_EVENT_TEXT_INPUT:
        {
            // 一个 TEXT_INPUT 可能包含多个码点（输入法一次性提交），逐个发出。
            const unsigned char *text = reinterpret_cast<const unsigned char *>(event.text.text);
            if (text == nullptr)
                break;
            while (*text != 0)
            {
                char32_t codepoint = 0;
                int bytes = utf8_next(text, codepoint);
                if (bytes == 0)
                {
                    ++text; // 非法序列跳过一字节，避免死循环
                    continue;
                }
                WindowEvent text_event;
                text_event.type = WindowEventType::TextInput;
                text_event.unicode = codepoint;
                emit(text_event);
                text += bytes;
            }
            break;
        }
        case SDL_EVENT_MOUSE_MOTION:
        {
            WindowEvent motion_event;
            motion_event.type = WindowEventType::MouseMotion;
            motion_event.x = event.motion.x;
            motion_event.y = event.motion.y;
            motion_event.delta_x = event.motion.xrel;
            motion_event.delta_y = event.motion.yrel;
            motion_event.button_mask = map_button_state(event.motion.state);
            mouse_button_mask = motion_event.button_mask;
            emit(motion_event);
            break;
        }
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
        {
            MouseButton button = map_mouse_button(event.button.button);
            if (button == MouseButton::None)
                break;

            WindowEvent button_event;
            button_event.type = WindowEventType::MouseButton;
            button_event.mouse_button = button;
            button_event.pressed = event.button.down;
            button_event.press_strength = event.button.down ? 1.0 : 0.0; // 鼠标只有0或1的力度
            button_event.modifiers = map_modifiers(SDL_GetModState());

            uint32_t bit = mouse_button_mask_bit(button);
            if (button_event.pressed)
                mouse_button_mask |= bit;
            else
                mouse_button_mask &= ~bit;
            button_event.button_mask = mouse_button_mask;

            button_event.x = event.button.x;
            button_event.y = event.button.y;
            emit(button_event);
            break;
        }
        case SDL_EVENT_MOUSE_WHEEL:
        {
            WindowEvent wheel_event;
            wheel_event.type = WindowEventType::MouseWheel;
            wheel_event.scroll_x = event.wheel.x;
            wheel_event.scroll_y = event.wheel.y;
            wheel_event.x = event.wheel.mouse_x;
            wheel_event.y = event.wheel.mouse_y;
            wheel_event.button_mask = mouse_button_mask;
            emit(wheel_event);
            break;
        }
        default:
            break;
        }
    }
}

void SDL3Window::get_size(int &w, int &h) const
{
    // 窗口坐标（逻辑尺寸），与鼠标事件坐标系一致。
    SDL_GetWindowSize(window, &w, &h);
}

void SDL3Window::get_frame_buffer_size(int &w, int &h) const
{
    // 物理像素尺寸，高 DPI 下与窗口坐标不同，用于渲染视口。
    SDL_GetWindowSizeInPixels(window, &w, &h);
}

RenderingWindowInfo SDL3Window::get_rendering_window_info() const
{
    RenderingWindowInfo info;
    if (window == nullptr)
        return info;

    int w = 0, h = 0;
    SDL_GetWindowSizeInPixels(window, &w, &h);
    info.width = w > 0 ? static_cast<uint32_t>(w) : 0;
    info.height = h > 0 ? static_cast<uint32_t>(h) : 0;

    if (driver == RenderingDriverType::OpenGL)
    {
        info.context = gl_context;
        info.get_proc_address = [](const char *name) -> void *
        {
            return reinterpret_cast<void *>(SDL_GL_GetProcAddress(name));
        };
    }
#ifdef ORCA_RENDERER_VULKAN
    else if (driver == RenderingDriverType::Vulkan)
    {
        Uint32 count = 0;
        const char *const *extensions = SDL_Vulkan_GetInstanceExtensions(&count);
        if (extensions)
            info.vulkan_instance_extensions.assign(extensions, extensions + count);

        info.create_vulkan_surface = [this](void *vk_instance, uint64_t &out_surface)
        {
            VkSurfaceKHR surface = VK_NULL_HANDLE;
            if (!SDL_Vulkan_CreateSurface(window, static_cast<VkInstance>(vk_instance), nullptr, &surface))
                return false;
            out_surface = reinterpret_cast<uint64_t>(surface);
            return true;
        };
    }
#endif
    return info;
}

void SDL3Window::swap_buffers()
{
    if (window != nullptr && driver == RenderingDriverType::OpenGL)
        SDL_GL_SwapWindow(window);
}

void SDL3Window::set_mouse_mode(MouseMode mode)
{
    switch (mode)
    {
    case MouseMode::Hidden:
        break;
    case MouseMode::Captured:
        break;
    case MouseMode::Visible:
        break;
    }
}

std::unique_ptr<Window> Window::create()
{
    SDL3Window window;
    return std::make_unique<SDL3Window>(std::move(window));
}
