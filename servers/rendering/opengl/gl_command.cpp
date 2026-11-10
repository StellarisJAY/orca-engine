#include "gl_internal.h"
#include "gl_rendering_device_refactor.h"
#include "os/memory.h"

#include <cstdio>
#include <utility>

// =============================================================================
// 命令录制（M1）
//
// 本文件只做两件事：
//   1. 校验录制顺序（pass 必须在 encoder 内、encoder 同一时间只能有一个打开的 pass）；
//   2. 把调用参数快照进 GLCommandBuffer。
//
// 这里不产生任何 GL 调用：资源解析、GL 状态、绘制全部在回放阶段（gl_executor.cpp）完成。
// =============================================================================

namespace
{
    /// @brief 校验 pass 属于 encoder 且处于打开状态，失败时返回 nullptr 并打印错误。
    GLCommandEncoder *open_encoder(RenderPassHandle pass, const char *what)
    {
        if (!pass.is_valid() || pass.data == nullptr)
        {
            std::printf("[ERROR] %s: invalid render pass handle\n", what);
            return nullptr;
        }
        GLCommandEncoder *encoder = static_cast<GLCommandEncoder *>(pass.data);
        if (encoder->open_pass != pass.handle)
        {
            std::printf("[ERROR] %s: render pass %llu is not open\n", what,
                        static_cast<unsigned long long>(pass.handle));
            return nullptr;
        }
        return encoder;
    }

    void append_pass_pipeline(GLCommandBuffer &buffer, uint64_t pass, RenderPipelineHandle pipeline)
    {
        GLCommand command;
        command.type = GLCommandType::SetPipeline;
        command.pass = pass;
        command.a = pipeline.handle;
        command.ptr = pipeline.data;
        buffer.commands.push_back(command);
    }
} // namespace

// ---------------------------------------------------------------------------
// Encoder
// ---------------------------------------------------------------------------

CommandEncoderHandle GLRenderingDeviceRefactor::create_command_encoder()
{
    CommandEncoderHandle handle;
    GLCommandEncoder *encoder = memnew(GLCommandEncoder);
    encoder->id = alloc_handle_id();
    encoder->buffer.commands.reserve(256);
    handle.handle = encoder->id;
    handle.data = encoder;
    return handle;
}

RenderPassHandle GLRenderingDeviceRefactor::encoder_begin_render_pass(CommandEncoderHandle encoder_handle,
                                                                      const RenderPassDesc &desc)
{
    RenderPassHandle handle;

    if (!encoder_handle.is_valid() || encoder_handle.data == nullptr)
    {
        std::printf("[ERROR] encoder_begin_render_pass: invalid encoder handle\n");
        return handle;
    }

    GLCommandEncoder *encoder = static_cast<GLCommandEncoder *>(encoder_handle.data);
    if (encoder->open_pass != 0)
    {
        std::printf("[ERROR] encoder_begin_render_pass: encoder already has an open render pass\n");
        return handle;
    }

    if (desc.color_attachments.empty() && desc.depth_attachments.empty())
    {
        std::printf("[ERROR] encoder_begin_render_pass: render pass has no attachment\n");
        return handle;
    }

    if (desc.depth_attachments.size() > 1)
    {
        std::printf("[ERROR] encoder_begin_render_pass: at most one depth attachment is supported\n");
        return handle;
    }

    GLPassInfo info;
    info.width = desc.width;
    info.height = desc.height;

    for (const ColorAttachmentDesc &attachment : desc.color_attachments)
    {
        GLColorAttachmentInfo snapshot;
        snapshot.view = attachment.view.handle;
        snapshot.view_data = attachment.view.data;
        snapshot.load_op = attachment.load_op;
        snapshot.store_op = attachment.store_op;
        for (int i = 0; i < 4; ++i)
            snapshot.clear_color[i] = attachment.clear_color[i];
        info.colors.push_back(snapshot);
    }

    for (const DepthAttachmentDesc &attachment : desc.depth_attachments)
    {
        GLDepthAttachmentInfo snapshot;
        snapshot.view = attachment.view.handle;
        snapshot.view_data = attachment.view.data;
        snapshot.load_op = attachment.load_op;
        snapshot.store_op = attachment.store_op;
        snapshot.clear_depth = attachment.clear_depth;
        info.depths.push_back(snapshot);
    }

    const uint64_t pass_id = encoder->next_pass_id++;
    encoder->buffer.passes.push_back(std::move(info));

    GLCommand command;
    command.type = GLCommandType::BeginRenderPass;
    command.pass = pass_id;
    command.a = static_cast<uint64_t>(encoder->buffer.passes.size() - 1);
    encoder->buffer.commands.push_back(command);

    encoder->open_pass = pass_id;
    handle.handle = pass_id;
    handle.data = encoder;

    // TODO(pass): 尺寸为 0 时由附件纹理推导（需要 M2 的纹理对象），当前原样保存。
    return handle;
}

