#include "vk_rendering_device.h"
#include "vk_internal.h"
#include "os/memory.h"

/// @brief 创建Vulkan CommandPool内存池
/// @return Handle
CommandEncoderHandle VkRenderingDevice::create_command_encoder()
{
    const auto [graphics_family, _] = findQueueFamilies(physical_device);
    vk::CommandPoolCreateInfo create_info;
    create_info.setQueueFamilyIndex(graphics_family.value());
    create_info.setFlags(vk::CommandPoolCreateFlagBits::eResetCommandBuffer);
    // 创建CommandPool
    const auto command_encoder_data = memnew(VkCommandEncoderHandleData);
    command_encoder_data->command_pool = device.createCommandPool(create_info);

    // 创建CommandBuffer
    vk::CommandBufferAllocateInfo alloc_info;
    alloc_info.setCommandPool(command_encoder_data->command_pool);
    alloc_info.setCommandBufferCount(1);
    alloc_info.setLevel(vk::CommandBufferLevel::ePrimary);
    command_encoder_data->command_buffers = device.allocateCommandBuffers(alloc_info);

    CommandEncoderHandle cmd_encoder;
    cmd_encoder.handle = 1;
    cmd_encoder.data = command_encoder_data;
    return cmd_encoder;
}

vk::AttachmentLoadOp get_vk_load_op(LoadOp orca_load_op)
{
    switch (orca_load_op)
    {
    case LoadOp::Load:
        return vk::AttachmentLoadOp::eLoad;
    case LoadOp::Clear:
        return vk::AttachmentLoadOp::eClear;
    case LoadOp::DontCare:
        return vk::AttachmentLoadOp::eDontCare;
    }
}

vk::AttachmentStoreOp get_vk_store_op(StoreOp orca_store_op)
{
    switch (orca_store_op)
    {
    case StoreOp::Discard:
        return vk::AttachmentStoreOp::eDontCare;
    case StoreOp::Store:
        return vk::AttachmentStoreOp::eStore;
    }
}

RenderPassHandle VkRenderingDevice::encoder_begin_render_pass(CommandEncoderHandle encoder, const RenderPassDesc &desc)
{
    RenderPassHandle render_pass;
    if (!encoder.is_valid() || encoder.data == nullptr)
        return render_pass;
    const auto command_encoder_data = static_cast<VkCommandEncoderHandleData *>(encoder.data);

    vk::RenderPassCreateInfo create_info;
    std::vector<vk::AttachmentDescription> vk_attachments;
    std::vector<vk::ImageView> vk_image_views;
    std::vector<vk::ClearValue> clear_values;
    // Color附件
    for (const auto &color_attachment : desc.color_attachments)
    {
        if (!color_attachment.view.is_valid())
            continue;
        const auto texture_view_data = static_cast<VkTextureViewHandleData *>(color_attachment.view.data);
        vk::AttachmentDescription vk_attachment;
        // 附件读取操作：读、清空或忽略
        vk_attachment.setLoadOp(get_vk_load_op(color_attachment.load_op));
        // 附件写操作：写入或忽略
        vk_attachment.setStoreOp(get_vk_store_op(color_attachment.store_op));
        vk_attachments.emplace_back(vk_attachment);
        vk_image_views.emplace_back(texture_view_data->image_view);
        // 附件的清屏色
        const auto &color = color_attachment.clear_color;
        vk::ClearValue clear_color(vk::ClearColorValue(color));
        clear_values.emplace_back(clear_color);
    }

    // 深度附件
    if (!desc.depth_attachments.empty())
    {
        const auto &depth_attachment = desc.depth_attachments[0];
        if (depth_attachment.view.is_valid())
        {
            const auto texture_view_data = static_cast<VkTextureViewHandleData *>(depth_attachment.view.data);
            vk::AttachmentDescription vk_attachment;
            // 附件读取操作：读、清空或忽略
            vk_attachment.setLoadOp(get_vk_load_op(depth_attachment.load_op));
            // 附件写操作：写入或忽略
            vk_attachment.setStoreOp(get_vk_store_op(depth_attachment.store_op));
            vk_attachments.emplace_back(vk_attachment);
            vk_image_views.emplace_back(texture_view_data->image_view);
            // 附件的清屏色
            float clear_depth = depth_attachment.clear_depth;
            clear_values.emplace_back(vk::ClearValue(clear_depth));
        }
    }
    create_info.setAttachments(vk_attachments);

    const auto render_pass_data = memnew(VkRenderPassHandleData);
    render_pass_data->render_pass = device.createRenderPass(create_info);
    render_pass_data->command_encoder_data = command_encoder_data;
    render_pass.data = render_pass_data;
    render_pass.handle = 1;

    // 开始render pass

    vk::RenderPassBeginInfo begin_info;
    // 渲染区域
    vk::Rect2D render_area;
    render_area.setOffset(vk::Offset2D(0, 0));
    render_area.setExtent(vk::Extent2D(desc.width, desc.height));
    begin_info.setRenderArea(render_area);
    begin_info.setRenderPass(render_pass_data->render_pass);
    begin_info.setClearValues(clear_values);
    // 帧缓冲
    vk::FramebufferCreateInfo fb_info;
    fb_info.setAttachments(vk_image_views);
    fb_info.setRenderPass(render_pass_data->render_pass);
    fb_info.setWidth(desc.width);
    fb_info.setHeight(desc.height);
    fb_info.setLayers(1);
    render_pass_data->framebuffer = device.createFramebuffer(fb_info);
    begin_info.setFramebuffer(render_pass_data->framebuffer);

    // 现在只有一个主命令缓冲
    const auto &command_buffer = command_encoder_data->command_buffers[0];
    // 子pass命令嵌入到主pass命令缓冲
    command_buffer.beginRenderPass(begin_info, vk::SubpassContents::eInline);
    return render_pass;
}

