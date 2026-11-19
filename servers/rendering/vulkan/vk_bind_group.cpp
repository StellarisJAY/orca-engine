#include "vk_rendering_device.h"
#include "vk_internal.h"
#include "rhi_validation.h"
#include "os/memory.h"

#include <cstdio>
#include <string>
#include <utility>
#include <vector>

// =============================================================================
// 绑定组 / 管线布局（V5-1 / V5-2 / V5-4）。
//
// Vulkan 的映射：
//   BindGroupLayout → VkDescriptorSetLayout
//   BindGroup       → 每个 bind group 独占一个小 descriptor pool + 一个 descriptor set
//   PipelineLayout  → VkPipelineLayout（set 顺序与 PipelineLayoutDesc::bind_group_layouts 对齐）
//
// 描述校验统一走 rhi_validation.*（rhi_validate_bind_group_layout_desc /
// rhi_resolve_bind_group_desc / rhi_validate_pipeline_layout_desc），这里只补后端能力
// 约束（每 set 的描述符数量上限、uniform buffer offset 对齐）并做 Vulkan 调用。
//
// descriptor set 不受本后端额外管理：一个 bind group 一个 pool（maxSets = 1），
// destroy_bind_group 直接销毁整个 pool，避免共享 pool 的碎片/耗尽问题（G-10 的
// 按帧 pool 方案在 V6 引入帧 fence 后再统一设计）。
// =============================================================================

namespace
{
    void print_vk_error(const char *what, const vk::SystemError &error)
    {
        std::printf("[ERROR] Vulkan %s failed: %s\n", what, error.what());
    }

    vk::ShaderStageFlags get_vk_shader_stages(uint8_t visibility)
    {
        vk::ShaderStageFlags stages;
        if ((visibility & ShaderStage::VERTEX) != 0)
            stages |= vk::ShaderStageFlagBits::eVertex;
        if ((visibility & ShaderStage::FRAGMENT) != 0)
            stages |= vk::ShaderStageFlagBits::eFragment;
        return stages;
    }

    /// @brief 绑定类型 + 动态偏移 → descriptor type。
    ///
    /// 动态 uniform buffer 必须用 eUniformBufferDynamic：它允许 vkCmdBindDescriptorSets
    /// 的 dynamicOffset 在寄存器里切换，而不必为每个 draw 重建 descriptor set。
    vk::DescriptorType get_vk_descriptor_type(BindingType type, bool has_dynamic_offset)
    {
        switch (type)
        {
        case BindingType::UniformBuffer:
            return has_dynamic_offset ? vk::DescriptorType::eUniformBufferDynamic
                                      : vk::DescriptorType::eUniformBuffer;
        case BindingType::Texture2D:
        case BindingType::TextureCube:
        case BindingType::DepthTexture:
            return vk::DescriptorType::eCombinedImageSampler;
        case BindingType::Sampler:
            return vk::DescriptorType::eSampler;
        default:
            return vk::DescriptorType::eUniformBuffer;
        }
    }

    /// @brief 采样 descriptor 使用的 image layout。
    ///
    /// 与 vk_image_resting_layout 一致：颜色纹理在 shader 里采样时是
    /// eShaderReadOnlyOptimal，深度纹理是 eDepthStencilReadOnlyOptimal。纹理在
    /// 绑定后仍可能被 render pass 切到附件布局；真正的 barrier 由 draw 路径（V6）
    /// 在提交前按 VkTextureHandleData::current_layout 补齐。
    vk::ImageLayout get_vk_binding_image_layout(BindingType type)
    {
        return type == BindingType::DepthTexture ? vk::ImageLayout::eDepthStencilReadOnlyOptimal
                                                 : vk::ImageLayout::eShaderReadOnlyOptimal;
    }