void GLRenderingDeviceRefactor::pass_set_pipeline(RenderPassHandle pass, RenderPipelineHandle pipeline)
{
    GLCommandEncoder *encoder = open_encoder(pass, "pass_set_pipeline");
    if (encoder == nullptr)
        return;

    // 录制阶段不校验管线对象是否存在（属于回放阶段的职责），只做快照。
    append_pass_pipeline(encoder->buffer, pass.handle, pipeline);
}

void GLRenderingDeviceRefactor::pass_set_bind_group(RenderPassHandle pass, uint32_t group_index,
                                                    BindGroupHandle bind_group, const uint32_t *dynamic_offsets,
                                                    uint32_t dynamic_offset_count)
{
    GLCommandEncoder *encoder = open_encoder(pass, "pass_set_bind_group");
    if (encoder == nullptr)
        return;

    if (dynamic_offset_count > 0 && dynamic_offsets == nullptr)
    {
        std::printf("[ERROR] pass_set_bind_group: dynamic_offset_count is %u but offsets pointer is null\n",
                    dynamic_offset_count);
        return;
    }

    GLCommandBuffer &buffer = encoder->buffer;
    const uint32_t offset_start = static_cast<uint32_t>(buffer.dynamic_offsets.size());
    for (uint32_t i = 0; i < dynamic_offset_count; ++i)
        buffer.dynamic_offsets.push_back(dynamic_offsets[i]);

    GLCommand command;
    command.type = GLCommandType::SetBindGroup;
    command.pass = pass.handle;
    command.a = group_index;
    command.b = bind_group.handle;
    command.ptr = bind_group.data;
    command.c = offset_start;
    command.d = dynamic_offset_count;
    buffer.commands.push_back(command);
}

void GLRenderingDeviceRefactor::pass_set_vertex_buffer(RenderPassHandle pass, uint32_t slot, GPUBufferHandle buffer,
                                                       uint64_t offset)
{
    GLCommandEncoder *encoder = open_encoder(pass, "pass_set_vertex_buffer");
    if (encoder == nullptr)
        return;

    // 本期只支持 slot 0 且 offset 为 0（见接口补全时的约定）。
    if (slot != 0 || offset != 0)
    {
        std::printf("[ERROR] pass_set_vertex_buffer: only slot 0 / offset 0 is supported (got slot=%u, offset=%llu)\n",
                    slot, static_cast<unsigned long long>(offset));
        return;
    }

    GLCommand command;
    command.type = GLCommandType::SetVertexBuffer;
    command.pass = pass.handle;
    command.a = offset;
    command.b = buffer.handle;
    command.ptr = buffer.data;
    command.c = slot;
    encoder->buffer.commands.push_back(command);
}

void GLRenderingDeviceRefactor::pass_set_index_buffer(RenderPassHandle pass, GPUBufferHandle buffer, uint64_t offset,
                                                      IndexFormat format)
{
    GLCommandEncoder *encoder = open_encoder(pass, "pass_set_index_buffer");
    if (encoder == nullptr)
        return;

    GLCommand command;
    command.type = GLCommandType::SetIndexBuffer;
    command.pass = pass.handle;
    command.a = offset;
    command.b = buffer.handle;
    command.ptr = buffer.data;
    command.c = static_cast<uint32_t>(format);
    encoder->buffer.commands.push_back(command);
}

