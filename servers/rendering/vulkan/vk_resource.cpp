#include "vk_rendering_device.h"
#include "vk_internal.h"
#include "vk_format.h"
#include "vk_image_layout.h"
#include "vk_memory.h"
#include "rhi_validation.h"
#include "os/memory.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace
{
    void print_vk_error(const char *what, const vk::SystemError &error)
    {
        std::printf("[ERROR] Vulkan %s failed: %s\n", what, error.what());
    }

    vk::SamplerAddressMode get_vk_sampler_address_mode(AddressMode orca_mode)
    {
        switch (orca_mode)
        {
        case AddressMode::Repeat:
            return vk::SamplerAddressMode::eRepeat;
        case AddressMode::ClampToEdge:
            return vk::SamplerAddressMode::eClampToEdge;
        case AddressMode::MirrorRepeat:
            return vk::SamplerAddressMode::eMirroredRepeat;
        default:
            return vk::SamplerAddressMode::eRepeat;
        }
    }

    vk::Filter get_vk_filter(FilterMode orca_filter)
    {
        switch (orca_filter)
        {
        case FilterMode::Linear:
            return vk::Filter::eLinear;
        case FilterMode::Nearest:
            return vk::Filter::eNearest;
        default:
            return vk::Filter::eLinear;
        }
    }

    /// @brief 采样器创建日志用的可读名字。
    const char *filter_name(FilterMode filter)
    {
        return filter == FilterMode::Nearest ? "nearest" : "linear";
    }
} // namespace

/// @brief 从预编译的 SPIR-V 二进制创建 shader 模块。
///
/// 修复内容（V4 / D-11 / D-27）：
///   - 源类型必须显式声明为 SPIRV，GLSL 文本在这里明确拒绝（Vulkan 不编译 GLSL）；
///   - 先用共享校验 `rhi_validate_shader_module_desc` 检查长度/对齐/magic/版本/
///     入口点存在性与 stage 一致性；
///   - `pCode` 指向本地 `std::vector<uint32_t>`，不再把 `std::string::data()`
///     直接 reinterpret_cast（后者不保证 4 字节对齐，属未定义行为）。
ShaderHandle VkRenderingDevice::create_shader_module(const ShaderModuleDesc &desc)
{
    ShaderHandle handle;

    std::string validation_error;
    if (!rhi_validate_shader_module_desc(desc, validation_error))
    {
        std::printf("[ERROR] Vulkan create_shader_module(%s): %s\n", desc.name.c_str(), validation_error.c_str());
        return handle;
    }

    if (desc.source_kind != ShaderSourceKind::SPIRV)
    {
        std::printf("[ERROR] Vulkan create_shader_module(%s): GLSL source is not accepted; compile it to SPIR-V "
                    "first (glslc --target-env=vulkan1.1) and set source_kind = ShaderSourceKind::SPIRV\n",
                    desc.name.c_str());
        return handle;
    }

    // 校验已保证 size 是 4 的倍数；vector 的数据地址天然满足 pCode 的对齐要求。
    std::vector<uint32_t> code(desc.source.size() / sizeof(uint32_t));
    std::memcpy(code.data(), desc.source.data(), desc.source.size());

    VkShaderModuleData *shader_module_data = nullptr;
    try
    {
        vk::ShaderModuleCreateInfo create_info;
        create_info.codeSize = desc.source.size();
        create_info.pCode = code.data();

        shader_module_data = memnew(VkShaderModuleData);
        shader_module_data->module = device.createShaderModule(create_info);
        shader_module_data->type = desc.type;
        shader_module_data->entry_point = desc.entry_point;

        handle.handle = alloc_handle_id();
        handle.data = shader_module_data;
    }
    catch (const vk::SystemError &error)
    {
        print_vk_error(("create_shader_module(" + desc.name + ")").c_str(), error);
        memdelete(shader_module_data);
        return {};
    }
    return handle;
}

