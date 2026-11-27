#pragma once
#include "rendering_device_refactor.h"

class RenderingServer;
struct DrawItem;

/// @brief 多个render pass共享的上下文
struct RenderPassContext
{
    /// @brief 指令录制
    CommandEncoderHandle encoder;
    /// @brief 帧全局uniform buffer
    GPUBufferHandle frame_uniform_buffer;
    /// @brief 物体共享uniform buffer
    GPUBufferHandle objects_uniform_buffer;
    /// @brief 帧全局和物体uniform buffer绑定组
    BindGroupHandle frame_obj_bind_group;
    /// @brief 物体和帧全局信息的bindGroup布局
    BindGroupLayoutHandle frame_objects_layout;
    /// @brief 主渲染管线的材质bindGroup布局
    BindGroupLayoutHandle material_layout;
    /// @brief 物体uniform的数据跨度
    uint32_t object_uniform_stride = 0;

    // G-Buffer
    
    /// @brief g-buffer position
    TextureViewHandle g_position;
    /// @brief g-buffer normal
    TextureViewHandle g_normal;
    /// @brief g-buffer metallic and roughness
    TextureViewHandle g_metallic_roughness;
    /// @brief g-buffer albedo
    TextureViewHandle g_albedo;
};

class RenderingPass
{
public:
    virtual void init(const RenderPassContext &context) = 0;
    virtual void process(const RenderPassContext &context, const std::vector<DrawItem> &draw_items, void *custom_data) = 0;
    virtual void destroy() = 0;
};

class RenderingMainPass: public RenderingPass
{
public:
    void init(const RenderPassContext &context) override;
    void process(const RenderPassContext &context, const std::vector<DrawItem> &draw_items, void *custom_data) override;
    void destroy() override;
private:
    PipelineLayoutHandle main_pipeline_layout;
    RenderPipelineHandle main_pipeline;
    ShaderHandle main_vertex_shader;
    ShaderHandle main_fragment_shader;
};

class RenderingDepthMapPass: public RenderingPass
{
public:
    void init(const RenderPassContext &context) override;
    void process(const RenderPassContext &context, const std::vector<DrawItem> &draw_items, void *custom_data) override;
    void destroy() override;
private:
    RenderPipelineHandle depthmap_pipeline;
    ShaderHandle vertex_shader;
    ShaderHandle fragment_shader;
};

class RenderingGBufferPass: public RenderingPass
{
public:
    void init(const RenderPassContext &context) override;
    void process(const RenderPassContext &context, const std::vector<DrawItem> &draw_items, void *custom_data) override;
    void destroy() override;
private:
    PipelineLayoutHandle pipeline_layout;
    RenderPipelineHandle pipeline;
    ShaderHandle vertex_shader;
    ShaderHandle fragment_shader;
};