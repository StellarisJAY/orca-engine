#pragma once

#include "rendering_device_refactor.h"

#include <cstdint>
#include <string>
#include <vector>

// =============================================================================
// OpenGL 后端内部数据结构：命令录制与回放。
//
// 设计约定：
//   - 录制阶段不产生任何 GL 调用，只往 GLCommandBuffer 里追加命令；
//   - 命令里存"录制时快照"的句柄/对象指针，回放时直接使用，不再查表；
//   - 需要变长参数的命令（附件、动态偏移、viewport）把数据放在侧表里，
//     命令只存下标，避免每条命令一次堆分配；
//   - 资源必须在对应 command buffer 提交之后才能销毁（见 M7 的延迟销毁）。
// =============================================================================

/// @brief BeginRenderPass 的颜色附件快照。
struct GLColorAttachmentInfo
{
    uint64_t view = 0;          // TextureViewHandle.handle，0 表示默认帧缓冲
    void *view_data = nullptr;  // TextureViewHandle.data
    LoadOp load_op = LoadOp::Clear;
    StoreOp store_op = StoreOp::Store;
    float clear_color[4] = {0.0f, 0.0f, 0.0f, 1.0f};
};

/// @brief BeginRenderPass 的深度附件快照。
struct GLDepthAttachmentInfo
{
    uint64_t view = 0;
    void *view_data = nullptr;
    LoadOp load_op = LoadOp::Clear;
    StoreOp store_op = StoreOp::Store;
    float clear_depth = 1.0f;
};

/// @brief 一个 render pass 的附件信息。
struct GLPassInfo
{
    std::vector<GLColorAttachmentInfo> colors;
    std::vector<GLDepthAttachmentInfo> depths;
    /// @brief 渲染区域尺寸；0 表示回放时由附件推导
    uint32_t width = 0;
    uint32_t height = 0;
};

enum class GLCommandType : uint8_t
{
    BeginRenderPass,
    EndRenderPass,
    SetPipeline,
    SetBindGroup,
    SetVertexBuffer,
    SetIndexBuffer,
    SetViewport,
    DrawIndexed,
    Draw,
    // 以下为 M5 预留，当前录制侧还不会产生
    WriteBuffer,
    WriteTexture,
};

/// @brief 一条命令。字段语义由 type 决定：
///
///   BeginRenderPass : pass = pass id, a = GLPassInfo 下标
///   EndRenderPass   : pass = pass id
///   SetPipeline     : pass = pass id, a = pipeline.handle, ptr = pipeline.data
///   SetBindGroup    : pass = pass id, a = group index, b = bind_group.handle,
///                     ptr = bind_group.data, c = dynamic_offsets 起始下标, d = 数量
///   SetVertexBuffer : pass = pass id, a = buffer offset（本期恒为 0），
///                     b = buffer.handle, ptr = buffer.data, c = slot（本期恒为 0）
///   SetIndexBuffer  : pass = pass id, a = buffer offset,
///                     b = buffer.handle, ptr = buffer.data, c = IndexFormat
///   SetViewport     : pass = pass id, a = viewports 侧表起始下标（6 个 float）
///   DrawIndexed     : pass = pass id, a = index_count, b = base_vertex（int32 符号扩展）,
///                     c = instance_count, d = first_index
///   Draw            : pass = pass id, a = vertex_count, c = instance_count, d = first_vertex
struct GLCommand
{
    GLCommandType type = GLCommandType::BeginRenderPass;
    uint64_t pass = 0;
    uint64_t a = 0;
    uint64_t b = 0;
    uint32_t c = 0;
    uint32_t d = 0;
    void *ptr = nullptr;
};