void GLRenderingDeviceRefactor::pass_set_viewport(RenderPassHandle pass, float x, float y, float width, float height,
                                                  float min_depth, float max_depth)
{
    GLCommandEncoder *encoder = open_encoder(pass, "pass_set_viewport");
    if (encoder == nullptr)
        return;

    GLCommandBuffer &buffer = encoder->buffer;
    const uint64_t start = buffer.viewports.size();

    GLCommand command;
    command.type = GLCommandType::SetViewport;
    command.pass = pass.handle;
    command.a = start;
    buffer.viewports.push_back(x);
    buffer.viewports.push_back(y);
    buffer.viewports.push_back(width);
    buffer.viewports.push_back(height);
    buffer.viewports.push_back(min_depth);
    buffer.viewports.push_back(max_depth);
    buffer.commands.push_back(command);
}

void GLRenderingDeviceRefactor::pass_draw_indexed(RenderPassHandle pass, uint32_t index_count, uint32_t instance_count,
                                                  uint32_t first_index, int32_t base_vertex)
{
    GLCommandEncoder *encoder = open_encoder(pass, "pass_draw_indexed");
    if (encoder == nullptr)
        return;

    GLCommand command;
    command.type = GLCommandType::DrawIndexed;
    command.pass = pass.handle;
    command.a = index_count;
    // base_vertex 是 int32，按位扩到 uint64，回放时再转回 int32。
    command.b = static_cast<uint64_t>(static_cast<int64_t>(base_vertex));
    command.c = instance_count;
    command.d = first_index;
    encoder->buffer.commands.push_back(command);
}

void GLRenderingDeviceRefactor::pass_draw(RenderPassHandle pass, uint32_t vertex_count, uint32_t instance_count,
                                          uint32_t first_vertex)
{
    GLCommandEncoder *encoder = open_encoder(pass, "pass_draw");
    if (encoder == nullptr)
        return;

    GLCommand command;
    command.type = GLCommandType::Draw;
    command.pass = pass.handle;
    command.a = vertex_count;
    command.c = instance_count;
    command.d = first_vertex;
    encoder->buffer.commands.push_back(command);
}

void GLRenderingDeviceRefactor::pass_end(RenderPassHandle pass)
{
    GLCommandEncoder *encoder = open_encoder(pass, "pass_end");
    if (encoder == nullptr)
        return;

    GLCommand command;
    command.type = GLCommandType::EndRenderPass;
    command.pass = pass.handle;
    encoder->buffer.commands.push_back(command);
    encoder->open_pass = 0;
}

CommandBufferHandle GLRenderingDeviceRefactor::encoder_finish(CommandEncoderHandle encoder_handle)
{
    CommandBufferHandle handle;

    if (!encoder_handle.is_valid() || encoder_handle.data == nullptr)
    {
        std::printf("[ERROR] encoder_finish: invalid encoder handle\n");
        return handle;
    }

    GLCommandEncoder *encoder = static_cast<GLCommandEncoder *>(encoder_handle.data);
    if (encoder->open_pass != 0)
    {
        std::printf("[ERROR] encoder_finish: render pass %llu is still open\n",
                    static_cast<unsigned long long>(encoder->open_pass));
        return handle;
    }

    GLCommandBuffer *buffer = memnew(GLCommandBuffer);
    *buffer = std::move(encoder->buffer);
    buffer->id = alloc_handle_id();

    memdelete(encoder);

    handle.handle = buffer->id;
    handle.data = buffer;
    return handle;
}

void GLRenderingDeviceRefactor::destroy_command_buffer(CommandBufferHandle cmd)
{
    // 句柄按值传递，销毁后调用方手里的副本即失效；重复销毁是未定义行为。
    if (!cmd.is_valid() || cmd.data == nullptr)
        return;

    GLCommandBuffer *buffer = static_cast<GLCommandBuffer *>(cmd.data);
    memdelete(buffer);
}

// ---------------------------------------------------------------------------
// 指令队列
// ---------------------------------------------------------------------------

void GLRenderingDeviceRefactor::queue_submit(CommandBufferHandle cmd)
{
    if (!cmd.is_valid() || cmd.data == nullptr)
    {
        std::printf("[ERROR] queue_submit: invalid command buffer handle\n");
        return;
    }

    // 先执行排队中的写入，保证“先写后提交”的数据对本次绘制可见（不变式 I2）。
    executor.flush_pending_writes(*this);

    const GLCommandBuffer *buffer = static_cast<const GLCommandBuffer *>(cmd.data);
    executor.replay(*this, *buffer);

    executor.flush_pending_destroys(*this);
}
