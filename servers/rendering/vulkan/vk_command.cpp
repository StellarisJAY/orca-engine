#include "vk_rendering_device.h"
#include "vk_internal.h"
#include "os/memory.h"

#include <array>
#include <cstdio>
#include <utility>

namespace
{
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
        default:
            return vk::AttachmentLoadOp::eLoad;
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
        default:
            return vk::AttachmentStoreOp::eStore;
        }
    }

    void print_vk_error(const char *what, const vk::SystemError &error)
    {
        std::printf("[ERROR] Vulkan %s failed: %s\n", what, error.what());
    }
} // namespace

/// @brief 创建指令编码器（命令池 + 已 begin 的主 command buffer）。
CommandEncoderHandle VkRenderingDevice::create_command_encoder()
{
    CommandEncoderHandle cmd_encoder;
    VkCommandEncoderHandleData *command_encoder_data = nullptr;

    try
    {
        const auto [graphics_family, _] = findQueueFamilies(physical_device);
        if (!graphics_family.has_value())
        {
            std::printf("[ERROR] Vulkan create_command_encoder: no graphics queue family\n");
            return cmd_encoder;
        }

        vk::CommandPoolCreateInfo create_info;
        create_info.setQueueFamilyIndex(graphics_family.value());
        create_info.setFlags(vk::CommandPoolCreateFlagBits::eResetCommandBuffer);

        command_encoder_data = memnew(VkCommandEncoderHandleData);
        command_encoder_data->command_pool = device.createCommandPool(create_info);

        vk::CommandBufferAllocateInfo alloc_info;
        alloc_info.setCommandPool(*command_encoder_data->command_pool);
        alloc_info.setCommandBufferCount(1);
        alloc_info.setLevel(vk::CommandBufferLevel::ePrimary);
        auto command_buffers = device.allocateCommandBuffers(alloc_info);
        command_encoder_data->command_buffer = std::move(command_buffers[0]);

        // 命令录制前必须进入 recording 状态
        command_encoder_data->command_buffer.begin(vk::CommandBufferBeginInfo{});

        cmd_encoder.handle = alloc_handle_id();
        cmd_encoder.data = command_encoder_data;
    }
    catch (const vk::SystemError &error)
    {
        print_vk_error("create_command_encoder", error);
        memdelete(command_encoder_data);
        return {};
    }
    return cmd_encoder;
}

RenderPassHandle VkRenderingDevice::encoder_begin_render_pass(CommandEncoderHandle encoder,
                                                              const RenderPassDesc &desc)
{
    RenderPassHandle render_pass;
    if (!encoder.is_valid() || encoder.data == nullptr)
    {
        std::printf("[ERROR] Vulkan encoder_begin_render_pass: invalid encoder handle\n");
        return render_pass;
    }
    auto command_encoder_data = static_cast<VkCommandEncoderHandleData *>(encoder.data);

    VkRenderPassHandleData *render_pass_data = nullptr;
    try
    {
        vk::RenderPassCreateInfo create_info;
        std::vector<vk::AttachmentDescription> vk_attachments;
        std::vector<vk::ImageView> vk_image_views;
        std::vector<vk::ClearValue> clear_values;

        // Color 附件
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
            vk_image_views.emplace_back(*texture_view_data->image_view);
            // 附件的清屏色
            const auto &color = color_attachment.clear_color;
            clear_values.emplace_back(vk::ClearValue{vk::ClearColorValue{
                std::array<float, 4>{color[0], color[1], color[2], color[3]}}});
        }

        // 深度附件
        if (!desc.depth_attachments.empty())
        {
            const auto &depth_attachment = desc.depth_attachments[0];
            if (depth_attachment.view.is_valid())
            {
                const auto texture_view_data = static_cast<VkTextureViewHandleData *>(depth_attachment.view.data);
                vk::AttachmentDescription vk_attachment;
                vk_attachment.setLoadOp(get_vk_load_op(depth_attachment.load_op));
                vk_attachment.setStoreOp(get_vk_store_op(depth_attachment.store_op));
                vk_attachments.emplace_back(vk_attachment);
                vk_image_views.emplace_back(*texture_view_data->image_view);
                clear_values.emplace_back(vk::ClearValue{vk::ClearDepthStencilValue{depth_attachment.clear_depth, 0}});
            }
        }
        create_info.setAttachments(vk_attachments);

        render_pass_data = memnew(VkRenderPassHandleData);
        render_pass_data->render_pass = device.createRenderPass(create_info);
        render_pass_data->command_encoder_data = command_encoder_data;

        // 开始 render pass
        vk::RenderPassBeginInfo begin_info;
        vk::Rect2D render_area;
        render_area.setOffset(vk::Offset2D(0, 0));
        render_area.setExtent(vk::Extent2D(desc.width, desc.height));
        begin_info.setRenderArea(render_area);
        begin_info.setRenderPass(*render_pass_data->render_pass);
        begin_info.setClearValues(clear_values);

        // 帧缓冲
        vk::FramebufferCreateInfo fb_info;
        fb_info.setAttachments(vk_image_views);
        fb_info.setRenderPass(*render_pass_data->render_pass);
        fb_info.setWidth(desc.width);
        fb_info.setHeight(desc.height);
        fb_info.setLayers(1);
        render_pass_data->framebuffer = device.createFramebuffer(fb_info);
        begin_info.setFramebuffer(*render_pass_data->framebuffer);

        command_encoder_data->command_buffer.beginRenderPass(begin_info, vk::SubpassContents::eInline);

        render_pass.handle = alloc_handle_id();
        render_pass.data = render_pass_data;
    }
    catch (const vk::SystemError &error)
    {
        print_vk_error("encoder_begin_render_pass", error);
        memdelete(render_pass_data);
        return {};
    }
    return render_pass;
}