    /// @brief 校验同一个 binding 的句柄可用，并把资源信息填进共享校验结构。
    ///
    /// 这里不报错，只把"是否提供了某类资源"如实填进去：具体错误由
    /// rhi_resolve_bind_group_desc 统一生成，保证校验消息只有一处来源。
    void fill_binding_resource_info(const BindGroupEntry &entry, RhiBindingResourceInfo &info,
                                    VkBindGroupBindingData &binding)
    {
        info.type = entry.resource.type;
        binding.binding = entry.binding;
        binding.type = entry.resource.type;

        if (entry.resource.buffer.is_valid() && entry.resource.buffer.data != nullptr)
        {
            const auto buffer_data = static_cast<VkBufferHandleData *>(entry.resource.buffer.data);
            info.has_buffer = true;
            info.buffer_usage = buffer_data->usage;
            info.buffer_size = buffer_data->size;
            info.offset = entry.resource.offset;
            info.size = entry.resource.size;
            binding.buffer = buffer_data;
        }

        if (entry.resource.texture_view.is_valid() && entry.resource.texture_view.data != nullptr)
        {
            const auto view_data = static_cast<VkTextureViewHandleData *>(entry.resource.texture_view.data);
            info.has_view = true;
            info.view_dim = view_data->dim;
            info.view_aspect = view_data->aspect;
            info.view_format = view_data->format;
            binding.view = view_data;
            binding.image_layout = get_vk_binding_image_layout(entry.resource.type);
        }

        if (entry.resource.sampler.is_valid() && entry.resource.sampler.data != nullptr)
        {
            const auto sampler_data = static_cast<VkSamplerHandleData *>(entry.resource.sampler.data);
            info.has_sampler = true;
            info.sampler_compare_enable = sampler_data->desc.compare_enable;
            binding.sampler = sampler_data;
        }
    }
} // namespace

