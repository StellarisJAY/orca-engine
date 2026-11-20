#pragma once

#include "rendering_device_refactor.h"
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>
#include <cstdint>
#include <functional>
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

/// @brief 交换链支持情况 
struct SwapChainSupportDetails {
    vk::SurfaceCapabilitiesKHR capabilities;
    std::vector<vk::SurfaceFormatKHR>  formats;
    std::vector<vk::PresentModeKHR> presentModes;
};

/// @brief Vulkan 后端。
///
/// 当前状态（2026-11）：V0/V1/V2/V4 已完成（构建、唯一句柄、异常处理、格式映射、
/// 描述校验、显存/纹理/缓冲/视图/采样器、SPIR-V 工具链）；V3-1 完成（交换链
/// image view、每图像深度、默认 render pass/framebuffer、acquire/present 同步对象）；
/// I-04 完成（`resize_swapchain` 用 oldSwapchain 重建全部依赖资源）；I-31 完成
/// `encoder_begin_render_pass` 的 render pass 创建（完整附件/subpass/dependency、
/// 尺寸推导、默认帧缓冲分支，pass 内部对象随 command buffer 释放）；V5-1/V5-2 完成
/// 绑定组布局、绑定组（独占 descriptor pool + descriptor set）与管线布局，
/// `create_render_pipeline` 改为使用调用方传入的 `pipeline_layout`。
/// V6-2 完成 pass 级绑定：`pass_set_bind_group`（含动态偏移校验）、
/// `pass_set_vertex_buffer`、`pass_set_index_buffer`、`pass_set_viewport`
/// （同时派生并裁剪 scissor，pass 开始时设默认 viewport/scissor）。
/// `pass_draw`/`pass_draw_indexed`、`queue_submit`、`present` 仍未实现。
/// 进度见 docs/vulkan_rhi_checklist_v2.md。
class VkRenderingDevice : public RenderingDeviceRefactor
{
private:
    RenderingWindowInfo window_info;
    uint64_t frame_index = 0;
    /// @brief 非原生对象（encoder / pass / command buffer）的句柄计数器
    uint64_t next_handle_id = 1;
    vk::raii::Context context;
    vk::raii::Instance instance { nullptr };
    vk::raii::DebugUtilsMessengerEXT debug_messenger { nullptr };
    vk::raii::PhysicalDevice physical_device { nullptr };
    /// @brief 物理设备内存属性，init_device 时缓存，供 vk_memory 显存分配使用
    vk::PhysicalDeviceMemoryProperties memory_properties {};
    vk::PhysicalDeviceFeatures device_features;
    vk::raii::Device device { nullptr };
    vk::raii::Queue graphics_queue { nullptr };
    vk::raii::Queue present_queue { nullptr };
    vk::raii::SurfaceKHR surface { nullptr };
    vk::raii::SwapchainKHR swapchain { nullptr };

    vk::Format swapchain_image_format{};
    vk::Extent2D swapchain_image_extent{};
    std::vector<vk::Image> swapchain_images;

    // ---- 交换链依赖资源 -------------
    //
    // 声明顺序约定：所有引用交换链 image 的对象（image view / 深度 / framebuffer）
    // 必须声明在 swapchain 之后，这样逆序析构时它们先于 swapchain 释放
    // （Vulkan 要求 view 先于 image、framebuffer 先于 render pass 销毁）。
    static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;

    /// @brief 每张交换链图像的 color image view（默认帧缓冲的颜色附件）。
    std::vector<vk::raii::ImageView> swapchain_image_views;

    /// @brief 每张交换链图像独占一份深度缓冲（G-07）。
    ///
    /// 按 image 而不是全局一份：多帧在飞时不同交换链图像的深度内容互不干扰，
    /// framebuffer 也能与 render pass 一一配对。
    struct SwapchainDepth
    {
        // 声明顺序保证逆序析构：view → image → memory。
        vk::raii::DeviceMemory memory { nullptr };
        vk::raii::Image image { nullptr };
        vk::raii::ImageView view { nullptr };
        vk::DeviceSize allocation_size = 0;
    };
    std::vector<SwapchainDepth> swapchain_depths;
    /// @brief 内部深度缓冲实际使用的 Vulkan 格式（由设备支持能力选择）。
    vk::Format swapchain_depth_format { vk::Format::eUndefined };

    /// @brief 默认帧缓冲的 render pass：color = 交换链 image，depth = 内部深度。
    ///
    /// 经典 render pass 方案（G-09）；管线与 framebuffer 通过它配对。
    vk::raii::RenderPass swapchain_render_pass { nullptr };
    /// @brief 每张交换链图像一个 framebuffer。
    std::vector<vk::raii::Framebuffer> swapchain_framebuffers;

