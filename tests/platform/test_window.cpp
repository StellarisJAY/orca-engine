#include "window.h"
#include "rendering_device.h"
#include <cstdio>

int main()
{
    // 使用编译期默认后端（ORCA_RENDERER_DEFAULT，默认 opengl）。
    RenderingDevice::create_singleton();
    auto device = RenderingDevice::get_singleton();
    if (!device)
    {
        std::printf("create rendering device failed\n");
        return -1;
    }
    std::printf("rendering driver: %s\n",
                device->get_driver_type() == RenderingDriverType::Vulkan ? "Vulkan" : "OpenGL");

    WindowDesc desc(800, 600);
    desc.fullscreen = false;
    desc.resizable = false;
    // 窗口需要知道要创建哪种图形上下文，因此显式传入后端类型。
    desc.driver = device->get_driver_type();

    auto window = Window::create();
    if (!window->init(desc))
    {
        std::printf("init window failed\n");
        return -1;
    }

    // 平台层只提供原生窗口/上下文信息，不包含任何图形 API 细节。
    RenderingWindowInfo window_info = window->get_rendering_window_info();

    // 阶段一：初始化后端上下文（Vulkan: instance + surface；OpenGL: 校验上下文）。
    if (!device->init(window_info))
    {
        std::printf("init rendering device failed\n");
        return -1;
    }
    // 阶段二：创建逻辑设备。
    if (!device->init_device())
    {
        std::printf("init rendering device (device) failed\n");
        return -1;
    }
    // 阶段三：创建呈现目标（Vulkan: 交换链；OpenGL: 默认帧缓冲/视口）。
    if (!device->init_swapchain())
    {
        std::printf("init rendering device (swapchain) failed\n");
        return -1;
    }

    int framebuffer_w, framebuffer_h;
    window->get_frame_buffer_size(framebuffer_w, framebuffer_h);
    std::printf("frame buffer size: w:%d, h:%d\n", framebuffer_w, framebuffer_h);

    window->register_event_callback([](const WindowEvent &event) {
        if (event.type == WindowEventType::Close) std::printf("window closed\n");
        if (event.type == WindowEventType::WindowSize) std::printf("new window size: w=%d,h=%d\n", event.w, event.h);
        if (event.type == WindowEventType::FramebufferSize) std::printf("new framebuffer size: w=%d,h=%d\n", event.w, event.h);
    });

    while (!window->get_should_close())
    {
        window->poll_event();
        window->swap_buffers();
    }

    device->shutdown();
    window->shutdown();
    RenderingDevice::destroy_singleton();
    return 0;
}
