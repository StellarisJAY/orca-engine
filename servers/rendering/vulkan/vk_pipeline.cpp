#include "vk_rendering_device.h"
#include "vk_internal.h"
#include "os/memory.h"

vk::PrimitiveTopology get_vk_primitive_topology(Primitive::Topology orca_topology)
{
    switch (orca_topology)
    {
    case Primitive::Topology::TriangleList:
        return vk::PrimitiveTopology::eTriangleList;
    case Primitive::Topology::TriangleStrip:
        return vk::PrimitiveTopology::eTriangleStrip;
    default:
        return vk::PrimitiveTopology::eTriangleList;
    }
}

vk::CullModeFlags get_vk_cull_mode(Primitive::CullMode orca_cull_mode)
{
    switch (orca_cull_mode)
    {
    case Primitive::CullMode::Back:
        return vk::CullModeFlagBits::eBack;
    case Primitive::CullMode::Front:
        return vk::CullModeFlagBits::eFront;
    case Primitive::CullMode::None:
        return vk::CullModeFlagBits::eNone;
    }
}

vk::FrontFace get_vk_front_face(Primitive::FrontFace orca_front_face)
{
    switch (orca_front_face)
    {
    case Primitive::FrontFace::CCW:
        return vk::FrontFace::eCounterClockwise;
    case Primitive::FrontFace::CW:
        return vk::FrontFace::eClockwise;
    }
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

    // ================================================================
    // 动态视口和裁剪规则
    // ================================================================

    vk::PipelineViewportStateCreateInfo viewport_state;
    viewport_state.viewportCount = 1;
    viewport_state.scissorCount = 1;
    create_info.setPViewportState(&viewport_state);

    // ================================================================
    // 光栅化器
    // ================================================================

    vk::PipelineRasterizationStateCreateInfo rasterizer;
    rasterizer.setDepthClampEnable(false);
    rasterizer.setRasterizerDiscardEnable(false);
    rasterizer.setPolygonMode(vk::PolygonMode::eFill);                     // 多边形模式，面、线框或点
    rasterizer.setCullMode(get_vk_cull_mode(desc.primitive.cull_mode));    // 面剔除模式
    rasterizer.setFrontFace(get_vk_front_face(desc.primitive.front_face)); // 正面
    rasterizer.setDepthBiasEnable(false);                                  // 深度偏置 暂时没用
    create_info.setPRasterizationState(&rasterizer);

    // ================================================================
    // 多重采样
    // 抗锯齿效果
    // ================================================================

    vk::PipelineMultisampleStateCreateInfo multi_sampling;
    multi_sampling.rasterizationSamples = vk::SampleCountFlagBits::e1;
    multi_sampling.sampleShadingEnable = false;
    create_info.setPMultisampleState(&multi_sampling);

    // ================================================================
    // 深度测试 & 模板测试
    // ================================================================

    vk::PipelineDepthStencilStateCreateInfo depth_stencil_state;
    depth_stencil_state.setDepthTestEnable(true);
    depth_stencil_state.setStencilTestEnable(false);
    create_info.setPDepthStencilState(&depth_stencil_state);

    // ================================================================
    // 颜色混合
    // ================================================================

    // TODO 暂时不做混合，之后兼容透明度时再接入
    vk::PipelineColorBlendAttachmentState color_blend_attachment;
    color_blend_attachment.setBlendEnable(false);
    color_blend_attachment.colorWriteMask = (vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                                             vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA);

    auto data = memnew(VkGraphicsPipelineHandleData);

    // ================================================================
    // 管线布局
    // ================================================================

    vk::PipelineLayoutCreateInfo pipeline_layout_info;
    data->pipeline_layout = device.createPipelineLayout(pipeline_layout_info);
    
    create_info.setLayout(data->pipeline_layout);
    create_info.basePipelineHandle = nullptr; // Optional
    create_info.basePipelineIndex = -1; // Optional

    
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