void VkRenderingDevice::destroy_shader_module(ShaderHandle handle)
{
    if (!handle.is_valid() || handle.data == nullptr)
        return;

    // handle.data 实际指向 VkShaderModuleData；原实现 cast 成 VkShaderModule 是
    // 未定义行为（会按错误类型析构并泄漏内部 RAII module）。
    memdelete(static_cast<VkShaderModuleData *>(handle.data));
}

/// @brief 创建 image 并分配、绑定显存。
///
/// 本期使用 vk_memory 的"每资源独占 allocation"（G-06 决策）：image 创建后按
/// vkGetImageMemoryRequirements2 的结果分配一块设备本地显存并绑定。创建后的
/// image 处于 eUndefined 布局、内容未定义；内容由 queue_write_texture 上传，
/// mip 层由 generate_mip_map 生成（两者都会把整张图切到 resting_layout）。
TextureHandle VkRenderingDevice::create_texture(const TextureDescRefactor &desc)
{
    TextureHandle texture;

    std::string error;
    if (!rhi_validate_texture_desc(desc, error))
    {
        std::printf("[ERROR] Vulkan create_texture(%s): %s\n", desc.label.c_str(), error.c_str());
        return texture;
    }

    vk::Format format = vk::Format::eUndefined;
    if (!vk_format_info(desc.format, format))
    {
        std::printf("[ERROR] Vulkan create_texture(%s): unsupported format\n", desc.label.c_str());
        return texture;
    }

    VkTextureHandleData *data = nullptr;
    try
    {
        vk::ImageCreateInfo create_info;
        create_info.mipLevels = desc.mip_levels;
        create_info.arrayLayers = desc.dim == TextureDimension::Cube ? 6u : 1u;
        create_info.format = format;
        create_info.usage = vk_image_usage_from_flags(desc.usage, desc.format);
        create_info.samples = vk::SampleCountFlagBits::e1;
        // 目前所有纹理都按最优布局创建；需要 CPU 直接映射的线性布局留给后续 staging。
        create_info.tiling = vk::ImageTiling::eOptimal;
        create_info.initialLayout = vk::ImageLayout::eUndefined;

        vk::Extent3D extent;
        extent.width = desc.width;
        extent.height = desc.dim == TextureDimension::Texture1D ? 1u : desc.height;
        extent.depth = 1u;

        switch (desc.dim)
        {
        case TextureDimension::Texture1D:
            create_info.imageType = vk::ImageType::e1D;
            break;
        case TextureDimension::Texture2D:
            create_info.imageType = vk::ImageType::e2D;
            break;
        case TextureDimension::Cube:
            // cubemap 是 2D 图像的 6 层数组，不是 3D 图像。
            create_info.imageType = vk::ImageType::e2D;
            create_info.flags = vk::ImageCreateFlagBits::eCubeCompatible;
            break;
        }
        create_info.setExtent(extent);

        data = memnew(VkTextureHandleData);
        data->image = device.createImage(create_info);

        // 显存分配：按 image 自身的内存需求选择类型位，并要求设备本地内存。
        // 每张纹理独占一块 allocation（见 vk_memory.h 的 G-06 说明）。这里同时查询
        // dedicated 需求，驱动要求/倾向独占分配时把 VkMemoryDedicatedAllocateInfo
        // 链进 vkAllocateMemory，避免 validation 报警。
        vk::ImageMemoryRequirementsInfo2 image_requirements_info;
        image_requirements_info.setImage(*data->image);
        auto requirements_chain =
            device.getImageMemoryRequirements2<vk::MemoryRequirements2, vk::MemoryDedicatedRequirements>(
                image_requirements_info);
        const vk::MemoryRequirements &memory_requirements =
            requirements_chain.get<vk::MemoryRequirements2>().memoryRequirements;
        const vk::MemoryDedicatedRequirements &dedicated_requirements =
            requirements_chain.get<vk::MemoryDedicatedRequirements>();

        vk::MemoryDedicatedAllocateInfo dedicated_info;
        dedicated_info.setImage(*data->image);
        const vk::MemoryDedicatedAllocateInfo *dedicated_info_ptr = nullptr;
        if (dedicated_requirements.requiresDedicatedAllocation || dedicated_requirements.prefersDedicatedAllocation)
            dedicated_info_ptr = &dedicated_info;

        data->memory = vk_memory::allocate_device_memory(device, memory_properties, memory_requirements,
                                                         vk::MemoryPropertyFlagBits::eDeviceLocal,
                                                         dedicated_info_ptr);
        if (data->memory == nullptr)
        {
            std::printf("[ERROR] Vulkan create_texture(%s): failed to allocate device memory\n", desc.label.c_str());
            memdelete(data);
            data = nullptr;
            return {};
        }

        try
        {
            (*device).bindImageMemory(*data->image, *data->memory, 0);
        }
        catch (const vk::SystemError &error)
        {
            print_vk_error(("bind_image_memory(" + desc.label + ")").c_str(), error);
            memdelete(data);
            data = nullptr;
            return {};
        }
        data->allocation_size = memory_requirements.size;

        data->format = desc.format;
        data->dim = desc.dim;
        data->width = desc.width;
        data->height = desc.height;
        data->mip_levels = desc.mip_levels;
        data->layers = create_info.arrayLayers;
        data->usage = desc.usage;
        data->label = desc.label;
        // layout 是显式状态：创建后整张图处于 eUndefined，上传/mipmap 结束后回到
        // resting_layout。后续 render pass 必须从这里读取当前布局（见 vk_image_layout.h）。
        data->resting_layout = vk_image_resting_layout(desc.format, desc.usage);
        data->current_layout = vk::ImageLayout::eUndefined;

        texture.handle = alloc_handle_id();
        texture.data = data;

        std::printf("[INFO] Vulkan created texture(%s): %ux%u, %u mip(s), %u layer(s), %.2f KiB\n",
                    desc.label.c_str(), desc.width, desc.height, desc.mip_levels, data->layers,
                    static_cast<double>(data->allocation_size) / 1024.0);
    }
    catch (const vk::SystemError &error)
    {
        print_vk_error(("create_texture(" + desc.label + ")").c_str(), error);
        memdelete(data);
        return {};
    }
    return texture;
}

