#include "vk_rendering_device.h"
#include "vk_format.h"

#include <cstdio>

void VkRenderingDevice::present()
{
    // TODO(V3)：acquire/present 与 semaphore 链。
}

void VkRenderingDevice::shutdown()
{
    try
    {
        if (device != nullptr)
            device.waitIdle();
    }
    catch (const vk::SystemError &error)
    {
        std::printf("[WARN] Vulkan device waitIdle during shutdown failed: %s\n", error.what());
    }
}

void VkRenderingDevice::queue_wait_idle()
{
    try
    {
        if (device != nullptr)
            device.waitIdle();
    }
    catch (const vk::SystemError &error)
    {
        std::printf("[WARN] Vulkan device waitIdle failed: %s\n", error.what());
    }
}

uint64_t VkRenderingDevice::submit_frame()
{
    return ++frame_index;
}
