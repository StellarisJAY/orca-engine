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
};