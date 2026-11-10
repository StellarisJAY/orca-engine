#pragma once

#include "rendering_device_refactor.h"

class VkRenderingDeviceRefactor : public RenderingDeviceRefactor
{
public:
    VkRenderingDeviceRefactor() = default;
    ~VkRenderingDeviceRefactor() = default;
public:
    // ------------------------ 初始化流程 -------------------------------------

    /// @brief 初始化渲染后端
    /// @param info 窗口信息
    /// @return 成功?
    bool init(const RenderingWindowInfo &info) override;

    /// @brief 初始化逻辑设备(物理设备集合->唯一逻辑设备)
    /// @return 成功?
    bool init_device() override;

    /// @brief 初始化交换链
    /// @return 成功?
    bool init_swapchain() override;

    void shutdown() override;

    // ------------------------- Shader ---------------------------------------

    /// @brief 创建shader
    /// @param desc shader描述+代码
    /// @return shader句柄
    ShaderHandle create_shader_module(const ShaderModuleDesc &desc) override;
    /// @brief 销毁shader
    /// @param handle
    void destroy_shader_module(ShaderHandle handle) override;

    // ------------------------- 缓冲 -----------------------------------------

    /// @brief 创建GPU缓冲区
    /// @param desc 缓冲区描述
    /// @return handle
    GPUBufferHandle create_buffer(const GPUBufferDesc &desc) override;

    /// @brief 写入GPU缓冲区
    /// @param handle 缓冲区handle
    /// @param offset 偏移
    /// @param data 数据
    void queue_write_buffer(GPUBufferHandle handle, uint64_t offset, float *data) override;

    // ------------------------- 绑定组 ------------------------------------

    /// @brief 创建绑定组
    /// @return
    BindGroupLayoutHandle create_bind_group_layout() override;

    // ------------------------- 纹理、sampler ----------------------------------------

    /// @brief 创建纹理
    /// @param desc 纹理描述
    /// @return 纹理hanle
    TextureHandle create_texture(const TextureDesc &desc) override;
    /// @brief 创建纹理view
    /// @param texture 纹理handle
    /// @param desc 视图描述
    /// @return 视图handle
    TextureViewHandle create_texture_view(TextureHandle texture, const TextureViewDesc &desc) override;
    SamplerHandle create_sampler(const SamplerDesc &desc) override;
    void queue_write_texture(TextureHandle texture, const TextureWriteDesc &desc) override;
    void generate_mip_map(TextureHandle texture) override;

    // ------------------------- 渲染管线 ----------------------------------------

    /// @brief 创建渲染管线
    /// @brief 渲染管线负责管理buffer的layout，使用什么shader程序，输出到什么格式
    /// @brief 渲染管现在绘制前创建，绘制时绑定管线
    /// @return 句柄
    RenderPipelineHandle create_render_pipeline(const RenderPipelineDesc &desc) override;

    // ------------------------- command & render pass --------------------------

    /// @brief 创建指令编码器
    /// @return 句柄
    CommandEncoderHandle create_command_encoder() override;

    /// @brief 开启渲染通道指令录制
    /// @brief 一个渲染流程 = 多个渲染通道的指令录制
    /// @brief eg: 深度贴图pass->PBR pass->后处理pass
    /// @param encoder 编码器handle
    /// @param desc 渲染通道描述
    /// @return render pass句柄
    RenderPassHandle encoder_begin_render_pass(CommandEncoderHandle encoder, const RenderPassDesc &desc) override;

    /// @brief 设置渲染通道的渲染管线
    /// @param pass 通道handle
    /// @param pipeline 管线handle
    void pass_set_pipeline(RenderPassHandle pass, RenderPipelineHandle pipeline) override;

    void pass_set_bind_group(RenderPassHandle pass) override;

    /// @brief 设置槽位的缓冲区
    /// @param pass 渲染通道handle
    /// @param slot layout槽位
    /// @param buffer 缓冲区handle
    void pass_set_vertex_buffer(RenderPassHandle pass, uint8_t slot, GPUBufferHandle buffer) override;

    /// @brief 设置索引缓冲区
    /// @param pass 渲染通道handle
    /// @param buffer 缓冲区handle
    void pass_set_index_buffer(RenderPassHandle pass, GPUBufferHandle buffer) override;

    /// @brief 设置viewport
    /// @param pass 渲染通道handle
    void pass_set_viewport(RenderPassHandle pass, uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint32_t min_depth, uint32_t max_depth) override;

    /// @brief 渲染通道绘制 draw call
    /// @param pass 渲染通道handle
    /// @param indices_length 索引数量
    void pass_draw_indexed(RenderPassHandle pass, uint64_t indices_length) override;

    /// @brief 一个渲染通道结束，之后可能结束指令录制也可能开始另一个渲染通道
    /// @param pass 渲染通道handle
    void pass_end(RenderPassHandle pass) override;

    /// @brief 结束指令录制拿到编码后的command buffer
    /// @param encoder 指令编码器
    /// @return CommandBuffer句柄
    CommandBufferHandle encoder_finish(CommandEncoderHandle encoder) override;

    // ------------------------- 设备指令队列 --------------------------------------------

    /// @brief 提交录制的指令buffer
    /// @param cmd CommandBuffer handle
    void queue_submit(CommandBufferHandle cmd) override;
    void queue_wait_idle() override;
    uint64_t submit_frame() override;
    void defer_destroy() override;
};