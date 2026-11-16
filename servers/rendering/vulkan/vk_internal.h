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

struct VkTextureHandleData
{
    vk::raii::Image image { nullptr };
    /// @brief 创建时保存的描述，供 create_texture_view 校验与格式映射使用
    Format format = Format::Undefined;
    TextureDimension dim = TextureDimension::Texture2D;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t mip_levels = 1;
    /// @brief 数组层数：2D 为 1，Cube 为 6
    uint32_t layers = 1;
};

struct VkTextureViewHandleData
{
    vk::raii::ImageView image_view { nullptr };
};

struct VkSamplerHandleData
{
    vk::raii::Sampler sampler { nullptr };
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
