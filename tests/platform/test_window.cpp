#include "window.h"
#include <iostream>
int main()
{
    WindowDesc desc(800, 600);
    desc.fullscreen = false;
    desc.resizable = false;
    
    auto window = Window::create();
    if(!window->init(desc))
    {
        std::printf("init window failed\n");
        return -1;
    }

    int framebuffer_w, framebuffer_h;
    window->get_frame_buffer_size(framebuffer_w, framebuffer_w);
    std::printf("frame buffer size: w:%d, h:%d\n", framebuffer_w, framebuffer_h);

    window->register_event_callback([](const WindowEvent &event) {
        if(event.type == WindowEventType::Close) std::printf("window closed\n");
        if(event.type == WindowEventType::WindowSize) std::printf("new window size: w=%d,h=%d\n", event.w, event.h);
        if(event.type == WindowEventType::FramebufferSize) std::printf("new framebuffer size: w=%d,h=%d\n", event.w, event.h);
    });

    while(!window->get_should_close())
    {
        window->poll_event();
    }

    window->shutdown();
    return 0;
}