/// @brief 创建绑定组布局（BindGroupLayout → VkDescriptorSetLayout）。
BindGroupLayoutHandle VkRenderingDevice::create_bind_group_layout(const BindGroupLayoutDesc &desc)
{
    BindGroupLayoutHandle result;

    std::string error;
    if (!rhi_validate_bind_group_layout_desc(desc, error))
    {
        std::printf("[ERROR] Vulkan create_bind_group_layout(%s): %s\n", desc.label.c_str(), error.c_str());
        return result;
    }

    // 后端能力检查：每种 descriptor type 在每个 set 内的数量上限（VUID 会拒绝超限）。
    const vk::PhysicalDeviceLimits &limits = physical_device.getProperties().limits;
    uint32_t uniform_buffer_count = 0;
    uint32_t dynamic_uniform_buffer_count = 0;
    uint32_t sampler_count = 0;
    uint32_t sampled_image_count = 0;
    for (const BindGroupLayoutEntry &entry : desc.entries)
    {
        switch (entry.type)
        {
        case BindingType::UniformBuffer:
            if (entry.has_dynamic_offset)
                ++dynamic_uniform_buffer_count;
            else
                ++uniform_buffer_count;
            break;
        case BindingType::Texture2D:
        case BindingType::TextureCube:
        case BindingType::DepthTexture:
            ++sampled_image_count;
            break;
        case BindingType::Sampler:
            ++sampler_count;
            break;
        default:
            break;
        }
    }
    if (uniform_buffer_count > limits.maxDescriptorSetUniformBuffers)
    {
        std::printf("[ERROR] Vulkan create_bind_group_layout(%s): %u uniform buffers exceed "
                    "maxDescriptorSetUniformBuffers %u\n",
                    desc.label.c_str(), uniform_buffer_count, limits.maxDescriptorSetUniformBuffers);
        return result;
    }
    if (dynamic_uniform_buffer_count > limits.maxDescriptorSetUniformBuffersDynamic)
    {
        std::printf("[ERROR] Vulkan create_bind_group_layout(%s): %u dynamic uniform buffers exceed "
                    "maxDescriptorSetUniformBuffersDynamic %u\n",
                    desc.label.c_str(), dynamic_uniform_buffer_count,
                    limits.maxDescriptorSetUniformBuffersDynamic);
        return result;
    }
    if (sampler_count > limits.maxDescriptorSetSamplers)
    {
        std::printf("[ERROR] Vulkan create_bind_group_layout(%s): %u samplers exceed "
                    "maxDescriptorSetSamplers %u\n",
                    desc.label.c_str(), sampler_count, limits.maxDescriptorSetSamplers);
        return result;
    }
    if (sampled_image_count > limits.maxDescriptorSetSampledImages)
    {
        std::printf("[ERROR] Vulkan create_bind_group_layout(%s): %u sampled images exceed "
                    "maxDescriptorSetSampledImages %u\n",
                    desc.label.c_str(), sampled_image_count, limits.maxDescriptorSetSampledImages);
        return result;
    }

    VkBindGroupLayoutHandleData *data = nullptr;
    try
    {
        data = memnew(VkBindGroupLayoutHandleData);
        data->label = desc.label;
        data->entries.reserve(desc.entries.size());

        std::vector<vk::DescriptorSetLayoutBinding> bindings;
        bindings.reserve(desc.entries.size());
        for (const BindGroupLayoutEntry &entry : desc.entries)
        {
            vk::DescriptorSetLayoutBinding binding;
            binding.setBinding(entry.binding);
            binding.setDescriptorType(get_vk_descriptor_type(entry.type, entry.has_dynamic_offset));
            binding.setDescriptorCount(1);
            binding.setStageFlags(get_vk_shader_stages(entry.visibility));
            bindings.push_back(binding);

            VkBindGroupLayoutEntryData entry_data;
            entry_data.binding = entry.binding;
            entry_data.type = entry.type;
            entry_data.visibility = entry.visibility;
            entry_data.min_binding_size = entry.min_binding_size;
            entry_data.has_dynamic_offset = entry.has_dynamic_offset;
            data->entries.push_back(entry_data);
        }

        vk::DescriptorSetLayoutCreateInfo create_info;
        create_info.setBindings(bindings);
        data->descriptor_set_layout = device.createDescriptorSetLayout(create_info);

        result.handle = alloc_handle_id();
        result.data = data;
    }
    catch (const vk::SystemError &vk_error)
    {
        print_vk_error(("create_bind_group_layout(" + desc.label + ")").c_str(), vk_error);
        memdelete(data);
        return {};
    }

    std::printf("[INFO] Vulkan created bind group layout(%s): %zu binding(s)\n", desc.label.c_str(),
                desc.entries.size());
    return result;
}

