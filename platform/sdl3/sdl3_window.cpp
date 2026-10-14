#include "sdl3_window.h"
#include <SDL3/SDL.h>
#ifdef ORCA_RENDERER_VULKAN
#include <SDL3/SDL_vulkan.h>
#endif

bool SDL3Window::init(const WindowDesc &desc)
{
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
        return false;

    if (driver == RenderingDriverType::OpenGL)
    {
        gl_context = SDL_GL_CreateContext(window);
        if (gl_context == nullptr)
        {
            SDL_DestroyWindow(window);
            window = nullptr;
            return false;
        }
        SDL_GL_SetSwapInterval(1);
    }
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
        SDL_DestroyWindow(window);
        window = nullptr;
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
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            should_close = true;
            {
                WindowEvent closeEv;
                closeEv.type = WindowEventType::Close;
                emit(closeEv);
            }
            break;
        case SDL_EVENT_WINDOW_RESIZED:
            {
                WindowEvent resizeEv;
                resizeEv.w = event.window.data1;
                resizeEv.h = event.window.data2;
                resizeEv.type = WindowEventType::WindowSize;
                emit(resizeEv);
            }
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
            {
                WindowEvent mbEvent;
                mbEvent.type = WindowEventType::MouseButton;
                emit(mbEvent);
            }
            break;
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP:
            {
                WindowEvent keyEvent;
                keyEvent.type = WindowEventType::Key;
                emit(keyEvent);
            }
            break;
        }
    }
}

void SDL3Window::get_size(int &w, int &h) const
{
    SDL_GetWindowSizeInPixels(window, &w, &h);
}

void SDL3Window::get_frame_buffer_size(int &w, int &h) const
{
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
        info.get_proc_address = [](const char *name) -> void * {
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

        info.create_vulkan_surface = [this](void *vk_instance, uint64_t &out_surface) {
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

std::unique_ptr<Window> Window::create()
{
    SDL3Window window;
    return std::make_unique<SDL3Window>(std::move(window));
}
