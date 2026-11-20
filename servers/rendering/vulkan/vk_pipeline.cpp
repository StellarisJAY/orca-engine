#include "vk_rendering_device.h"
#include "vk_internal.h"
#include "vk_format.h"
#include "rhi_validation.h"
#include "os/memory.h"

#include <array>
#include <cstdio>
#include <string>
#include <vector>

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

    /// @brief 创建只用于管线兼容性检查的 render pass（G-09）。
    ///
    /// 录制侧走经典 `VkRenderPass` 对象（I-31），因此管线创建必须传 renderPass；但管线
    /// 创建时还不知道 load/store 与 initial/final layout（真正由 `encoder_begin_render_pass`
    /// 按附件决定）。Vulkan 的 render pass 兼容性只比较附件格式/采样数、subpass 引用与
    /// 附件索引，**不比较** load/store 与 initial/final layout，所以这个 render pass 用
    /// 固定的 Clear/Store + Undefined/附件布局即可，只需保证：
    ///   - 颜色附件按 targets 顺序排在 0..n-1，深度附件排在 n（与录制侧一致）；
    ///   - 采样数与管线一致。
    /// 它从不会被 `vkCmdBeginRenderPass` 使用，只随管线一起销毁。
    vk::raii::RenderPass create_compatibility_render_pass(const vk::raii::Device &device,
                                                          const std::vector<vk::Format> &color_formats,
                                                          vk::Format depth_format, vk::SampleCountFlagBits samples)
    {
        std::vector<vk::AttachmentDescription> attachments;
        std::vector<vk::AttachmentReference> color_references;
        const bool has_depth = depth_format != vk::Format::eUndefined;
        attachments.reserve(color_formats.size() + (has_depth ? 1u : 0u));
        color_references.reserve(color_formats.size());

        for (size_t i = 0; i < color_formats.size(); ++i)
        {
            vk::AttachmentDescription attachment;
            attachment.setFormat(color_formats[i]);
            attachment.setSamples(samples);
            attachment.setLoadOp(vk::AttachmentLoadOp::eClear);
            attachment.setStoreOp(vk::AttachmentStoreOp::eStore);
            attachment.setStencilLoadOp(vk::AttachmentLoadOp::eDontCare);
            attachment.setStencilStoreOp(vk::AttachmentStoreOp::eDontCare);
            attachment.setInitialLayout(vk::ImageLayout::eUndefined);
            attachment.setFinalLayout(vk::ImageLayout::eColorAttachmentOptimal);
            attachments.emplace_back(attachment);
            color_references.emplace_back(vk::AttachmentReference{
                static_cast<uint32_t>(i), vk::ImageLayout::eColorAttachmentOptimal});
        }

        vk::AttachmentReference depth_reference;
        if (has_depth)
        {
            vk::AttachmentDescription attachment;
            attachment.setFormat(depth_format);
            attachment.setSamples(samples);
            attachment.setLoadOp(vk::AttachmentLoadOp::eClear);
            attachment.setStoreOp(vk::AttachmentStoreOp::eDontCare);
            attachment.setStencilLoadOp(vk::AttachmentLoadOp::eDontCare);
            attachment.setStencilStoreOp(vk::AttachmentStoreOp::eDontCare);
            attachment.setInitialLayout(vk::ImageLayout::eUndefined);
            attachment.setFinalLayout(vk::ImageLayout::eDepthStencilAttachmentOptimal);
            attachments.emplace_back(attachment);
            depth_reference = vk::AttachmentReference{static_cast<uint32_t>(color_formats.size()),
                                                      vk::ImageLayout::eDepthStencilAttachmentOptimal};
        }

        vk::SubpassDescription subpass;
        subpass.setPipelineBindPoint(vk::PipelineBindPoint::eGraphics);
        if (!color_references.empty())
            subpass.setColorAttachments(color_references);
        if (has_depth)
            subpass.setPDepthStencilAttachment(&depth_reference);

        vk::RenderPassCreateInfo create_info;
        create_info.setAttachments(attachments);
        create_info.setSubpasses(subpass);
        // 兼容性还要求 subpass dependency 的数量与内容完全一致（Vulkan 的 render pass
        // 兼容性只豁免 initial/final layout、load/store 与 attachment reference 的
        // layout）。默认帧缓冲与显式附件 pass 都用同一组 dependency，这里必须一致。
        const auto dependencies = vk_render_pass_util::make_render_pass_dependencies();
        create_info.setDependencies(dependencies);
        return device.createRenderPass(create_info);
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

    // 后端能力检查：每个颜色目标 / 深度格式都必须能映射成 vk::Format，
    // 颜色附件数量不能超过设备的 maxColorAttachments。
    const auto properties = physical_device.getProperties();
    const auto &limits = properties.limits;
    if (desc.fragment.targets.size() > limits.maxColorAttachments)
    {
        std::printf("[ERROR] Vulkan create_render_pipeline(%s): %zu color targets exceed maxColorAttachments (%u)\n",
                    desc.label.c_str(), desc.fragment.targets.size(), limits.maxColorAttachments);
        return result;
    }

    std::vector<vk::Format> color_formats;
    color_formats.reserve(desc.fragment.targets.size());
    for (size_t i = 0; i < desc.fragment.targets.size(); ++i)
    {
        vk::Format format = vk::Format::eUndefined;
        if (!vk_format_info(desc.fragment.targets[i].format, format))
        {
            std::printf("[ERROR] Vulkan create_render_pipeline(%s): color target %zu format is unsupported\n",
                        desc.label.c_str(), i);
            return result;
        }
        color_formats.push_back(format);
    }

    vk::Format depth_format = vk::Format::eUndefined;
    if (desc.depth_stencil.format != Format::Undefined && !vk_format_info(desc.depth_stencil.format, depth_format))
    {
        std::printf("[ERROR] Vulkan create_render_pipeline(%s): depth format is unsupported\n",
                    desc.label.c_str());
        return result;
    }

    const vk::SampleCountFlagBits samples = vk_sample_count_from_bits(desc.samples_count);

    VkGraphicsPipelineHandleData *data = nullptr;
    try
    {
        // ================================================================
        // 管线布局
        // ================================================================
        // D-28：不再自建空的 pipeline layout。绑定组按 desc.pipeline_layout 的 set 顺序
        // 绑定，管线必须与它完全一致；pipeline layout 的所有权归调用方。
        if (!desc.pipeline_layout.is_valid() || desc.pipeline_layout.data == nullptr)
        {
            std::printf("[ERROR] Vulkan create graphics pipeline failed, invalid pipeline layout handle\n");
            return result;
        }
        const auto pipeline_layout_data =
            static_cast<VkPipelineLayoutHandleData *>(desc.pipeline_layout.data);

        vk::GraphicsPipelineCreateInfo create_info;
        create_info.setLayout(*pipeline_layout_data->pipeline_layout);

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
        const VkShaderModuleData *vertex_module_data =
            static_cast<const VkShaderModuleData *>(vertex_stage.module.data);
        const VkShaderModuleData *fragment_module_data =
            static_cast<const VkShaderModuleData *>(fragment_stage.module.data);

        // D-27：模块创建时记录的 stage 必须与管线槽位一致，避免把顶点模块装进
        // fragment 槽（Vulkan 只在 validation 层报 stage 不匹配，release 下会静默出错）。
        if (vertex_module_data->type != ShaderType::VERTEX)
        {
            std::printf("[ERROR] Vulkan create graphics pipeline failed, vertex stage module is not a vertex shader\n");
            return result;
        }
        if (fragment_module_data->type != ShaderType::FRAGMENT)
        {
            std::printf("[ERROR] Vulkan create graphics pipeline failed, fragment stage module is not a fragment shader\n");
            return result;
        }

        // 入口名以阶段级 entry_point 为准（一个模块可以带多个入口），为空时回退到
        // 模块创建时已经校验过的入口名；两者都为空由共享校验拒绝。
        const std::string &vertex_entry_point =
            vertex_stage.entry_point.empty() ? vertex_module_data->entry_point : vertex_stage.entry_point;
        const std::string &fragment_entry_point =
            fragment_stage.entry_point.empty() ? fragment_module_data->entry_point : fragment_stage.entry_point;

        vk::PipelineShaderStageCreateInfo vk_vert_stage;
        vk_vert_stage.setStage(vk::ShaderStageFlagBits::eVertex);
        vk_vert_stage.setModule(vertex_module_data->module);
        vk_vert_stage.setPName(vertex_entry_point.c_str());
        vk::PipelineShaderStageCreateInfo vk_frag_stage;
        vk_frag_stage.setStage(vk::ShaderStageFlagBits::eFragment);
        vk_frag_stage.setModule(fragment_module_data->module);
        vk_frag_stage.setPName(fragment_entry_point.c_str());
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
        // 顶点输入
        // ================================================================
        // 接口约定：buffers[i] 对应 pass_set_vertex_buffer 的 slot i，binding 使用 slot 下标；
        // 共享校验当前只允许 1 个 vertex buffer，但这里按通用循环生成，后续放开数量无需改。
        std::vector<vk::VertexInputBindingDescription> vertex_bindings;
        std::vector<vk::VertexInputAttributeDescription> vertex_attributes;
        vertex_bindings.reserve(desc.vertex.buffers.size());
        for (size_t buffer_index = 0; buffer_index < desc.vertex.buffers.size(); ++buffer_index)
        {
            const BufferLayout &buffer = desc.vertex.buffers[buffer_index];
            vertex_bindings.emplace_back(static_cast<uint32_t>(buffer_index), buffer.array_stride,
                                         vk::VertexInputRate::eVertex);
            vertex_attributes.reserve(vertex_attributes.size() + buffer.attributes.size());
            for (const BufferLayout::Attribute &attribute : buffer.attributes)
            {
                vk::Format attribute_format = vk::Format::eUndefined;
                if (!vk_vertex_format_info(attribute.format, attribute_format))
                {
                    // 共享校验已经拒绝未知格式，这里只是防御性分支。
                    std::printf("[ERROR] Vulkan create graphics pipeline failed, unknown vertex format "
                                "at shader_location %u\n", attribute.shader_location);
                    memdelete(data);
                    return result;
                }
                vertex_attributes.emplace_back(attribute.shader_location, static_cast<uint32_t>(buffer_index),
                                               attribute_format, attribute.offset);
            }
        }
        vk::PipelineVertexInputStateCreateInfo vertex_input_state;
        vertex_input_state.setVertexBindingDescriptions(vertex_bindings);
        vertex_input_state.setVertexAttributeDescriptions(vertex_attributes);
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
        rasterizer.setLineWidth(1.0f);
        create_info.setPRasterizationState(&rasterizer);

        // ================================================================
        // 多重采样
        // ================================================================
        vk::PipelineMultisampleStateCreateInfo multi_sampling;
        multi_sampling.setRasterizationSamples(samples);
        multi_sampling.setSampleShadingEnable(false);
        create_info.setPMultisampleState(&multi_sampling);

        // ================================================================
        // 深度测试 & 模板测试
        // ================================================================
        // 深度状态完全由 desc.depth_stencil 驱动；没有深度附件时关闭 depth test/write，
        // 避免与"render pass 无深度附件"产生 validation 冲突。
        const bool has_depth_attachment = depth_format != vk::Format::eUndefined;
        vk::PipelineDepthStencilStateCreateInfo depth_stencil_state;
        depth_stencil_state.setDepthTestEnable(has_depth_attachment);
        depth_stencil_state.setDepthWriteEnable(has_depth_attachment && desc.depth_stencil.depth_write_enabled);
        depth_stencil_state.setDepthCompareOp(vk_compare_op_from_function(desc.depth_stencil.depth_compare));
        depth_stencil_state.setDepthBoundsTestEnable(false);
        depth_stencil_state.setStencilTestEnable(false);
        create_info.setPDepthStencilState(&depth_stencil_state);

        // ================================================================
        // 颜色混合
        // ================================================================
        // 每个颜色目标一个 attachment state（blend + write mask）；targets 为空表示
        // 纯深度 pass，attachmentCount 必须为 0（对应 render pass 没有颜色附件）。
        std::vector<vk::PipelineColorBlendAttachmentState> color_blend_attachments;
        color_blend_attachments.reserve(desc.fragment.targets.size());
        for (const ColorTargetState &target : desc.fragment.targets)
        {
            vk::PipelineColorBlendAttachmentState attachment;
            attachment.setBlendEnable(target.blend.enabled);
            attachment.setSrcColorBlendFactor(vk_blend_factor_from(target.blend.color.src_factor));
            attachment.setDstColorBlendFactor(vk_blend_factor_from(target.blend.color.dst_factor));
            attachment.setColorBlendOp(vk_blend_op_from(target.blend.color.operation));
            attachment.setSrcAlphaBlendFactor(vk_blend_factor_from(target.blend.alpha.src_factor));
            attachment.setDstAlphaBlendFactor(vk_blend_factor_from(target.blend.alpha.dst_factor));
            attachment.setAlphaBlendOp(vk_blend_op_from(target.blend.alpha.operation));
            attachment.setColorWriteMask(vk_color_write_mask_from(target.write_mask));
            color_blend_attachments.emplace_back(attachment);
        }
        vk::PipelineColorBlendStateCreateInfo color_blend_state;
        color_blend_state.setLogicOpEnable(false);
        color_blend_state.setAttachments(color_blend_attachments);
        create_info.setPColorBlendState(&color_blend_state);

        data = memnew(VkGraphicsPipelineHandleData);
        data->pipeline_layout = pipeline_layout_data;
        data->vertex_buffer_count = static_cast<uint32_t>(desc.vertex.buffers.size());
        data->label = desc.label;

        // ================================================================
        // render pass 兼容性（G-09）
        // ================================================================
        data->compatibility_render_pass =
            create_compatibility_render_pass(device, color_formats, depth_format, samples);
        create_info.setRenderPass(*data->compatibility_render_pass);

        create_info.basePipelineHandle = nullptr;
        create_info.basePipelineIndex = -1;

        // TODO(V5)：pipeline cache（D-28 剩余项）；完整状态已按 RenderPipelineDesc 映射。
        data->pipeline = device.createGraphicsPipeline(nullptr, create_info);
        result.handle = alloc_handle_id();
        result.data = data;
        std::printf("[INFO] Vulkan created render pipeline(%s): %zu color target(s), depth %s, %u sample(s)\n",
                    desc.label.c_str(), color_formats.size(), has_depth_attachment ? "on" : "off",
                    desc.samples_count);
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