    /// @brief 每帧在飞的 acquire/present 同步对象（V3-2 / D-24 的基础设施）。
    ///
    /// `queue_submit` 目前仍是桩，V3-2/V6 接线后由它使用：
    /// acquire 时 signal `image_available`、submit 时等待它并 signal
    /// `render_finished`、present 时等待 `render_finished`；`in_flight` 初始为
    /// signaled，用于限制 CPU 超前后复用同一组对象。
    struct FrameSync
    {
        vk::raii::Semaphore image_available { nullptr };
        vk::raii::Semaphore render_finished { nullptr };
        vk::raii::Fence in_flight { nullptr };
    };
    std::vector<FrameSync> frame_syncs;

    /// @brief 当前已 acquire 的交换链图像索引；`UINT32_MAX` 表示尚未 acquire。
    uint32_t current_swapchain_image = UINT32_MAX;
    /// @brief 当前使用的 `frame_syncs` 下标。
    uint32_t current_frame = 0;
private:
    void create_instance();
    void create_debug_messenger();
    bool create_surface();
    bool select_physical_device();
    QueueFamilyIndices findQueueFamilies(const vk::raii::PhysicalDevice &physicalDevice) const;
    bool create_logical_device();
    bool is_physical_device_suitable(const vk::raii::PhysicalDevice &device);
    bool check_validation_layer_support() const;
    SwapChainSupportDetails query_swapchain_support(const vk::raii::PhysicalDevice& physicalDevice) const;
    vk::Extent2D choose_swap_extent(const vk::SurfaceCapabilitiesKHR &capabilities) const;
    uint64_t alloc_handle_id() { return next_handle_id++; }

    // ---- 交换链依赖资源的创建/释放 ---------------------------

    /// @brief 为每张交换链图像创建 color image view。
    bool create_swapchain_image_views();
    /// @brief 选择设备支持的深度格式，为每张交换链图像创建深度 image/view。
    bool create_swapchain_depth_buffers();
    /// @brief 创建默认帧缓冲的 render pass，并为每张交换链图像创建 framebuffer。
    bool create_swapchain_render_pass_and_framebuffers();
    /// @brief 创建 MAX_FRAMES_IN_FLIGHT 组 acquire/present 同步对象。
    bool create_frame_sync_objects();
    /// @brief 释放所有依赖交换链的资源（保留 swapchain 本身，供重建时传 oldSwapchain）。
    ///
    /// 释放顺序：framebuffer → render pass → 深度 view/image/memory → 颜色 view。
    /// 这些对象必须先于 swapchain 释放，否则违反 Vulkan 的父子对象销毁约束。
    void destroy_swapchain_dependents();

    /// @brief 复用的 host-visible staging buffer（缓冲区上传用）。
    ///
    /// 容量按最大的写入请求扩容（向上取整到 4 KiB），避免每次 queue_write_buffer
    /// 都重新分配一块 VkBuffer + VkDeviceMemory。queue_write_buffer 是"提交 + fence
    /// 等待"的同步语义，调用返回时上一次拷贝已经完成，因此同一块 staging 可以在下
    /// 一次写入时安全复用。
    ///
    /// 注意成员声明顺序：staging 必须声明在 device 之后，保证它与 device 的析构顺序
    /// （staging 先销毁）正确。
    struct StagingBuffer
    {
        // 声明顺序保证逆序析构：buffer 先于它绑定的 memory 释放。
        vk::raii::DeviceMemory memory { nullptr };
        vk::raii::Buffer buffer { nullptr };
        uint64_t capacity = 0;
    };
    StagingBuffer staging_buffer;

    /// @brief 保证 staging buffer 容量不小于 size；需要时重建（旧 buffer 先于旧 memory 释放）。
    bool ensure_staging_capacity(uint64_t size, const char *what);

    /// @brief 记录一组命令并立即提交到图形队列，阻塞到 fence 信号。
    ///
    /// 供 queue_write_texture / generate_mip_map 这类"一次性上传"使用：内部创建
    /// 临时命令池 + command buffer + fence，提交后等待完成再返回，因此调用返回时
    /// GPU 已经执行完这些命令。它不参与帧提交（queue_submit 的 semaphore/fence 链），
    /// 但与之前录制的命令保证先于之后提交的帧生效（Vulkan 队列按提交顺序执行）。
    ///
    /// @param what 出错时打印的操作名
    /// @param record 录制回调，参数是已 begin 的 command buffer
    /// @return 成功?
    bool submit_immediate_commands(const char *what,
                                   const std::function<void(const vk::raii::CommandBuffer &command_buffer)> &record);
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