/// @brief 一帧内多个 render pass 的完整命令序列。
struct GLCommandBuffer
{
    uint64_t id = 0;
    std::vector<GLCommand> commands;
    /// @brief BeginRenderPass 的附件信息，命令里存下标
    std::vector<GLPassInfo> passes;
    /// @brief 所有 bind group 的动态偏移拼接在一起，命令里存起始下标 + 数量
    std::vector<uint32_t> dynamic_offsets;
    /// @brief 所有 SetViewport 的参数（每条 6 个 float：x, y, w, h, min_depth, max_depth）
    std::vector<float> viewports;

    void clear()
    {
        commands.clear();
        passes.clear();
        dynamic_offsets.clear();
        viewports.clear();
    }
};

/// @brief 指令编码器：持有正在录制的命令缓冲与当前打开的 pass。
struct GLCommandEncoder
{
    uint64_t id = 0;
    GLCommandBuffer buffer;
    /// @brief 当前打开的 pass id，0 表示没有打开的 pass
    uint64_t open_pass = 0;
    uint64_t next_pass_id = 1;
};

/// @brief 延迟销毁操作（M7 使用）。当前 destroy_* 仍是立即释放。
struct GLDestroyOp
{
    /// @brief 0 = 无操作；后续按资源类型扩展
    uint32_t kind = 0;
    void *object = nullptr;
};

// =============================================================================
// 格式映射
// =============================================================================

/// @brief Format 对应的 GL 三元组 + 像素字节数。
///
/// 这里刻意只用 uint32_t，不把 <glad/gl.h> 引入内部头：
/// 单元测试因此可以直接包含本头文件验证映射表（不需要 GL 上下文）。
struct GLFormatInfo
{
    /// @brief glTexImage2D 的 internalformat（GLint）
    uint32_t internal_format = 0;
    /// @brief 外部数据格式（GLenum）
    uint32_t format = 0;
    /// @brief 外部数据类型（GLenum）
    uint32_t type = 0;
    /// @brief 紧密打包时每像素字节数（上传行宽计算用）
    uint32_t bytes_per_pixel = 0;
    bool is_depth = false;
};

// =============================================================================
// 资源对象（M2）
//
// 所有对象都由设备用 memnew/memdelete 管理，句柄的 data 指向它们。
// 句柄约定（见 rendering_device_refactor.h）：调用方不得释放，销毁后副本失效。
// =============================================================================

struct GLShaderModule
{
    uint32_t program = 0;
    uint32_t vertex_shader = 0;
    uint32_t fragment_shader = 0;
};

struct GLBuffer
{
    uint32_t buffer = 0;
    uint64_t size = 0;
    uint8_t usage = 0;
};

/// @brief 纹理对象。cubemap 的 layers = 6，普通 2D 纹理 layers = 1。
struct GLTexture
{
    uint32_t texture = 0;
    TextureDimension dim = TextureDimension::Texture2D;
    Format format = Format::Undefined;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t mip_levels = 1;
    /// @brief 数组层数：2D 为 1，Cube 为 6
    uint32_t layers = 1;
    uint8_t usage = 0;
};

/// @brief 纹理视图。GL 3.3 没有 glTextureView，这里退化为"逻辑包装"：
/// 只记录 mip/layer/aspect 范围，绑定与附件时由执行器按范围下发。
struct GLTextureView
{
    uint64_t texture_handle = 0;
    GLTexture *texture = nullptr;
    TextureViewDimension dim = TextureViewDimension::Texture2D;
    uint32_t base_mip = 0;
    /// @brief 已解析的 mip 层数（创建 view 时把 0 解析成"到最后一层"）
    uint32_t mip_count = 1;
    uint32_t base_layer = 0;
    uint32_t layer_count = 1;
    TextureAspect aspect = TextureAspect::Color;
};

struct GLSampler
{
    uint32_t sampler = 0;
    SamplerDesc desc;
};

/// @brief 绑定组布局：只保存声明。物理位置（uniform block binding point /
/// texture unit）不在这里分配，因为同一个布局可能被多个 pipeline layout 共用。
struct GLBindGroupLayout
{
    std::vector<BindGroupLayoutEntry> entries;
};

