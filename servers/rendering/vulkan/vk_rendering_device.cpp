#include "vk_rendering_device.h"

// Vulkan 后端的接口骨架：只保证接口完整、可实例化，绘制与资源接口尚未实现。
// 真正的 instance / device / swapchain 初始化在 vk_rendering_device.cpp 中，
// 等 Vulkan 路径正式落地时再迁移到本文件。

void VkRenderingDevice::resize_swapchain(uint32_t width, uint32_t height)
{
    window_info.width = width;
    window_info.height = height;
}

Format VkRenderingDevice::get_swapchain_format() const
{
    return Format::RGBA8UnormSrgb;
}

void VkRenderingDevice::get_swapchain_size(uint32_t &width, uint32_t &height) const
{
    width = window_info.width;
    height = window_info.height;
}

void VkRenderingDevice::present()
{
}

void VkRenderingDevice::shutdown()
{
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
}

uint64_t VkRenderingDevice::submit_frame()
{
    return ++frame_index;
}