/// @brief 绑定渲染管线
/// @param pass render pass handle
/// @param pipeline 渲染管线handle
void VkRenderingDevice::pass_set_pipeline(RenderPassHandle pass, RenderPipelineHandle pipeline)
{

    if (!pass.is_valid() || pass.data == nullptr)
        return;
    if (!pipeline.is_valid() || pipeline.data == nullptr)
        return;
    const auto render_pass_data = static_cast<VkRenderPassHandleData *>(pass.data);
    const auto render_pipeline_data = static_cast<VkGraphicsPipelineHandleData *>(pipeline.data);
    auto &render_pass = render_pass_data->render_pass;
    auto &render_pipeline = render_pipeline_data->pipeline;
    auto &command_buffer = render_pass_data->command_encoder_data->command_buffers;
    command_buffer[0].bindPipeline(vk::PipelineBindPoint::eGraphics, render_pipeline);
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
    if (!pass.is_valid() || pass.data == nullptr)
        return;
    const auto render_pass_data = static_cast<VkRenderPassHandleData *>(pass.data);
    auto &render_pass = render_pass_data->render_pass;
    auto &command_buffer = render_pass_data->command_encoder_data->command_buffers;
    vk::Viewport viewport(x, y, width, height, min_depth, max_depth);
    command_buffer[0].setViewport(0, viewport);
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
    if (!pass.is_valid() || pass.data == nullptr)
        return;
    const auto render_pass_data = static_cast<VkRenderPassHandleData *>(pass.data);
    auto &render_pass = render_pass_data->render_pass;
    auto &command_buffer = render_pass_data->command_encoder_data->command_buffers;
    command_buffer[0].endRenderPass();
}

CommandBufferHandle VkRenderingDevice::encoder_finish(CommandEncoderHandle encoder)
{
    CommandBufferHandle cmd_buffer;
    if (!encoder.is_valid() || encoder.data == nullptr)
        return cmd_buffer;
    const auto cmd_encoder_data = static_cast<VkCommandEncoderHandleData *>(encoder.data);
    cmd_encoder_data->command_buffers[0].end();
    auto cmd_buffer_data = memnew(VkCommandBufferHandleData);
    cmd_buffer_data->command_buffers = cmd_encoder_data->command_buffers;
    cmd_buffer.handle = 1;
    cmd_buffer.data = cmd_buffer_data;
    return cmd_buffer;
}

void VkRenderingDevice::destroy_command_buffer(CommandBufferHandle cmd)
{
    if (cmd.is_valid() && cmd.data != nullptr)
    {
    }
}

void VkRenderingDevice::queue_submit(CommandBufferHandle cmd)
{
    (void)cmd;
}