/// @brief pipeline layout 为某个布局项分配的物理位置。
struct GLPipelineResource
{
    uint32_t group = 0;
    uint32_t binding = 0;
    BindingType type = BindingType::UniformBuffer;
    /// @brief GLSL 里的名字（uniform block 名 / sampler uniform 名）
    std::string name;
    /// @brief UniformBuffer：uniform block binding point；
    ///        纹理项：texture unit；Sampler：不占独立位置（恒为 0）
    uint32_t index = 0;
};

/// @brief pipeline layout：按 group 下标保存布局 + 全局唯一的物理位置分配。
struct GLPipelineLayout
{
    std::vector<GLBindGroupLayout *> groups;
    /// @brief 所有组的资源项，顺序为 (group 升序, 布局项声明顺序)
    std::vector<GLPipelineResource> resources;
    /// @brief 每个组占用的纹理单元范围（组内 Sampler 要绑到这些单元上）
    std::vector<uint32_t> group_unit_begin;
    std::vector<uint32_t> group_unit_count;
    uint32_t uniform_buffer_count = 0;
    uint32_t texture_unit_count = 0;
};

/// @brief 绑定组里的单项资源快照。创建时已按布局校验过，回放时直接使用。
struct GLBindGroupBinding
{
    uint32_t binding = 0;
    BindingType type = BindingType::UniformBuffer;
    GPUBufferHandle buffer;
    TextureViewHandle view;
    SamplerHandle sampler;
    uint64_t offset = 0;
    /// @brief UniformBuffer 的绑定字节数；0 表示从 offset 到缓冲区末尾
    uint64_t size = 0;
};

struct GLBindGroup
{
    GLBindGroupLayout *layout = nullptr;
    /// @brief 与 layout->entries 同序（创建时已保证一一对应）
    std::vector<GLBindGroupBinding> bindings;
};

/// @brief 渲染管线：完整状态包 + 名字解析结果。
/// shader program 的所有权属于 ShaderModule，管线不拥有它，也不负责删除。
struct GLRenderPipeline
{
    uint32_t program = 0;
    ShaderHandle vertex_module;
    ShaderHandle fragment_module;
    PipelineLayoutHandle pipeline_layout_handle;
    GLPipelineLayout *pipeline_layout = nullptr;
    Primitive primitive;
    DepthStencilState depth_stencil;
    std::vector<ColorTargetState> color_targets;
    std::vector<BufferLayout> vertex_buffers;
    uint32_t samples_count = 1;
    /// @brief 与 pipeline_layout->resources 同序的名字解析结果
    struct Resolved
    {
        /// @brief UniformBuffer：glGetUniformBlockIndex 的结果
        uint32_t uniform_block = 0;
        /// @brief 纹理项：glGetUniformLocation 的结果（-1 表示 shader 未使用，已被优化掉）
        int32_t uniform_location = -1;
    };
    std::vector<Resolved> resolved;
};

// =============================================================================
// 校验与映射（纯逻辑，不产生 GL 调用，单元测试可直接覆盖）
// =============================================================================

/// @brief 查询格式映射；Undefined 或未知格式返回 false。
bool gl_format_info(Format format, GLFormatInfo &out);

/// @brief 该格式是否为深度格式。
bool gl_format_is_depth(Format format);

/// @brief 校验纹理描述。失败时把原因写入 error。
bool gl_validate_texture_desc(const TextureDescRefactor &desc, std::string &error);

/// @brief 校验纹理视图描述与所属纹理是否匹配。
bool gl_validate_texture_view_desc(const GLTexture &texture, const TextureViewDesc &desc, std::string &error);

/// @brief 校验采样器描述。
bool gl_validate_sampler_desc(const SamplerDesc &desc, std::string &error);

/// @brief 校验渲染管线描述（只校验描述层面，不检查 shader 里的名字是否存在）。
bool gl_validate_render_pipeline_desc(const RenderPipelineDesc &desc, std::string &error);
