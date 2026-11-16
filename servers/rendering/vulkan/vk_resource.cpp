#include "vk_rendering_device.h"
#include "vk_internal.h"
#include "vk_format.h"
#include "rhi_validation.h"
#include "os/memory.h"

#include <cstdio>
#include <string>
#include <utility>

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
} // namespace

/// @brief 创建 shader 模块。
///
/// 注意：当前仍按接口约定的 `ShaderModuleDesc.source` 解释为 SPIR-V 二进制。
/// GLSL 文本 → SPIR-V 的编译链路见审计清单 D-11（V4 处理）。
ShaderHandle VkRenderingDevice::create_shader_module(const ShaderModuleDesc &desc)
{
    ShaderHandle handle;
    if (desc.source.empty())
    {
        std::printf("[WARN] Vulkan create shader module failed: empty shader source\n");
        return handle;
    }

    VkShaderModuleData *shader_module_data = nullptr;
    try
    {
        vk::ShaderModuleCreateInfo create_info;
        create_info.codeSize = desc.source.size();
        create_info.pCode = reinterpret_cast<const uint32_t *>(desc.source.data());

        shader_module_data = memnew(VkShaderModuleData);
        shader_module_data->module = device.createShaderModule(create_info);
        shader_module_data->type = desc.type;

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

/// @brief 创建 image（本期尚未分配/绑定显存，见审计清单 D-23/V2）。
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

        auto data = memnew(VkTextureHandleData);
        data->image = device.createImage(create_info);
        data->format = desc.format;
        data->dim = desc.dim;
        data->width = desc.width;
        data->height = desc.height;
        data->mip_levels = desc.mip_levels;
        data->layers = create_info.arrayLayers;

        texture.handle = alloc_handle_id();
        texture.data = data;
    }
    catch (const vk::SystemError &error)
    {
        print_vk_error(("create_texture(" + desc.label + ")").c_str(), error);
        memdelete(data);
        return {};
    }
    return texture;
}

void VkRenderingDevice::generate_mip_map(TextureHandle texture)
{
    (void)texture;
    // TODO(V2)：VkCmdBlitImage 逐级下采样。
}

TextureViewHandle VkRenderingDevice::create_texture_view(TextureHandle texture, const TextureViewDesc &desc)
{
    TextureViewHandle texture_view;
    if (!texture.is_valid() || texture.data == nullptr)
        return texture_view;

    const auto texture_data = static_cast<VkTextureHandleData *>(texture.data);
    RhiTextureInfo texture_info;
    texture_info.format = texture_data->format;
    texture_info.dim = texture_data->dim;
    texture_info.width = texture_data->width;
    texture_info.height = texture_data->height;
    texture_info.mip_levels = texture_data->mip_levels;
    texture_info.layers = texture_data->layers;

    std::string error;
    if (!rhi_validate_texture_view_desc(texture_info, desc, error))
    {
        std::printf("[ERROR] Vulkan create_texture_view: %s\n", error.c_str());
        return texture_view;
    }

    vk::Format view_format = vk::Format::eUndefined;
    if (!vk_format_info(texture_data->format, view_format))
    {
        std::printf("[ERROR] Vulkan create_texture_view: unsupported parent format\n");
        return texture_view;
    }

    const uint32_t mip_count = desc.mip_count == 0 ? texture_data->mip_levels - desc.base_mip : desc.mip_count;
    const uint32_t layer_count =
        desc.layer_count == 0 ? texture_data->layers - desc.base_layer : desc.layer_count;

    VkTextureViewHandleData *data = nullptr;
    try
    {
        vk::ImageViewCreateInfo create_info;
        create_info.setImage(texture_data->image);
        create_info.viewType =
            desc.dim == TextureViewDimension::Cube ? vk::ImageViewType::eCube : vk::ImageViewType::e2D;
        create_info.format = view_format;
        create_info.subresourceRange.aspectMask =
            desc.aspect == TextureAspect::DepthOnly ? vk::ImageAspectFlagBits::eDepth
                                                    : vk::ImageAspectFlagBits::eColor;
        create_info.subresourceRange.baseMipLevel = desc.base_mip;
        create_info.subresourceRange.levelCount = mip_count;
        create_info.subresourceRange.baseArrayLayer = desc.base_layer;
        create_info.subresourceRange.layerCount = layer_count;

        data = memnew(VkTextureViewHandleData);
        data->image_view = device.createImageView(create_info);
        texture_view.handle = alloc_handle_id();
        texture_view.data = data;
    }
    catch (const vk::SystemError &error)
    {
        print_vk_error("create_texture_view", error);
        memdelete(data);
        return {};
    }
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
        // TODO(G-05)：接口 SamplerDesc 还没有 compare_op；启用 compare 时需要补 compareOp。

        sampler_data = memnew(VkSamplerHandleData);
        sampler_data->sampler = device.createSampler(create_info);

        sampler_handle.data = sampler_data;
        sampler_handle.handle = alloc_handle_id();
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
