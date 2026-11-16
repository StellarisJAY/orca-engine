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

GPUBufferHandle VkRenderingDevice::create_buffer(const GPUBufferDesc &desc)
{
    (void)desc;
    return {};
}

void VkRenderingDevice::queue_write_buffer(GPUBufferHandle handle, uint64_t offset, const void *data, uint64_t size)
{
    (void)handle;
    (void)offset;
    (void)data;
    (void)size;
}

void VkRenderingDevice::destroy_buffer(GPUBufferHandle handle)
{
    (void)handle;
}

void VkRenderingDevice::queue_write_texture(TextureHandle texture, const TextureWriteDesc &desc)
{
    (void)texture;
    (void)desc;
}

BindGroupLayoutHandle VkRenderingDevice::create_bind_group_layout(const BindGroupLayoutDesc &desc)
{
    (void)desc;
    return {};
}

BindGroupHandle VkRenderingDevice::create_bind_group(const BindGroupDesc &desc)
{
    (void)desc;
    return {};
}

PipelineLayoutHandle VkRenderingDevice::create_pipeline_layout(const PipelineLayoutDesc &desc)
{
    (void)desc;
    return {};
}

void VkRenderingDevice::destroy_bind_group(BindGroupHandle bind_group)
{
    (void)bind_group;
}

void VkRenderingDevice::destroy_bind_group_layout(BindGroupLayoutHandle layout)
{
    (void)layout;
}

void VkRenderingDevice::destroy_pipeline_layout(PipelineLayoutHandle layout)
{
    (void)layout;
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
