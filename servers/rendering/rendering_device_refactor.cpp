#include "rendering_device_refactor.h"

#include <cstdio>

#if defined(ORCA_RENDERER_VULKAN)
#include "vulkan/vk_rendering_device.h"
#endif

void RenderingDeviceRefactor::create_singleton()
{
    create_singleton(get_default_driver());
}

void RenderingDeviceRefactor::create_singleton(RenderingDriverType driver)
{
    // 新 RHI 只保留 Vulkan 实现；OpenGL 适配层已移除，因此显式请求 OpenGL 会被拒绝，
    // 而不是静默创建 Vulkan 设备（避免窗口按 GL 创建、设备却是 Vulkan 的错配）。
    if (driver != RenderingDriverType::Vulkan)
    {
        std::fprintf(stderr,
                     "[ERROR] RenderingDeviceRefactor only supports Vulkan; "
                     "the OpenGL adapter has been removed\n");
        return;
    }

#if defined(ORCA_RENDERER_VULKAN)
    singleton = std::make_shared<VkRenderingDevice>();
#else
    std::fprintf(stderr, "[ERROR] Vulkan backend is not available in this build\n");
#endif
}

void RenderingDeviceRefactor::destroy_singleton()
{
    singleton.reset();
}

RenderingDriverType RenderingDeviceRefactor::get_default_driver()
{
    // 新 RHI 没有 OpenGL 实现，默认驱动恒为 Vulkan；旧 RenderingDevice 在编译了 GL 时
    // 仍默认 OpenGL（见 rendering_device.cpp）。
    return RenderingDriverType::Vulkan;
}