/// @brief 上传纹理内容（staging → vkCmdCopyBufferToImage），同步提交。
///
/// 时序：接口约定"先写入、后提交的数据对本次提交的绘制可见"。Vulkan 这里用一次性
/// 命令缓冲立即提交并等待 fence（见 submit_immediate_commands），因此上传一定发生在
/// 之后提交的任何帧之前；不要求调用方先开 encoder。
///
/// 布局：整张图 eUndefined/current_layout → eTransferDstOptimal → resting_layout。
/// 上传单个 mip/层时也对整张图做 barrier：纹理的布局状态按"整图统一"记录，
/// 未上传的 mip/层内容仍是未定义，但布局已被定义，采样不会产生 layout 错误。
void VkRenderingDevice::queue_write_texture(TextureHandle texture, const TextureWriteDesc &desc)
{
    if (!texture.is_valid() || texture.data == nullptr)
    {
        std::printf("[ERROR] Vulkan queue_write_texture: invalid texture handle\n");
        return;
    }
    auto texture_data = static_cast<VkTextureHandleData *>(texture.data);

    if ((texture_data->usage & TextureUsage::COPY_DST) == 0)
    {
        std::printf("[ERROR] Vulkan queue_write_texture(%s): texture was not created with COPY_DST usage\n",
                    texture_data->label.c_str());
        return;
    }
    if (desc.data == nullptr)
    {
        std::printf("[ERROR] Vulkan queue_write_texture(%s): data pointer is null\n", texture_data->label.c_str());
        return;
    }
    if (desc.mip_level >= texture_data->mip_levels)
    {
        std::printf("[ERROR] Vulkan queue_write_texture(%s): mip_level %u is out of range (mip_levels = %u)\n",
                    texture_data->label.c_str(), desc.mip_level, texture_data->mip_levels);
        return;
    }

    const vk::Extent2D mip_extent = vk_mip_extent(texture_data->width, texture_data->height, desc.mip_level);
    const uint32_t upload_width = desc.width == 0 ? mip_extent.width : desc.width;
    const uint32_t upload_height = desc.height == 0 ? mip_extent.height : desc.height;
    if (upload_width == 0 || upload_height == 0)
    {
        std::printf("[ERROR] Vulkan queue_write_texture(%s): upload region is empty\n", texture_data->label.c_str());
        return;
    }
    if (static_cast<uint64_t>(desc.origin_x) + upload_width > mip_extent.width ||
        static_cast<uint64_t>(desc.origin_y) + upload_height > mip_extent.height)
    {
        std::printf("[ERROR] Vulkan queue_write_texture(%s): upload region [%u, %u, %u, %u] exceeds mip %u size %ux%u\n",
                    texture_data->label.c_str(), desc.origin_x, desc.origin_y, upload_width, upload_height,
                    desc.mip_level, mip_extent.width, mip_extent.height);
        return;
    }
    if (desc.array_layer >= texture_data->layers)
    {
        std::printf("[ERROR] Vulkan queue_write_texture(%s): array layer / cubemap face %u is out of range (layers = %u)\n",
                    texture_data->label.c_str(), desc.array_layer, texture_data->layers);
        return;
    }

    const uint32_t texel_size = rhi_format_texel_size(texture_data->format);
    if (texel_size == 0)
    {
        std::printf("[ERROR] Vulkan queue_write_texture(%s): format has no texel size mapping\n",
                    texture_data->label.c_str());
        return;
    }

    // bytes_per_row 约定：0 表示紧密打包；否则必须是像素字节数的整数倍且不小于
    // 紧密行宽。Vulkan 的 bufferRowLength 以 texel 计数，这里先把数据按行重排成
    // 紧密打包，避免对齐/步长差异传给驱动。
    const uint32_t tight_row_bytes = upload_width * texel_size;
    uint32_t source_row_pitch = tight_row_bytes;
    if (desc.bytes_per_row != 0)
    {
        if (desc.bytes_per_row % texel_size != 0)
        {
            std::printf("[ERROR] Vulkan queue_write_texture(%s): bytes_per_row must be a multiple of %u\n",
                        texture_data->label.c_str(), texel_size);
            return;
        }
        if (desc.bytes_per_row < tight_row_bytes)
        {
            std::printf("[ERROR] Vulkan queue_write_texture(%s): bytes_per_row %u is smaller than %u required bytes\n",
                        texture_data->label.c_str(), desc.bytes_per_row, tight_row_bytes);
            return;
        }
        source_row_pitch = desc.bytes_per_row;
    }

    const uint64_t required =
        static_cast<uint64_t>(upload_height - 1) * source_row_pitch + tight_row_bytes;
    if (desc.data_size < required)
    {
        std::printf("[ERROR] Vulkan queue_write_texture(%s): data_size %llu is smaller than the required %llu bytes\n",
                    texture_data->label.c_str(), static_cast<unsigned long long>(desc.data_size),
                    static_cast<unsigned long long>(required));
        return;
    }

    const vk::DeviceSize staging_size = static_cast<vk::DeviceSize>(tight_row_bytes) * upload_height;
    const vk::ImageAspectFlags aspect = vk_image_aspect(texture_data->format);
    const std::string what = "queue_write_texture(" + texture_data->label + ")";

    try
    {
        // 声明顺序保证析构逆序：staging_buffer 先于它绑定的 staging_memory 释放。
        vk::raii::DeviceMemory staging_memory { nullptr };
        vk::raii::Buffer staging_buffer = device.createBuffer(
            vk::BufferCreateInfo{vk::BufferCreateFlags{}, staging_size, vk::BufferUsageFlagBits::eTransferSrc});

        const vk::MemoryRequirements staging_requirements = (*device).getBufferMemoryRequirements(*staging_buffer);
        // staging 必须是 host-visible；严格模式：不满足就报错，而不是映射一块不可写内存。
        staging_memory = vk_memory::allocate_device_memory(
            device, memory_properties, staging_requirements,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent, nullptr,
            /*allow_fallback=*/false);
        if (staging_memory == nullptr)
        {
            std::printf("[ERROR] Vulkan %s: failed to allocate host-visible staging memory\n", what.c_str());
            return;
        }
        (*device).bindBufferMemory(*staging_buffer, *staging_memory, 0);

        void *mapped = staging_memory.mapMemory(0, staging_size);
        const uint8_t *source = static_cast<const uint8_t *>(desc.data);
        uint8_t *destination = static_cast<uint8_t *>(mapped);
        for (uint32_t row = 0; row < upload_height; ++row)
        {
            std::memcpy(destination + static_cast<uint64_t>(row) * tight_row_bytes,
                        source + static_cast<uint64_t>(row) * source_row_pitch, tight_row_bytes);
        }
        // 无论实际内存类型是否 coherent，刷一次都合法；非 coherent 时才真正生效。
        vk::MappedMemoryRange mapped_range;
        mapped_range.setMemory(*staging_memory);
        mapped_range.setOffset(0);
        mapped_range.setSize(VK_WHOLE_SIZE);
        device.flushMappedMemoryRanges(mapped_range);
        staging_memory.unmapMemory();

        const bool submitted = submit_immediate_commands(
            what.c_str(), [&](const vk::raii::CommandBuffer &command_buffer) {
                vk_cmd_image_layout_barrier(*command_buffer, *texture_data->image, texture_data->current_layout,
                                            vk::ImageLayout::eTransferDstOptimal, aspect, 0,
                                            texture_data->mip_levels, 0, texture_data->layers);

                vk::BufferImageCopy copy_region;
                copy_region.setBufferOffset(0);
                copy_region.setBufferRowLength(0); // 0 = staging 已按行紧密打包
                copy_region.setBufferImageHeight(0);
                copy_region.imageSubresource.setAspectMask(aspect);
                copy_region.imageSubresource.setMipLevel(desc.mip_level);
                copy_region.imageSubresource.setBaseArrayLayer(desc.array_layer);
                copy_region.imageSubresource.setLayerCount(1);
                copy_region.setImageOffset(vk::Offset3D{static_cast<int32_t>(desc.origin_x),
                                                       static_cast<int32_t>(desc.origin_y), 0});
                copy_region.setImageExtent(vk::Extent3D{upload_width, upload_height, 1});
                (*command_buffer).copyBufferToImage(*staging_buffer, *texture_data->image,
                                                    vk::ImageLayout::eTransferDstOptimal, copy_region);

                vk_cmd_image_layout_barrier(*command_buffer, *texture_data->image,
                                            vk::ImageLayout::eTransferDstOptimal, texture_data->resting_layout,
                                            aspect, 0, texture_data->mip_levels, 0, texture_data->layers);
            });

        if (!submitted)
        {
            std::printf("[ERROR] Vulkan %s: failed to submit upload commands\n", what.c_str());
            return;
        }
        texture_data->current_layout = texture_data->resting_layout;
    }
    catch (const vk::SystemError &error)
    {
        print_vk_error(what.c_str(), error);
    }
}

