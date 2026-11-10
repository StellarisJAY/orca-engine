#pragma once

#include "rendering_device_refactor.h"
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>
#include <optional>

struct QueueFamilyIndices
{
    std::optional<uint32_t> graphicsFamily;
    std::optional<uint32_t> presentFamily;

    bool isComplete() const
    {
        return graphicsFamily.has_value() && presentFamily.has_value();
    }
};

/// @brief Vulkan 后端（骨架）。
/// 目前只保留接口签名，具体实现待 Vulkan 路径真正落地时补齐。
class VkRenderingDevice : public RenderingDeviceRefactor
{
private:
    RenderingWindowInfo window_info;
    uint64_t frame_index = 0;
    vk::raii::Context context;
    vk::raii::Instance instance { nullptr };
    vk::raii::PhysicalDevice physical_device { nullptr };
    vk::PhysicalDeviceFeatures device_features;
    vk::raii::Device device { nullptr };
    vk::raii::Queue graphics_queue { nullptr };
    vk::raii::Queue present_queue { nullptr };
    vk::raii::SurfaceKHR surface { nullptr };
private:
    void create_instance();
    void create_debug_messenger();
    bool create_surface();
    bool select_physical_device();
    QueueFamilyIndices findQueueFamilies(const vk::raii::PhysicalDevice &physicalDevice) const;
    bool create_logical_device();
    bool is_physical_device_suitable(const vk::raii::PhysicalDevice &device);
public:
    VkRenderingDevice() = default;
    ~VkRenderingDevice() override = default;

    // ------------------------ 初始化流程 -------------------------------------

    bool init(const RenderingWindowInfo &info) override;
    bool init_device() override;
    bool init_swapchain() override;
    void resize_swapchain(uint32_t width, uint32_t height) override;
    Format get_swapchain_format() const override;
    void get_swapchain_size(uint32_t &width, uint32_t &height) const override;
    void present() override;
    void shutdown() override;

    // ------------------------- Shader ---------------------------------------

    ShaderHandle create_shader_module(const ShaderModuleDesc &desc) override;
    void destroy_shader_module(ShaderHandle handle) override;

    // ------------------------- 缓冲区 ---------------------------------------

    GPUBufferHandle create_buffer(const GPUBufferDesc &desc) override;
    void queue_write_buffer(GPUBufferHandle handle, uint64_t offset, const void *data, uint64_t size) override;
    void destroy_buffer(GPUBufferHandle handle) override;

    // ------------------------- 纹理 / sampler --------------------------------

    TextureHandle create_texture(const TextureDescRefactor &desc) override;
    TextureViewHandle create_texture_view(TextureHandle texture, const TextureViewDesc &desc) override;
    SamplerHandle create_sampler(const SamplerDesc &desc) override;
    void queue_write_texture(TextureHandle texture, const TextureWriteDesc &desc) override;
    void generate_mip_map(TextureHandle texture) override;
    void destroy_texture(TextureHandle texture) override;
    void destroy_texture_view(TextureViewHandle view) override;
    void destroy_sampler(SamplerHandle sampler) override;

    // ------------------------- 绑定组 / 管线布局 ------------------------------

    BindGroupLayoutHandle create_bind_group_layout(const BindGroupLayoutDesc &desc) override;
    BindGroupHandle create_bind_group(const BindGroupDesc &desc) override;
    PipelineLayoutHandle create_pipeline_layout(const PipelineLayoutDesc &desc) override;
    void destroy_bind_group(BindGroupHandle bind_group) override;
    void destroy_bind_group_layout(BindGroupLayoutHandle layout) override;
    void destroy_pipeline_layout(PipelineLayoutHandle layout) override;

    // ------------------------- 渲染管线 -------------------------------------

    RenderPipelineHandle create_render_pipeline(const RenderPipelineDesc &desc) override;
    void destroy_render_pipeline(RenderPipelineHandle pipeline) override;

    // ------------------------- command & render pass -------------------------

    CommandEncoderHandle create_command_encoder() override;
    RenderPassHandle encoder_begin_render_pass(CommandEncoderHandle encoder, const RenderPassDesc &desc) override;
    void pass_set_pipeline(RenderPassHandle pass, RenderPipelineHandle pipeline) override;
    void pass_set_bind_group(RenderPassHandle pass, uint32_t group_index, BindGroupHandle bind_group,
                             const uint32_t *dynamic_offsets = nullptr, uint32_t dynamic_offset_count = 0) override;
    void pass_set_vertex_buffer(RenderPassHandle pass, uint32_t slot, GPUBufferHandle buffer, uint64_t offset = 0) override;
    void pass_set_index_buffer(RenderPassHandle pass, GPUBufferHandle buffer, uint64_t offset = 0,
                               IndexFormat format = IndexFormat::Uint32) override;
    void pass_set_viewport(RenderPassHandle pass, float x, float y, float width, float height,
                           float min_depth = 0.0f, float max_depth = 1.0f) override;
    void pass_draw_indexed(RenderPassHandle pass, uint32_t index_count, uint32_t instance_count = 1,
                           uint32_t first_index = 0, int32_t base_vertex = 0) override;
    void pass_draw(RenderPassHandle pass, uint32_t vertex_count, uint32_t instance_count = 1,
                   uint32_t first_vertex = 0) override;
    void pass_end(RenderPassHandle pass) override;
    CommandBufferHandle encoder_finish(CommandEncoderHandle encoder) override;
    void destroy_command_buffer(CommandBufferHandle cmd) override;

    // ------------------------- 设备指令队列 ----------------------------------

    void queue_submit(CommandBufferHandle cmd) override;
    void queue_wait_idle() override;
    uint64_t submit_frame() override;
};
