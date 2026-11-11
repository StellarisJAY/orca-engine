#include "vk_rendering_device.h"
#include "os/memory.h"

struct VkShaderModule
{
    std::vector<vk::PipelineShaderStageCreateInfo> stages;
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

/// @brief 创建shader
/// @param desc shader描述及源码
/// @return handle
ShaderHandle VkRenderingDevice::create_shader_module(const ShaderModuleDesc &desc)
{
    ShaderHandle handle;
    if (desc.vertex_source.empty() || desc.fragment_source.empty())
    {
        std::printf("[WARN] Vulkan create shader module failed: empty shader source\n");
        return handle;
    }

    // 创建shader模块

    auto shader_module_data = memnew(VkShaderModule);
    vk::ShaderModuleCreateInfo create_info_vert;
    vk::ShaderModuleCreateInfo create_info_frag;

    create_info_vert.codeSize = desc.vertex_source.size();
    create_info_vert.pCode = reinterpret_cast<const uint32_t *>(desc.vertex_source.data());

    create_info_frag.codeSize = desc.fragment_source.size();
    create_info_frag.pCode = reinterpret_cast<const uint32_t *>(desc.fragment_source.data());

    const vk::raii::ShaderModule vert_module = device.createShaderModule(create_info_vert);
    const vk::raii::ShaderModule frag_module = device.createShaderModule(create_info_frag);

    // 创建渲染管线stage

    vk::PipelineShaderStageCreateInfo vert_stage_create_info;
    vert_stage_create_info.stage = vk::ShaderStageFlagBits::eVertex;
    vert_stage_create_info.module = vert_module;
    vert_stage_create_info.pName = desc.name.c_str();

    vk::PipelineShaderStageCreateInfo frag_stage_create_info;
    frag_stage_create_info.stage = vk::ShaderStageFlagBits::eFragment;
    frag_stage_create_info.module = frag_module;
    frag_stage_create_info.pName = desc.name.c_str();

    shader_module_data->stages = {vert_stage_create_info, frag_stage_create_info};
    handle.data = shader_module_data;
    handle.handle = 1;
    return handle;
}

void VkRenderingDevice::destroy_shader_module(ShaderHandle handle)
{
    if (!handle.is_valid() || handle.data == nullptr)
        return;

    if (auto data = static_cast<VkShaderModule *>(handle.data); data)
    {
        memdelete(data);
        handle.handle = 0;
        handle.data = nullptr;
    }
}

vk::Format get_vk_format(Format orca_format)
{
    switch (orca_format)
    {
    case Format::Undefined:
        return vk::Format::eR8G8B8A8Srgb;
    case Format::R8Unorm:
        return vk::Format::eR8Unorm;
    case Format::RG8Unorm:
        return vk::Format::eR8G8Unorm;
    case Format::RGBA8Unorm:
        return vk::Format::eR8G8B8A8Unorm;
    case Format::RGBA8UnormSrgb:
        return vk::Format::eR8G8B8A8Unorm;
    case Format::R16Float:
        return vk::Format::eR16Sfloat;
    case Format::RGBA16Float:
        return vk::Format::eR16G16B16A16Sfloat;
    case Format::Depth32Float:
        return vk::Format::eD32Sfloat;
    }
}

vk::ImageUsageFlags get_vk_image_usage(uint8_t orca_usage_flags)
{
    vk::ImageUsageFlags flags;
    if (orca_usage_flags & TextureUsage::RENDER_ATTACHMENT != 0)
    {
        flags |= vk::ImageUsageFlagBits::eColorAttachment;
    }
    if (orca_usage_flags & TextureUsage::TEXTURE_BINDING != 0)
    {
        flags |= vk::ImageUsageFlagBits::eSampled;
    }
    if (orca_usage_flags & TextureUsage::COPY_SRC != 0)
    {
        flags |= vk::ImageUsageFlagBits::eTransferSrc;
    }
    if (orca_usage_flags & TextureUsage::COPY_DST != 0)
    {
        flags |= vk::ImageUsageFlagBits::eTransferDst;
    }
    if (orca_usage_flags & TextureUsage::STORAGE != 0)
    {
        flags |= vk::ImageUsageFlagBits::eStorage;
    }
    // TODO more flags
    return flags;
}

/// @brief 创建image
/// @param desc image描述
/// @return handle
TextureHandle VkRenderingDevice::create_texture(const TextureDescRefactor &desc)
{
    vk::ImageCreateInfo create_info;
    create_info.mipLevels = desc.mip_levels;
    create_info.format = get_vk_format(desc.format);
    create_info.usage = get_vk_image_usage(desc.usage);

    switch (desc.dim)
    {
    case TextureDimension::Texture1D:
        create_info.imageType = vk::ImageType::e1D;
        break;
    case TextureDimension::Texture2D:
        create_info.imageType = vk::ImageType::e2D;
        break;
    case TextureDimension::Cube:
        create_info.imageType = vk::ImageType::e3D;
        break;
    }

    // 分辨率
    vk::Extent3D extent;
    extent.setWidth(desc.width);
    extent.setHeight(desc.height);
    create_info.setExtent(extent);

    auto data = memnew(VkTextureHandleData);
    data->image = device.createImage(create_info);

    TextureHandle texture;
    texture.handle = 1;
    texture.data = data;
    return texture;
}

void VkRenderingDevice::generate_mip_map(TextureHandle texture)
{
}

/// @brief 创建vulkan ImageView
/// @param texture Image Handle
/// @param desc view描述
/// @return TextureView Handle
TextureViewHandle VkRenderingDevice::create_texture_view(TextureHandle texture, const TextureViewDesc &desc)
{
    TextureViewHandle texture_view;
    if (!texture.is_valid() || texture.data == nullptr)
        return texture_view;

    if (auto texture_data = static_cast<VkTextureHandleData *>(texture.data); texture_data)
    {
        vk::ImageViewCreateInfo create_info;
        create_info.setImage(texture_data->image);
        // TODO image view create info
        auto data = memnew(VkTextureViewHandleData);
        data->image_view = device.createImageView(create_info);
        texture_view.handle = 1;
        texture_view.data = data;
    }
    return texture_view;
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
    }
}

/// @brief 创建vulkan sampler
/// @param desc sampler参数
/// @return sampler handle
SamplerHandle VkRenderingDevice::create_sampler(const SamplerDesc &desc)
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

    auto sampler_data = memnew(VkSamplerHandleData);
    sampler_data->sampler = device.createSampler(create_info);

    SamplerHandle sampler_handle;
    sampler_handle.data = sampler_data;
    sampler_handle.handle = 1;
    return sampler_handle;
}

void VkRenderingDevice::destroy_texture(TextureHandle texture)
{
    if (!texture.is_valid() || texture.data == nullptr)
        return;
    if (auto data = static_cast<VkTextureHandleData *>(texture.data); data)
    {
        memdelete(data);
        texture.handle = 0;
        texture.data = nullptr;
    }
}

void VkRenderingDevice::destroy_texture_view(TextureViewHandle view)
{
    if (!view.is_valid() || view.data == nullptr)
        return;
    if (auto data = static_cast<VkTextureViewHandleData *>(view.data); data)
    {
        memdelete(data);
        view.handle = 0;
        view.data = nullptr;
    }
}

void VkRenderingDevice::destroy_sampler(SamplerHandle sampler)
{
    if (!sampler.is_valid() || sampler.data == nullptr)
        return;
    if (auto data = static_cast<VkSamplerHandleData *>(sampler.data); data)
    {
        memdelete(data);
        sampler.handle = 0;
        sampler.data = nullptr;
    }
}