/// @brief 生成完整 mip 链（vkCmdBlitImage 逐级下采样），同步提交。
void VkRenderingDevice::generate_mip_map(TextureHandle texture)
{
    if (!texture.is_valid() || texture.data == nullptr)
    {
        std::printf("[ERROR] Vulkan generate_mip_map: invalid texture handle\n");
        return;
    }
    auto texture_data = static_cast<VkTextureHandleData *>(texture.data);
    if (texture_data->mip_levels <= 1)
        return; // 单层纹理没有 mip 可生成

    // blit 需要 image 在创建时就带 TRANSFER_SRC / TRANSFER_DST 用途；usage 位在
    // create_texture 时保存，VkImage 本身不记录，必须在这里显式校验。
    if ((texture_data->usage & TextureUsage::COPY_SRC) == 0 ||
        (texture_data->usage & TextureUsage::COPY_DST) == 0)
    {
        std::printf("[ERROR] Vulkan generate_mip_map(%s): texture was not created with COPY_SRC | COPY_DST usage\n",
                    texture_data->label.c_str());
        return;
    }

    vk::Format format = vk::Format::eUndefined;
    if (!vk_format_info(texture_data->format, format))
    {
        std::printf("[ERROR] Vulkan generate_mip_map(%s): unsupported format\n", texture_data->label.c_str());
        return;
    }

    // blit 能力因格式/驱动而异（深度格式尤其常见），不支持时明确告警而不是让
    // validation layer 在录制阶段报错。
    const vk::FormatProperties format_properties = physical_device.getFormatProperties(format);
    const vk::FormatFeatureFlags features = format_properties.optimalTilingFeatures;
    if ((features & vk::FormatFeatureFlagBits::eBlitSrc) == vk::FormatFeatureFlags{} ||
        (features & vk::FormatFeatureFlagBits::eBlitDst) == vk::FormatFeatureFlags{})
    {
        std::printf("[WARN] Vulkan generate_mip_map(%s): format does not support blit as src+dst, mips left undefined\n",
                    texture_data->label.c_str());
        return;
    }
    // 有线性过滤能力时用 eLinear（默认的双线性下采样），否则退化为 eNearest。
    const bool linear_filter =
        (features & vk::FormatFeatureFlagBits::eSampledImageFilterLinear) != vk::FormatFeatureFlags{};
    const vk::Filter filter = linear_filter ? vk::Filter::eLinear : vk::Filter::eNearest;

    const vk::ImageAspectFlags aspect = vk_image_aspect(texture_data->format);
    const vk::ImageLayout resting_layout = texture_data->resting_layout;
    const std::string what = "generate_mip_map(" + texture_data->label + ")";

    const bool submitted = submit_immediate_commands(what.c_str(), [&](const vk::raii::CommandBuffer &command_buffer) {
        // 先把整张图切到 TRANSFER_DST，作为逐级 blit 的起点。
        vk_cmd_image_layout_barrier(*command_buffer, *texture_data->image, texture_data->current_layout,
                                    vk::ImageLayout::eTransferDstOptimal, aspect, 0, texture_data->mip_levels, 0,
                                    texture_data->layers);

        // 逐级下采样：第 level-1 层先切到 TRANSFER_SRC，blit 到仍处于 TRANSFER_DST 的
        // 第 level 层，再立刻把第 level-1 层切回 resting_layout（它已经完成上采样，
        // 可以立刻被采样）。最后一层在循环外统一切回 resting_layout。
        for (uint32_t level = 1; level < texture_data->mip_levels; ++level)
        {
            vk_cmd_image_layout_barrier(*command_buffer, *texture_data->image,
                                        vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eTransferSrcOptimal,
                                        aspect, level - 1, 1, 0, texture_data->layers);

            const vk::Extent2D source_extent = vk_mip_extent(texture_data->width, texture_data->height, level - 1);
            const vk::Extent2D destination_extent = vk_mip_extent(texture_data->width, texture_data->height, level);
            vk::ImageBlit blit_region;
            blit_region.srcSubresource.setAspectMask(aspect);
            blit_region.srcSubresource.setMipLevel(level - 1);
            blit_region.srcSubresource.setBaseArrayLayer(0);
            blit_region.srcSubresource.setLayerCount(texture_data->layers);
            blit_region.srcOffsets[0] = vk::Offset3D{0, 0, 0};
            blit_region.srcOffsets[1] = vk::Offset3D{static_cast<int32_t>(source_extent.width),
                                                     static_cast<int32_t>(source_extent.height), 1};
            blit_region.dstSubresource.setAspectMask(aspect);
            blit_region.dstSubresource.setMipLevel(level);
            blit_region.dstSubresource.setBaseArrayLayer(0);
            blit_region.dstSubresource.setLayerCount(texture_data->layers);
            blit_region.dstOffsets[0] = vk::Offset3D{0, 0, 0};
            blit_region.dstOffsets[1] = vk::Offset3D{static_cast<int32_t>(destination_extent.width),
                                                     static_cast<int32_t>(destination_extent.height), 1};
            (*command_buffer).blitImage(*texture_data->image, vk::ImageLayout::eTransferSrcOptimal,
                                        *texture_data->image, vk::ImageLayout::eTransferDstOptimal, blit_region,
                                        filter);

            vk_cmd_image_layout_barrier(*command_buffer, *texture_data->image,
                                        vk::ImageLayout::eTransferSrcOptimal, resting_layout, aspect, level - 1, 1, 0,
                                        texture_data->layers);
        }

        vk_cmd_image_layout_barrier(*command_buffer, *texture_data->image, vk::ImageLayout::eTransferDstOptimal,
                                    resting_layout, aspect, texture_data->mip_levels - 1, 1, 0, texture_data->layers);
    });

    if (!submitted)
    {
        std::printf("[ERROR] Vulkan %s: failed to submit mipmap commands\n", what.c_str());
        return;
    }
    texture_data->current_layout = resting_layout;
    std::printf("[INFO] Vulkan generated mip maps(%s): %u level(s)\n", texture_data->label.c_str(),
                texture_data->mip_levels);
}

