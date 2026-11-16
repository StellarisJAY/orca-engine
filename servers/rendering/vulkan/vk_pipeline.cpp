#include "vk_rendering_device.h"
#include "vk_internal.h"
#include "vk_format.h"
#include "rhi_validation.h"
#include "os/memory.h"

#include <array>
#include <cstdio>
#include <string>

namespace
{
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
        default:
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
        default:
            return vk::FrontFace::eCounterClockwise;
        }
    }
} // namespace

RenderPipelineHandle VkRenderingDevice::create_render_pipeline(const RenderPipelineDesc &desc)
{
    RenderPipelineHandle result;

    std::string error;
    if (!rhi_validate_render_pipeline_desc(desc, error))
    {
        std::printf("[ERROR] Vulkan create_render_pipeline(%s): %s\n", desc.label.c_str(), error.c_str());
        return result;
    }

    // 后端能力检查：每个颜色目标 / 深度格式都必须能映射成 vk::Format。
    for (size_t i = 0; i < desc.fragment.targets.size(); ++i)
    {
        vk::Format format = vk::Format::eUndefined;
        if (!vk_format_info(desc.fragment.targets[i].format, format))
        {
            std::printf("[ERROR] Vulkan create_render_pipeline(%s): color target %zu format is unsupported\n",
                        desc.label.c_str(), i);
            return result;
        }
    }
    if (desc.depth_stencil.format != Format::Undefined)
    {
        vk::Format format = vk::Format::eUndefined;
        if (!vk_format_info(desc.depth_stencil.format, format))
        {
            std::printf("[ERROR] Vulkan create_render_pipeline(%s): depth format is unsupported\n",
                        desc.label.c_str());
            return result;
        }
    }

    VkGraphicsPipelineHandleData *data = nullptr;
    try
    {
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
            std::printf("[ERROR] Vulkan create graphics pipeline failed, missing fragment shader\n");
            return result;
        }
        const vk::raii::ShaderModule &vertex_shader_module =
            (static_cast<VkShaderModuleData *>(vertex_stage.module.data))->module;
        const vk::raii::ShaderModule &fragment_shader_module =
            (static_cast<VkShaderModuleData *>(fragment_stage.module.data))->module;
        vk::PipelineShaderStageCreateInfo vk_vert_stage;
        vk_vert_stage.setModule(vertex_shader_module);
        vk::PipelineShaderStageCreateInfo vk_frag_stage;
        vk_frag_stage.setModule(fragment_shader_module);
        std::array<vk::PipelineShaderStageCreateInfo, 2> stages = {vk_vert_stage, vk_frag_stage};
        create_info.setStages(stages);

        // ================================================================
        // 动态状态
        // ================================================================
        vk::PipelineDynamicStateCreateInfo dynamic_state;
        const auto states = {
            vk::DynamicState::eViewport, // 窗口大小可变化
            vk::DynamicState::eScissor   // 裁剪状态可变化
        };
        dynamic_state.setDynamicStates(states);
        create_info.setPDynamicState(&dynamic_state);

        // ================================================================
        // 顶点输入 layout TODO
        // ================================================================
        vk::PipelineVertexInputStateCreateInfo vertex_input_state;
        create_info.setPVertexInputState(&vertex_input_state);

        // ================================================================
        // 输入装配
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
        rasterizer.setPolygonMode(vk::PolygonMode::eFill);
        rasterizer.setCullMode(get_vk_cull_mode(desc.primitive.cull_mode));
        rasterizer.setFrontFace(get_vk_front_face(desc.primitive.front_face));
        rasterizer.setDepthBiasEnable(false);
        create_info.setPRasterizationState(&rasterizer);

        // ================================================================
        // 多重采样
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
        // TODO(V5)：按 desc.fragment.targets / BlendState / write_mask 生成
        // VkPipelineColorBlendStateCreateInfo。
        vk::PipelineColorBlendAttachmentState color_blend_attachment;
        color_blend_attachment.setBlendEnable(false);
        color_blend_attachment.colorWriteMask = (vk::ColorComponentFlagBits::eR |
                                                 vk::ColorComponentFlagBits::eG |
                                                 vk::ColorComponentFlagBits::eB |
                                                 vk::ColorComponentFlagBits::eA);

        data = memnew(VkGraphicsPipelineHandleData);

        // ================================================================
        // 管线布局
        // ================================================================
        vk::PipelineLayoutCreateInfo pipeline_layout_info;
        data->pipeline_layout = device.createPipelineLayout(pipeline_layout_info);

        create_info.setLayout(data->pipeline_layout);
        create_info.basePipelineHandle = nullptr;
        create_info.basePipelineIndex = -1;

        // TODO(V5)：pipeline cache、render pass 兼容性、完整管线状态。
        data->pipeline = device.createGraphicsPipeline(nullptr, create_info);
        result.handle = alloc_handle_id();
        result.data = data;
    }
    catch (const vk::SystemError &err)
    {
        std::printf("[ERROR] Vulkan create_render_pipeline(%s) failed: %s\n", desc.label.c_str(), err.what());
        memdelete(data);
        return {};
    }
    return result;
}

void VkRenderingDevice::destroy_render_pipeline(RenderPipelineHandle pipeline)
{
    if (!pipeline.is_valid() || pipeline.data == nullptr)
        return;
    memdelete(static_cast<VkGraphicsPipelineHandleData *>(pipeline.data));
}
