#pragma once

#include "vk_format.h"
#include "vk_rendering_device.h"

#include <vector>

// =============================================================================
// Vulkan 后端私有对象（句柄的 data 字段指向这些结构，调用方不得解引用/释放）。
//
// 约定：
//   - 所有对象都由设备用 memnew/memdelete 管理；
//   - 含 vk::raii 成员的对象在 memdelete 时自动释放原生句柄；
//   - 需要"父对象先于子对象销毁"的依赖（command buffer → command pool、
//     image view → image），依靠成员声明顺序保证析构逆序正确。
// =============================================================================

struct VkRenderPassHandleData;

struct VkShaderModuleData
{
    vk::raii::ShaderModule module { nullptr };
    ShaderType type = ShaderType::VERTEX;
    /// @brief 创建时校验过的 SPIR-V 入口函数名（管线阶段可回退到它）
    std::string entry_point = "main";
};

struct VkBufferHandleData
{
    // 声明顺序保证逆序析构：buffer 先于它绑定的 memory 释放（Vulkan 要求先销毁
    // VkBuffer，再释放它占用的 VkDeviceMemory）。
    vk::raii::DeviceMemory memory { nullptr };
    vk::raii::Buffer buffer { nullptr };
    /// @brief 本次独占分配的大小，便于日志与后续显存统计
    vk::DeviceSize allocation_size = 0;
    /// @brief 缓冲区字节数（queue_write_buffer 据此校验写入区间）
    uint64_t size = 0;
    /// @brief 创建时的 usage 位掩码：queue_write_buffer 据此校验是否带 COPY_DST
    uint8_t usage = 0;
    /// @brief 调试用标签（创建 desc.label 的副本，供写入日志）
    std::string label;
};

struct VkTextureHandleData
{
    // 声明顺序保证逆序析构：image 先于 memory 释放（Vulkan 要求先销毁 image，
    // 再释放它绑定的 VkDeviceMemory）。
    vk::raii::DeviceMemory memory { nullptr };
    vk::raii::Image image { nullptr };
    /// @brief 本次独占分配的大小，便于日志与后续显存统计
    vk::DeviceSize allocation_size = 0;
    /// @brief 创建时保存的描述，供 create_texture_view 校验与格式映射使用
    Format format = Format::Undefined;
    TextureDimension dim = TextureDimension::Texture2D;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t mip_levels = 1;
    /// @brief 数组层数：2D 为 1，Cube 为 6
    uint32_t layers = 1;
    /// @brief 创建时的 usage 位掩码：queue_write_texture / generate_mip_map 据此校验
    ///        image 是否真的带 TRANSFER_SRC/DST 用途（VkImage 不记录这个信息）。
    uint8_t usage = 0;
    /// @brief 纹理静止阶段的布局，由 vk_image_resting_layout(format, usage) 推导。
    vk::ImageLayout resting_layout { vk::ImageLayout::eUndefined };
    /// @brief 整张图当前所处的布局；Vulkan 的 layout 是显式状态，每次切换后更新。
    vk::ImageLayout current_layout { vk::ImageLayout::eUndefined };
    /// @brief 调试用标签（创建 desc.label 的副本，供上传/mipmap 日志）
    std::string label;
};


struct VkTextureViewHandleData
{
    vk::raii::ImageView image_view { nullptr };
    /// @brief 父纹理（非拥有指针）。
    ///
    /// render pass 需要读取父纹理的实际布局（Vulkan 的 layout 是显式状态，不能按视图
    /// 猜测）；因此调用方必须先销毁视图、再销毁父纹理，否则这里会悬空。
    VkTextureHandleData *texture = nullptr;
    /// @brief 视图 base_mip 对应的像素尺寸（framebuffer 与 render area 推导用）
    uint32_t width = 0;
    uint32_t height = 0;
    /// @brief 父纹理的调试标签（日志用）
    std::string label;
    /// @brief 父纹理格式；视图不重解释格式（sRGB ↔ 非 sRGB 视图本期不支持）
    Format format = Format::Undefined;
    /// @brief 已解析的视图维度（Texture2D / Cube）
    TextureViewDimension dim = TextureViewDimension::Texture2D;
    TextureAspect aspect = TextureAspect::Color;
    /// @brief 已解析的 mip 范围（desc 里的 0 已展开成真实层数）
    uint32_t base_mip = 0;
    uint32_t mip_count = 1;
    /// @brief 已解析的层范围（cubemap 固定 base 0 / count 6）
    uint32_t base_layer = 0;
    uint32_t layer_count = 1;
};

struct VkSamplerHandleData
{
    vk::raii::Sampler sampler { nullptr };
    /// @brief 创建时的描述（含 compare_op），供绑定组校验与调试日志使用
    SamplerDesc desc;
};

/// @brief 创建 bind group layout 时缓存的布局项（供 create_bind_group 校验）。
struct VkBindGroupLayoutEntryData
{
    uint32_t binding = 0;
    BindingType type = BindingType::UniformBuffer;
    uint8_t visibility = ShaderStage::FRAGMENT;
    uint64_t min_binding_size = 0;
    bool has_dynamic_offset = false;
};

struct VkBindGroupLayoutHandleData
{
    vk::raii::DescriptorSetLayout descriptor_set_layout { nullptr };
    /// @brief 创建时校验过的布局项；create_bind_group / create_pipeline_layout 复用
    std::vector<VkBindGroupLayoutEntryData> entries;
    std::string label;
};