/// @brief 创建纹理视图。
///
/// 范围解析全部走共享层 `rhi_resolve_texture_view_range`：
/// 这里只负责把解析结果映射成 `VkImageViewCreateInfo`。视图格式固定为父纹理格式
/// （本期不支持 sRGB ↔ 非 sRGB 重解释视图，那需要额外校验格式兼容性）。
TextureViewHandle VkRenderingDevice::create_texture_view(TextureHandle texture, const TextureViewDesc &desc)
{
    TextureViewHandle texture_view;
    if (!texture.is_valid() || texture.data == nullptr)
    {
        std::printf("[ERROR] Vulkan create_texture_view: invalid texture handle\n");
        return texture_view;
    }

    const auto texture_data = static_cast<VkTextureHandleData *>(texture.data);
    RhiTextureInfo texture_info;
    texture_info.format = texture_data->format;
    texture_info.dim = texture_data->dim;
    texture_info.width = texture_data->width;
    texture_info.height = texture_data->height;
    texture_info.mip_levels = texture_data->mip_levels;
    texture_info.layers = texture_data->layers;

    RhiTextureViewRange range;
    std::string error;
    if (!rhi_resolve_texture_view_range(texture_info, desc, range, error))
    {
        std::printf("[ERROR] Vulkan create_texture_view(%s): %s\n", texture_data->label.c_str(), error.c_str());
        return texture_view;
    }

    vk::Format view_format = vk::Format::eUndefined;
    if (!vk_format_info(texture_data->format, view_format))
    {
        std::printf("[ERROR] Vulkan create_texture_view(%s): unsupported parent format\n",
                    texture_data->label.c_str());
        return texture_view;
    }

    // aspect 由父纹理格式决定（共享校验已保证 desc.aspect 与格式一致）：深度格式只能
    // 用 eDepth 作 aspect，颜色格式用 eColor。不要把两者按 desc 直接传，否则深度纹理
    // 用 Color 视图的路径会绕过 eDepth 分支。
    const vk::ImageAspectFlags aspect = vk_image_aspect(texture_data->format);

    VkTextureViewHandleData *data = nullptr;
    try
    {
        vk::ImageViewCreateInfo create_info;
        create_info.setImage(texture_data->image);
        create_info.viewType =
            range.dim == TextureViewDimension::Cube ? vk::ImageViewType::eCube : vk::ImageViewType::e2D;
        create_info.format = view_format;
        create_info.subresourceRange.aspectMask = aspect;
        create_info.subresourceRange.baseMipLevel = range.base_mip;
        create_info.subresourceRange.levelCount = range.mip_count;
        create_info.subresourceRange.baseArrayLayer = range.base_layer;
        create_info.subresourceRange.layerCount = range.layer_count;

        data = memnew(VkTextureViewHandleData);
        data->image_view = device.createImageView(create_info);
        data->label = texture_data->label;
        data->format = texture_data->format;
        data->dim = range.dim;
        data->aspect = range.aspect;
        data->base_mip = range.base_mip;
        data->mip_count = range.mip_count;
        data->base_layer = range.base_layer;
        data->layer_count = range.layer_count;

        texture_view.handle = alloc_handle_id();
        texture_view.data = data;
    }
    catch (const vk::SystemError &vk_error)
    {
        print_vk_error(("create_texture_view(" + texture_data->label + ")").c_str(), vk_error);
        memdelete(data);
        return {};
    }

    std::printf("[INFO] Vulkan created texture view(%s): %s, mip %u..%u, layer %u..%u\n",
                texture_data->label.c_str(),
                range.dim == TextureViewDimension::Cube ? "Cube" : "2D", range.base_mip,
                range.base_mip + range.mip_count - 1, range.base_layer, range.base_layer + range.layer_count - 1);
    return texture_view;
}