/// @brief 创建绑定组（BindGroup → descriptor set + 写入 descriptor）。
BindGroupHandle VkRenderingDevice::create_bind_group(const BindGroupDesc &desc)
{
    BindGroupHandle result;
    if (!desc.layout.is_valid() || desc.layout.data == nullptr)
    {
        std::printf("[ERROR] Vulkan create_bind_group(%s): invalid bind group layout handle\n",
                    desc.label.c_str());
        return result;
    }
    const auto layout_data = static_cast<VkBindGroupLayoutHandleData *>(desc.layout.data);

    // 1) 把布局和每个 entry 的资源信息整理成共享校验的输入。
    RhiBindGroupLayoutInfo layout_info;
    layout_info.entries.reserve(layout_data->entries.size());
    for (const VkBindGroupLayoutEntryData &layout_entry : layout_data->entries)
    {
        RhiBindGroupLayoutEntry entry;
        entry.binding = layout_entry.binding;
        entry.type = layout_entry.type;
        entry.visibility = layout_entry.visibility;
        entry.min_binding_size = layout_entry.min_binding_size;
        entry.has_dynamic_offset = layout_entry.has_dynamic_offset;
        layout_info.entries.push_back(entry);
    }

    std::vector<RhiBindGroupEntryInfo> entry_infos;
    entry_infos.reserve(desc.entries.size());
    std::vector<VkBindGroupBindingData> raw_bindings;
    raw_bindings.reserve(desc.entries.size());
    for (const BindGroupEntry &entry : desc.entries)
    {
        RhiBindGroupEntryInfo info;
        info.binding = entry.binding;
        VkBindGroupBindingData binding;
        fill_binding_resource_info(entry, info.resource, binding);
        entry_infos.push_back(info);
        raw_bindings.push_back(binding);
    }

    // 2) 共享校验 + 解析 buffer 区间。
    std::string error;
    std::vector<RhiResolvedBinding> resolved;
    if (!rhi_resolve_bind_group_desc(layout_info, entry_infos, resolved, error))
    {
        std::printf("[ERROR] Vulkan create_bind_group(%s): %s\n", desc.label.c_str(), error.c_str());
        return result;
    }

    // 3) 后端能力检查：uniform buffer 的 offset 必须按 minUniformBufferOffsetAlignment 对齐。
    const uint64_t uniform_offset_alignment =
        physical_device.getProperties().limits.minUniformBufferOffsetAlignment;
    for (const RhiResolvedBinding &binding : resolved)
    {
        if (binding.type != BindingType::UniformBuffer)
            continue;
        if (uniform_offset_alignment > 1 && binding.offset % uniform_offset_alignment != 0)
        {
            std::printf("[ERROR] Vulkan create_bind_group(%s): binding %u offset %llu is not aligned to "
                        "minUniformBufferOffsetAlignment %llu\n",
                        desc.label.c_str(), binding.binding, static_cast<unsigned long long>(binding.offset),
                        static_cast<unsigned long long>(uniform_offset_alignment));
            return result;
        }
    }

    VkBindGroupHandleData *data = nullptr;
    try
    {
        data = memnew(VkBindGroupHandleData);
        data->label = desc.label;
        data->layout = layout_data;
        data->bindings.reserve(resolved.size());

        // 4) 按类型统计 descriptor pool 容量（一个 bind group 独占一个 pool）。
        std::vector<vk::DescriptorPoolSize> pool_sizes;
        std::vector<vk::DescriptorType> descriptor_types;
        for (const RhiResolvedBinding &binding : resolved)
        {
            const VkBindGroupBindingData *raw = nullptr;
            for (const VkBindGroupBindingData &candidate : raw_bindings)
            {
                if (candidate.binding == binding.binding)
                {
                    raw = &candidate;
                    break;
                }
            }
            if (raw == nullptr)
            {
                std::printf("[ERROR] Vulkan create_bind_group(%s): binding %u is missing after validation\n",
                            desc.label.c_str(), binding.binding);
                memdelete(data);
                return {};
            }

            VkBindGroupBindingData binding_data = *raw;
            binding_data.type = binding.type;
            binding_data.has_dynamic_offset = binding.has_dynamic_offset;
            binding_data.offset = binding.offset;
            binding_data.size = binding.size;
            data->bindings.push_back(binding_data);

            const vk::DescriptorType descriptor_type =
                get_vk_descriptor_type(binding.type, binding.has_dynamic_offset);
            descriptor_types.push_back(descriptor_type);
            bool found = false;
            for (vk::DescriptorPoolSize &pool_size : pool_sizes)
            {
                if (pool_size.type == descriptor_type)
                {
                    ++pool_size.descriptorCount;
                    found = true;
                    break;
                }
            }
            if (!found)
                pool_sizes.emplace_back(descriptor_type, 1);
        }

        // 5) 创建 pool + 分配一个 descriptor set。
        vk::DescriptorPoolCreateInfo pool_info;
        pool_info.setMaxSets(1);
        // vk::raii::DescriptorSet 析构时会调用 vkFreeDescriptorSets，因此 pool 必须带
        // FREE_DESCRIPTOR_SET_BIT（VUID-vkFreeDescriptorSets-descriptorPool-00312）。
        // set 成员声明在 pool 之后，逆序析构正好满足"set 先于 pool 释放"。
        pool_info.setFlags(vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet);
        pool_info.setPoolSizes(pool_sizes);
        data->descriptor_pool = device.createDescriptorPool(pool_info);

        const vk::DescriptorSetLayout set_layout = *layout_data->descriptor_set_layout;
        vk::DescriptorSetAllocateInfo allocate_info(*data->descriptor_pool, 1, &set_layout);
        std::vector<vk::raii::DescriptorSet> descriptor_sets =
            device.allocateDescriptorSets(allocate_info);
        data->descriptor_set = std::move(descriptor_sets[0]);

        // 6) 写入 descriptor。pBufferInfo / pImageInfo 必须指向稳定存储，因此先填
        //    info 数组、再构造 WriteDescriptorSet。
        std::vector<vk::DescriptorBufferInfo> buffer_infos;
        std::vector<vk::DescriptorImageInfo> image_infos;
        buffer_infos.reserve(data->bindings.size());
        image_infos.reserve(data->bindings.size());

        struct WriteSource
        {
            uint32_t binding = 0;
            vk::DescriptorType descriptor_type = vk::DescriptorType::eUniformBuffer;
            /// @brief pBufferInfo / pImageInfo 数组中的下标
            size_t info_index = 0;
            bool is_buffer = false;
        };
        std::vector<WriteSource> write_sources;
        write_sources.reserve(data->bindings.size());

        for (size_t i = 0; i < data->bindings.size(); ++i)
        {
            const VkBindGroupBindingData &binding = data->bindings[i];
            WriteSource source;
            source.binding = binding.binding;
            source.descriptor_type = descriptor_types[i];
            if (binding.type == BindingType::UniformBuffer)
            {
                if (binding.buffer == nullptr)
                {
                    std::printf("[ERROR] Vulkan create_bind_group(%s): binding %u has no buffer\n",
                                desc.label.c_str(), binding.binding);
                    memdelete(data);
                    return {};
                }
                source.info_index = buffer_infos.size();
                source.is_buffer = true;
                buffer_infos.emplace_back(*binding.buffer->buffer, binding.offset, binding.size);
            }
            else
            {
                if (binding.sampler == nullptr)
                {
                    std::printf("[ERROR] Vulkan create_bind_group(%s): binding %u has no sampler\n",
                                desc.label.c_str(), binding.binding);
                    memdelete(data);
                    return {};
                }
                source.info_index = image_infos.size();
                if (binding.type == BindingType::Sampler)
                {
                    vk::DescriptorImageInfo image_info;
                    image_info.setSampler(*binding.sampler->sampler);
                    image_infos.push_back(image_info);
                }
                else
                {
                    if (binding.view == nullptr)
                    {
                        std::printf("[ERROR] Vulkan create_bind_group(%s): binding %u has no texture view\n",
                                    desc.label.c_str(), binding.binding);
                        memdelete(data);
                        return {};
                    }
                    image_infos.emplace_back(*binding.sampler->sampler, *binding.view->image_view,
                                             binding.image_layout);
                }
            }
            write_sources.push_back(source);
        }

        std::vector<vk::WriteDescriptorSet> writes;
        writes.reserve(write_sources.size());
        for (const WriteSource &source : write_sources)
        {
            vk::WriteDescriptorSet write;
            write.setDstSet(*data->descriptor_set);
            write.setDstBinding(source.binding);
            write.setDescriptorCount(1);
            write.setDescriptorType(source.descriptor_type);
            if (source.is_buffer)
                write.setPBufferInfo(&buffer_infos[source.info_index]);
            else
                write.setPImageInfo(&image_infos[source.info_index]);
            writes.push_back(write);
        }
        device.updateDescriptorSets(writes, {});

        result.handle = alloc_handle_id();
        result.data = data;
    }
    catch (const vk::SystemError &vk_error)
    {
        print_vk_error(("create_bind_group(" + desc.label + ")").c_str(), vk_error);
        memdelete(data);
        return {};
    }

    std::printf("[INFO] Vulkan created bind group(%s): %zu binding(s), layout(%s)\n", desc.label.c_str(),
                data->bindings.size(), layout_data->label.c_str());
    return result;
}

