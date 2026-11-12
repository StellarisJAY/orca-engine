#pragma once

#include "vk_rendering_device.h"

struct VkShaderModuleData
{
    vk::raii::ShaderModule module { nullptr };
    ShaderType type;
};

struct VkTextureHandleData
{
    vk::raii::Image image{nullptr};
};

struct VkTextureViewHandleData
{
    vk::raii::ImageView image_view{nullptr};
};

struct VkSamplerHandleData
{
    vk::raii::Sampler sampler{nullptr};
};

struct VkGraphicsPipelineHandleData
{
    vk::raii::Pipeline pipeline { nullptr };
    vk::raii::PipelineLayout pipeline_layout { nullptr };
};

struct VkCommandEncoderHandleData
{
    vk::raii::CommandPool command_pool { nullptr };
    std::vector<vk::raii::CommandBuffer> command_buffers;
};

struct VkRenderPassHandleData
{
    vk::raii::RenderPass render_pass { nullptr };
    vk::raii::Framebuffer framebuffer { nullptr };
    // 因为部分命令以RenderPass为参数，需要在render pass记录所属CommandEncoder
    // 注意不要错误释放该指针
    VkCommandEncoderHandleData *command_encoder_data = nullptr;
};

struct VkCommandBufferHandleData
{
    std::vector<vk::raii::CommandBuffer> command_buffers;
};