/// @brief 绑定渲染管线。
void VkRenderingDevice::pass_set_pipeline(RenderPassHandle pass, RenderPipelineHandle pipeline)
{
    if (!pass.is_valid() || pass.data == nullptr)
        return;
    if (!pipeline.is_valid() || pipeline.data == nullptr)
        return;
    const auto render_pass_data = static_cast<VkRenderPassHandleData *>(pass.data);
    const auto render_pipeline_data = static_cast<VkGraphicsPipelineHandleData *>(pipeline.data);
    render_pass_data->command_encoder_data->command_buffer.bindPipeline(
        vk::PipelineBindPoint::eGraphics, *render_pipeline_data->pipeline);
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

void VkRenderingDevice::pass_set_vertex_buffer(RenderPassHandle pass, uint32_t slot, GPUBufferHandle buffer,
                                               uint64_t offset)
{
    (void)pass;
    (void)slot;
    (void)buffer;
    (void)offset;
}

void VkRenderingDevice::pass_set_index_buffer(RenderPassHandle pass, GPUBufferHandle buffer, uint64_t offset,
                                              IndexFormat format)
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
    const vk::Viewport viewport(x, y, width, height, min_depth, max_depth);
    render_pass_data->command_encoder_data->command_buffer.setViewport(0, viewport);
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
    render_pass_data->command_encoder_data->command_buffer.endRenderPass();
}

/// @brief 结束录制：把命令池与 command buffer 一起转移给 CommandBufferHandleData。
CommandBufferHandle VkRenderingDevice::encoder_finish(CommandEncoderHandle encoder)
{
    CommandBufferHandle cmd_buffer;
    if (!encoder.is_valid() || encoder.data == nullptr)
    {
        std::printf("[ERROR] Vulkan encoder_finish: invalid encoder handle\n");
        return cmd_buffer;
    }
    auto cmd_encoder_data = static_cast<VkCommandEncoderHandleData *>(encoder.data);

    try
    {
        cmd_encoder_data->command_buffer.end();
    }
    catch (const vk::SystemError &error)
    {
        print_vk_error("encoder_finish", error);
        memdelete(cmd_encoder_data);
        return {};
    }

    auto cmd_buffer_data = memnew(VkCommandBufferHandleData);
    cmd_buffer_data->command_pool = std::move(cmd_encoder_data->command_pool);
    cmd_buffer_data->command_buffer = std::move(cmd_encoder_data->command_buffer);
    memdelete(cmd_encoder_data);

    cmd_buffer.handle = alloc_handle_id();
    cmd_buffer.data = cmd_buffer_data;
    return cmd_buffer;
}

void VkRenderingDevice::destroy_command_buffer(CommandBufferHandle cmd)
{
    if (!cmd.is_valid() || cmd.data == nullptr)
        return;
    memdelete(static_cast<VkCommandBufferHandleData *>(cmd.data));
}

void VkRenderingDevice::queue_submit(CommandBufferHandle cmd)
{
    (void)cmd;
    // TODO(V6)：vkQueueSubmit + semaphore/fence。
}
