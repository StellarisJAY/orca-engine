#include "rendering_device_refactor.h"
#include "rendering_device.h"
#include "os/memory.h"
#include "window.h"

int main()
{

    RenderingDeviceRefactor::create_singleton(RenderingDriverType::Vulkan);
    auto device = RenderingDeviceRefactor::get_singleton();
    if (!device)
    {
        std::printf("SKIP: Vulkan rendering device is not available in this build\n");
        return 0;
    }

    WindowDesc d;
    d.fullscreen = false;
    d.height = 600;
    d.width = 800;
    d.visible = true;
    d.driver = RenderingDriverType::Vulkan;

    // 创建窗口
    auto window = Window::create();
    if(!window->init(d)) return false;
    std::printf("Window initialized.\n");
    RenderingWindowInfo window_info = window->get_rendering_window_info();

    if(!device->init(window_info)) return 0;
    if(!device->init_device())
    {
        window->shutdown();
        return 0;
    }
    if(!device->init_swapchain())
    {
        window->shutdown();
        return 0;
    }

    while (!window->get_should_close())
    {
        window->poll_event();
        window->swap_buffers();
    }
    return 0;
}