/// @brief 创建管线布局（按 group 顺序引用 bind group layout）。
PipelineLayoutHandle VkRenderingDevice::create_pipeline_layout(const PipelineLayoutDesc &desc)
{
    PipelineLayoutHandle result;

    std::string error;
    if (!rhi_validate_pipeline_layout_desc(desc, error))
    {
        std::printf("[ERROR] Vulkan create_pipeline_layout(%s): %s\n", desc.label.c_str(), error.c_str());
        return result;
    }

    std::vector<VkBindGroupLayoutHandleData *> layout_data;
    layout_data.reserve(desc.bind_group_layouts.size());
    for (size_t i = 0; i < desc.bind_group_layouts.size(); ++i)
    {
        if (desc.bind_group_layouts[i].data == nullptr)
        {
            std::printf("[ERROR] Vulkan create_pipeline_layout(%s): bind group layout %zu has no backend data\n",
                        desc.label.c_str(), i);
            return result;
        }
        layout_data.push_back(static_cast<VkBindGroupLayoutHandleData *>(desc.bind_group_layouts[i].data));
    }

    // Vulkan 的 pipeline layout 最多引用 maxBoundDescriptorSets 个 set。
    const uint32_t max_bound_sets = physical_device.getProperties().limits.maxBoundDescriptorSets;
    if (desc.bind_group_layouts.size() > max_bound_sets)
    {
        std::printf("[ERROR] Vulkan create_pipeline_layout(%s): %zu sets exceed maxBoundDescriptorSets %u\n",
                    desc.label.c_str(), desc.bind_group_layouts.size(), max_bound_sets);
        return result;
    }

    VkPipelineLayoutHandleData *data = nullptr;
    try
    {
        std::vector<vk::DescriptorSetLayout> set_layouts;
        set_layouts.reserve(layout_data.size());
        for (const VkBindGroupLayoutHandleData *layout : layout_data)
            set_layouts.push_back(*layout->descriptor_set_layout);

        data = memnew(VkPipelineLayoutHandleData);
        data->label = desc.label;
        data->bind_group_layouts = layout_data;

        vk::PipelineLayoutCreateInfo create_info;
        if (!set_layouts.empty())
            create_info.setSetLayouts(set_layouts);
        data->pipeline_layout = device.createPipelineLayout(create_info);

        result.handle = alloc_handle_id();
        result.data = data;
    }
    catch (const vk::SystemError &vk_error)
    {
        print_vk_error(("create_pipeline_layout(" + desc.label + ")").c_str(), vk_error);
        memdelete(data);
        return {};
    }

    std::printf("[INFO] Vulkan created pipeline layout(%s): %zu bind group layout(s)\n", desc.label.c_str(),
                desc.bind_group_layouts.size());
    return result;
}

void VkRenderingDevice::destroy_bind_group(BindGroupHandle bind_group)
{
    if (!bind_group.is_valid() || bind_group.data == nullptr)
        return;
    // pool 随 bind group 一起销毁，它的 descriptor set 也一并释放。
    memdelete(static_cast<VkBindGroupHandleData *>(bind_group.data));
}

void VkRenderingDevice::destroy_bind_group_layout(BindGroupLayoutHandle layout)
{
    if (!layout.is_valid() || layout.data == nullptr)
        return;
    // 调用方必须先销毁引用该布局的 bind group 与 pipeline layout（非拥有指针）。
    memdelete(static_cast<VkBindGroupLayoutHandleData *>(layout.data));
}

void VkRenderingDevice::destroy_pipeline_layout(PipelineLayoutHandle layout)
{
    if (!layout.is_valid() || layout.data == nullptr)
        return;
    memdelete(static_cast<VkPipelineLayoutHandleData *>(layout.data));
}
