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

ShaderHandle VkRenderingDevice::create_shader_module(const ShaderModuleDesc &desc)
{
    (void)desc;
    return {};
}

void VkRenderingDevice::destroy_shader_module(ShaderHandle handle)
{
    (void)handle;
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

TextureHandle VkRenderingDevice::create_texture(const TextureDescRefactor &desc)
{
    (void)desc;
    return {};
}

TextureViewHandle VkRenderingDevice::create_texture_view(TextureHandle texture, const TextureViewDesc &desc)
{
    (void)texture;
    (void)desc;
    return {};
}

SamplerHandle VkRenderingDevice::create_sampler(const SamplerDesc &desc)
{
    (void)desc;
    return {};
}

void VkRenderingDevice::queue_write_texture(TextureHandle texture, const TextureWriteDesc &desc)
{
    (void)texture;
    (void)desc;
}

void VkRenderingDevice::generate_mip_map(TextureHandle texture)
{
    (void)texture;
}

void VkRenderingDevice::destroy_texture(TextureHandle texture)
{
    (void)texture;
}

void VkRenderingDevice::destroy_texture_view(TextureViewHandle view)
{
    (void)view;
}

void VkRenderingDevice::destroy_sampler(SamplerHandle sampler)
{
    (void)sampler;
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

RenderPipelineHandle VkRenderingDevice::create_render_pipeline(const RenderPipelineDesc &desc)
{
    (void)desc;
    return {};
}

void VkRenderingDevice::destroy_render_pipeline(RenderPipelineHandle pipeline)
{
    (void)pipeline;
}

CommandEncoderHandle VkRenderingDevice::create_command_encoder()
{
    return {};
}

RenderPassHandle VkRenderingDevice::encoder_begin_render_pass(CommandEncoderHandle encoder, const RenderPassDesc &desc)
{
    (void)encoder;
    (void)desc;
    return {};
}

void VkRenderingDevice::pass_set_pipeline(RenderPassHandle pass, RenderPipelineHandle pipeline)
{
    (void)pass;
    (void)pipeline;
}

void VkRenderingDevice::pass_set_bind_group(RenderPassHandle pass, uint32_t group_index, BindGroupHandle bind_group,
                                                    const uint32_t *dynamic_offsets, uint32_t dynamic_offset_count)
{
    (void)pass;
    (void)group_index;
    (void)bind_group;
    (void)dynamic_offsets;
    (void)dynamic_offset_count;
}

void VkRenderingDevice::pass_set_vertex_buffer(RenderPassHandle pass, uint32_t slot, GPUBufferHandle buffer, uint64_t offset)
{
    (void)pass;
    (void)slot;
    (void)buffer;
    (void)offset;
}

void VkRenderingDevice::pass_set_index_buffer(RenderPassHandle pass, GPUBufferHandle buffer, uint64_t offset, IndexFormat format)
{
    (void)pass;
    (void)buffer;
    (void)offset;
    (void)format;
}

void VkRenderingDevice::pass_set_viewport(RenderPassHandle pass, float x, float y, float width, float height,
                                                  float min_depth, float max_depth)
{
    (void)pass;
    (void)x;
    (void)y;
    (void)width;
    (void)height;
    (void)min_depth;
    (void)max_depth;
}

void VkRenderingDevice::pass_draw_indexed(RenderPassHandle pass, uint32_t index_count, uint32_t instance_count,
                                                  uint32_t first_index, int32_t base_vertex)
{
    (void)pass;
    (void)index_count;
    (void)instance_count;
    (void)first_index;
    (void)base_vertex;
}

void VkRenderingDevice::pass_draw(RenderPassHandle pass, uint32_t vertex_count, uint32_t instance_count,
                                          uint32_t first_vertex)
{
    (void)pass;
    (void)vertex_count;
    (void)instance_count;
    (void)first_vertex;
}

void VkRenderingDevice::pass_end(RenderPassHandle pass)
{
    (void)pass;
}

CommandBufferHandle VkRenderingDevice::encoder_finish(CommandEncoderHandle encoder)
{
    (void)encoder;
    return {};
}

void VkRenderingDevice::destroy_command_buffer(CommandBufferHandle cmd)
{
    (void)cmd;
}

void VkRenderingDevice::queue_submit(CommandBufferHandle cmd)
{
    (void)cmd;
}

void VkRenderingDevice::queue_wait_idle()
{
}

uint64_t VkRenderingDevice::submit_frame()
{
    return ++frame_index;
}