/// @brief bind group 的一项已绑定资源。
///
/// `pass_set_bind_group`（V6）需要用这些信息校验动态偏移并记录 descriptor set 的
/// 可见性/布局状态，因此 bind group 必须保留资源的非拥有指针。
struct VkBindGroupBindingData
{
    uint32_t binding = 0;
    BindingType type = BindingType::UniformBuffer;
    /// @brief UniformBuffer：缓冲区（非拥有指针）
    VkBufferHandleData *buffer = nullptr;
    /// @brief UniformBuffer：已解析的 [offset, offset + size)，size 不再是 0
    uint64_t offset = 0;
    uint64_t size = 0;
    /// @brief UniformBuffer：是否允许动态偏移（来自布局项）
    bool has_dynamic_offset = false;
    /// @brief 纹理绑定：纹理视图（非拥有指针）
    VkTextureViewHandleData *view = nullptr;
    /// @brief 纹理绑定：descriptor 使用的 image layout（采样时纹理必须已 transition 到它）
    vk::ImageLayout image_layout { vk::ImageLayout::eUndefined };
    /// @brief 纹理绑定 / Sampler 绑定：采样器（非拥有指针）
    VkSamplerHandleData *sampler = nullptr;
};

struct VkBindGroupHandleData
{
    // 声明顺序保证逆序析构：descriptor set 先于它所属的 pool 释放。
    vk::raii::DescriptorPool descriptor_pool { nullptr };
    vk::raii::DescriptorSet descriptor_set { nullptr };
    /// @brief 创建时使用的布局（非拥有指针）；布局必须先于 bind group 销毁。
    VkBindGroupLayoutHandleData *layout = nullptr;
    /// @brief 每个 binding 对应的资源与已解析区间
    std::vector<VkBindGroupBindingData> bindings;
    std::string label;
};

struct VkPipelineLayoutHandleData
{
    vk::raii::PipelineLayout pipeline_layout { nullptr };
    /// @brief 按 group index 对应的 bind group layout（非拥有指针）。
    ///        布局必须先于管线布局销毁。
    std::vector<VkBindGroupLayoutHandleData *> bind_group_layouts;
    std::string label;
};

struct VkGraphicsPipelineHandleData
{
    // 声明顺序保证逆序析构：pipeline 先于它创建时使用的兼容 render pass 释放。
    //
    // G-09 决策：录制侧走经典 VkRenderPass 对象（I-31），管线不能传 nullptr renderPass，
    // 也不能依赖 dynamic rendering。每个管线创建并持有一个只用于"兼容性"的 render pass：
    // 附件格式/采样数/subpass 引用与真正录制的 pass 一致，load/store 与 initial/final
    // layout 不参与兼容性判定（Vulkan 规范把这四项排除在兼容性之外），因此它从不会
    // 被真正 begin，只用于管线创建与更新 validation 的兼容性检查。
    vk::raii::RenderPass compatibility_render_pass { nullptr };
    vk::raii::Pipeline pipeline { nullptr };
    /// @brief 创建管线时传入的 pipeline layout（非拥有；由调用方保证生命周期）
    VkPipelineLayoutHandleData *pipeline_layout = nullptr;
    /// @brief 调试用标签（创建 desc.label 的副本）
    std::string label;
};

/// @brief render pass 结束后需要把父纹理切换到的新布局。
struct VkRenderPassLayoutUpdate
{
    VkTextureHandleData *texture = nullptr;
    vk::ImageLayout final_layout = vk::ImageLayout::eUndefined;
};

/// @brief command encoder：持有命令池、正在录制的 command buffer 与本次录制创建的 pass。
struct VkCommandEncoderHandleData
{
    // 声明顺序保证销毁时 command_buffer 先于 command_pool 释放。
    vk::raii::CommandPool command_pool { nullptr };
    vk::raii::CommandBuffer command_buffer { nullptr };
    /// @brief 本 encoder 录制的 render pass/framebuffer；encoder_finish 时移交 command buffer，
    ///        由 destroy_command_buffer 统一释放（避免每个 pass 新建即泄漏）。
    std::vector<VkRenderPassHandleData *> render_passes;
};

struct VkRenderPassHandleData
{
    vk::raii::RenderPass render_pass { nullptr };
    vk::raii::Framebuffer framebuffer { nullptr };
    // 因为部分命令以 RenderPass 为参数，需要在 RenderPass 记录所属 CommandEncoder。
    // encoder_finish 之后置空（该 encoder 已失效）。
    VkCommandEncoderHandleData *command_encoder_data = nullptr;
    /// @brief pass_end 后要写回父纹理的布局（仅在录制被提交执行后才真实生效）。
    std::vector<VkRenderPassLayoutUpdate> layout_updates;
};

struct VkCommandBufferHandleData
{
    // encoder_finish 把命令池和 command buffer 一起转移到这里，保证命令池
    // 至少和 command buffer 活得一样久。
    vk::raii::CommandPool command_pool { nullptr };
    vk::raii::CommandBuffer command_buffer { nullptr };
    /// @brief 录制期间创建的 render pass/framebuffer；destroy_command_buffer 时释放。
    ///        调用约定：必须在命令缓冲提交并执行完之后再销毁。
    std::vector<VkRenderPassHandleData *> render_passes;
};
