#include "vk_rendering_device.h"
#include "os/memory.h"


RenderPipelineHandle VkRenderingDevice::create_render_pipeline(const RenderPipelineDesc &desc)
{
    vk::GraphicsPipelineCreateInfo create_info;
    return {};
}

void VkRenderingDevice::destroy_render_pipeline(RenderPipelineHandle pipeline)
{
    (void)pipeline;
}