/// @brief 创建 Vulkan sampler。
SamplerHandle VkRenderingDevice::create_sampler(const SamplerDesc &desc)
{
    SamplerHandle sampler_handle;

    std::string error;
    if (!rhi_validate_sampler_desc(desc, error))
    {
        std::printf("[ERROR] Vulkan create_sampler: %s\n", error.c_str());
        return sampler_handle;
    }

    VkSamplerHandleData *sampler_data = nullptr;
    try
    {
        vk::SamplerCreateInfo create_info;
        create_info.addressModeU = get_vk_sampler_address_mode(desc.address_u);
        create_info.addressModeV = get_vk_sampler_address_mode(desc.address_v);
        create_info.addressModeW = get_vk_sampler_address_mode(desc.address_w);
        create_info.magFilter = get_vk_filter(desc.mag_filter);
        create_info.minFilter = get_vk_filter(desc.min_filter);

        if (desc.mipmap_filter == FilterMode::Linear)
            create_info.setMipmapMode(vk::SamplerMipmapMode::eLinear);
        else if (desc.mipmap_filter == FilterMode::Nearest)
            create_info.setMipmapMode(vk::SamplerMipmapMode::eNearest);

        create_info.setMinLod(desc.lod_min_clamp);
        create_info.setMaxLod(desc.lod_max_clamp);
        create_info.setCompareEnable(desc.compare_enable);
        // compareOp 即使不启用比较也必须给合法值（vk::SamplerCreateInfo 的默认是 eNever，
        // 但为了与 WebGPU 默认一致，统一从 desc.compare_op 映射）。
        create_info.setCompareOp(vk_compare_op_from_function(desc.compare_op));

        sampler_data = memnew(VkSamplerHandleData);
        sampler_data->sampler = device.createSampler(create_info);
        sampler_data->desc = desc;

        sampler_handle.data = sampler_data;
        sampler_handle.handle = alloc_handle_id();

        std::printf("[INFO] Vulkan created sampler: min/mag = %s/%s, mipmap = %s, compare = %s\n",
                    filter_name(desc.min_filter), filter_name(desc.mag_filter), filter_name(desc.mipmap_filter),
                    desc.compare_enable ? "enabled" : "disabled");
    }
    catch (const vk::SystemError &error)
    {
        print_vk_error("create_sampler", error);
        memdelete(sampler_data);
        return {};
    }
    return sampler_handle;
}

void VkRenderingDevice::destroy_texture(TextureHandle texture)
{
    if (!texture.is_valid() || texture.data == nullptr)
        return;
    memdelete(static_cast<VkTextureHandleData *>(texture.data));
}

void VkRenderingDevice::destroy_texture_view(TextureViewHandle view)
{
    if (!view.is_valid() || view.data == nullptr)
        return;
    memdelete(static_cast<VkTextureViewHandleData *>(view.data));
}

void VkRenderingDevice::destroy_sampler(SamplerHandle sampler)
{
    if (!sampler.is_valid() || sampler.data == nullptr)
        return;
    memdelete(static_cast<VkSamplerHandleData *>(sampler.data));
}
