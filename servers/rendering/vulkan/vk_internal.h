#pragma once

#include "vk_format.h"
#include "vk_rendering_device.h"

// =============================================================================
// Vulkan 后端私有对象（句柄的 data 字段指向这些结构，调用方不得解引用/释放）。
//
// 约定：
//   - 所有对象都由设备用 memnew/memdelete 管理；
//   - 含 vk::raii 成员的对象在 memdelete 时自动释放原生句柄；
//   - 需要"父对象先于子对象销毁"的依赖（command buffer → command pool、
//     image view → image），依靠成员声明顺序保证析构逆序正确。
// =============================================================================

struct VkShaderModuleData
{
    vk::raii::ShaderModule module { nullptr };
    ShaderType type = ShaderType::VERTEX;
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

struct VkGraphicsPipelineHandleData
{
    vk::raii::Pipeline pipeline { nullptr };
    vk::raii::PipelineLayout pipeline_layout { nullptr };
};

/// @brief 指令编码器：持有命令池和正在录制的 command buffer。
struct VkCommandEncoderHandleData
{
    // 声明顺序保证销毁时 command_buffer 先于 command_pool 释放。
    vk::raii::CommandPool command_pool { nullptr };
    vk::raii::CommandBuffer command_buffer { nullptr };
};

struct VkRenderPassHandleData
{
    vk::raii::RenderPass render_pass { nullptr };
    vk::raii::Framebuffer framebuffer { nullptr };
    // 因为部分命令以 RenderPass 为参数，需要在 RenderPass 记录所属 CommandEncoder。
    // 注意不要错误释放该指针。
    VkCommandEncoderHandleData *command_encoder_data = nullptr;
};

struct VkCommandBufferHandleData
{
    // encoder_finish 把命令池和 command buffer 一起转移到这里，保证命令池
    // 至少和 command buffer 活得一样久。
    vk::raii::CommandPool command_pool { nullptr };
    vk::raii::CommandBuffer command_buffer { nullptr };
};
