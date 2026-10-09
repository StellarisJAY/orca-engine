#include "sdl3_window.h"
#include <SDL3/SDL.h>

bool SDL3Window::init(const WindowDesc &desc)
{
    SDL_WindowFlags flags = 0;
    if(desc.fullscreen) flags |= SDL_WINDOW_FULLSCREEN;
    if(desc.resizable) flags |= SDL_WINDOW_RESIZABLE;
    if(!desc.visible) flags |= SDL_WINDOW_HIDDEN;
    window = SDL_CreateWindow(desc.title, desc.width, desc.height, flags);
    if(window == nullptr) return false;
    return true;
}

void SDL3Window::shutdown()
{
    SDL_DestroyWindow(window);
}

bool SDL3Window::get_should_close() const
{
    return should_close;
}

void SDL3Window::poll_event()
{
    SDL_Event event;
    while(SDL_PollEvent(&event))
    {
        switch(event.type)
        {
            case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                should_close = true;
                WindowEvent closeEv; closeEv.type = WindowEventType::Close;
                emit(closeEv);
                break;
            case SDL_EVENT_WINDOW_RESIZED:
                WindowEvent resizeEv;
                resizeEv.w = event.window.data1;
                resizeEv.h = event.window.data2;
                resizeEv.type = WindowEventType::WindowSize;
                emit(resizeEv);
                break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
            case SDL_EVENT_MOUSE_BUTTON_UP:
                WindowEvent mbEvent;
                mbEvent.type = WindowEventType::MouseButton;
                emit(mbEvent);
                break;
            case SDL_EVENT_KEY_DOWN:
            case SDL_EVENT_KEY_UP:
                WindowEvent keyEvent;
                keyEvent.type = WindowEventType::Key;
                emit(keyEvent);
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
    // TODO
}

NativeWindowHandle SDL3Window::get_native_handle() const
{
    NativeWindowHandle nativeHandle;
    nativeHandle.window = window;
    return nativeHandle;
}

std::unique_ptr<Window> Window::create()
{
    SDL3Window window;
    return std::make_unique<SDL3Window>(std::move(window));
}