#include "vk_rendering_device.h"
#include "vk_internal.h"
#include "os/memory.h"

vk::PrimitiveTopology get_vk_primitive_topology(Primitive::Topology orca_topology)
{

}

RenderPipelineHandle VkRenderingDevice::create_render_pipeline(const RenderPipelineDesc &desc)
{
    RenderPipelineHandle result;
    vk::GraphicsPipelineCreateInfo create_info;
    // ================================================================
    // Shader Stages
    // ================================================================
    const auto &vertex_stage = desc.vertex;
    const auto &fragment_stage = desc.fragment;
    if (!vertex_stage.module.is_valid() || vertex_stage.module.data == nullptr)
    {
        std::printf("[ERROR] Vulkan create graphics pipeline failed, missing vertex shader\n");
        return result;
    }
    if (!fragment_stage.module.is_valid() || fragment_stage.module.data == nullptr)
    {
        std::printf("[ERROR] Vulkan create graphics pipeline failed, missing vertex shader\n");
        return result;
    }
    const vk::raii::ShaderModule &vertex_shader_module = (static_cast<VkShaderModuleData *>(vertex_stage.module.data))->module;
    const vk::raii::ShaderModule &fragment_shader_module = (static_cast<VkShaderModuleData *>(fragment_stage.module.data))->module;
    vk::PipelineShaderStageCreateInfo vk_vert_stage;
    vk_vert_stage.setModule(vertex_shader_module);
    vk::PipelineShaderStageCreateInfo vk_frag_stage;
    vk_frag_stage.setModule(fragment_shader_module);
    std::array<vk::PipelineShaderStageCreateInfo, 2> stages = {vk_vert_stage, vk_frag_stage};
    create_info.setStages(stages);

    // ================================================================
    // 动态状态
    // 渲染管线中允许动态修改的状态
    // ================================================================

    vk::PipelineDynamicStateCreateInfo dynamic_state;
    const auto states = {
        vk::DynamicState::eViewport, // 窗口大小可变化
        vk::DynamicState::eScissor   // 裁剪状态可变化
    };
    dynamic_state.setDynamicStates(states);
    create_info.setPDynamicState(&dynamic_state);

    // ================================================================
    // 顶点输入layout
    // 顶点缓冲格式和顶点着色器layout匹配
    // ================================================================

    vk::PipelineVertexInputStateCreateInfo vertex_input_state;
    create_info.setPVertexInputState(&vertex_input_state);

    // ================================================================
    // 输入装配
    // Primitive Topology
    // ================================================================

    vk::PipelineInputAssemblyStateCreateInfo input_assembly;
    input_assembly.setTopology(get_vk_primitive_topology(desc.primitive.topology));
    input_assembly.setPrimitiveRestartEnable(false);
    create_info.setPInputAssemblyState(&input_assembly);

    auto data = memnew(VkGraphicsPipelineHandleData);
    // TODO pipeline cache
    data->pipeline = device.createGraphicsPipeline(nullptr, create_info);
    result.handle = 1;
    result.data = data;
    return result;
}

void VkRenderingDevice::destroy_render_pipeline(RenderPipelineHandle pipeline)
{
    (void)pipeline;
}