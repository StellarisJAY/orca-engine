#include "rhi_validation.h"

#include <cstdio>

namespace
{
    bool fail(std::string &error, const std::string &message)
    {
        error = message;
        return false;
    }

    /// @brief 尺寸允许的最大 mip 层数（含第 0 层）。
    uint32_t max_mip_levels(uint32_t width, uint32_t height)
    {
        uint32_t size = width > height ? width : height;
        uint32_t levels = 1;
        while (size > 1)
        {
            size >>= 1;
            ++levels;
        }
        return levels;
    }

    /// @brief 当前接口允许的纹理用途位。
    ///
    /// STORAGE 在接口里标注为"预留给 future compute / 存储缓冲，暂不要求后端实现"，
    /// 因此本期直接拒绝，避免后端静默忽略未知用途。
    constexpr uint8_t SUPPORTED_TEXTURE_USAGE =
        TextureUsage::TEXTURE_BINDING | TextureUsage::RENDER_ATTACHMENT | TextureUsage::COPY_DST |
        TextureUsage::COPY_SRC;
} // namespace

bool rhi_format_is_depth(Format format)
{
    return format == Format::Depth32Float;
}

bool rhi_format_is_srgb(Format format)
{
    return format == Format::RGBA8UnormSrgb || format == Format::BGRA8UnormSrgb;
}

uint32_t rhi_vertex_format_size(VertexFormat format)
{
    switch (format)
    {
    case VertexFormat::Float32: return 4;
    case VertexFormat::Float32x2: return 8;
    case VertexFormat::Float32x3: return 12;
    case VertexFormat::Float32x4: return 16;
    case VertexFormat::Uint32: return 4;
    case VertexFormat::Uint8x4Normalized: return 4;
    }
    return 0;
}

bool rhi_validate_texture_desc(const TextureDescRefactor &desc, std::string &error)
{
    if (desc.format == Format::Undefined)
        return fail(error, "format is Undefined");
    if (desc.width == 0 || desc.height == 0)
        return fail(error, "width and height must be greater than 0");
    if (desc.samples_count != 1)
        return fail(error, "only samples_count == 1 is supported (no MSAA yet)");
    if (desc.mip_levels == 0)
        return fail(error, "mip_levels must be at least 1");
    if (desc.dim == TextureDimension::Cube && desc.width != desc.height)
        return fail(error, "cubemap textures must be square");
    if (desc.mip_levels > max_mip_levels(desc.width, desc.height))
        return fail(error, "mip_levels exceeds the maximum for the given size");
    if (desc.usage == 0)
        return fail(error, "usage must not be empty");
    if ((desc.usage & ~SUPPORTED_TEXTURE_USAGE) != 0)
        return fail(error, "usage contains unknown or currently unsupported bits");
    return true;
}

bool rhi_validate_texture_view_desc(const RhiTextureInfo &texture, const TextureViewDesc &desc,
                                    std::string &error)
{
    if (texture.mip_levels == 0)
        return fail(error, "the parent texture has no mip level");

    if (rhi_format_is_depth(texture.format))
    {
        if (desc.aspect != TextureAspect::DepthOnly)
            return fail(error, "a depth texture can only be viewed with the DepthOnly aspect");
    }
    else if (desc.aspect != TextureAspect::Color)
    {
        return fail(error, "a color texture can only be viewed with the Color aspect");
    }

    if (texture.dim == TextureDimension::Cube)
    {
        if (desc.dim != TextureViewDimension::Cube)
            return fail(error, "a cubemap texture can only be viewed as Cube");

        if (desc.base_layer >= texture.layers)
            return fail(error, "base_layer is out of range");
        const uint32_t layer_count = desc.layer_count == 0 ? texture.layers - desc.base_layer : desc.layer_count;
        if (layer_count == 0)
            return fail(error, "layer_count resolves to 0");
        if (desc.base_layer + layer_count > texture.layers)
            return fail(error, "layer range exceeds the texture layers");
    }
    else
    {
        if (desc.dim != TextureViewDimension::Texture2D)
            return fail(error, "a 2D texture can only be viewed as Texture2D");
        if (desc.base_layer != 0 || (desc.layer_count != 0 && desc.layer_count != 1))
            return fail(error, "a 2D texture supports only a single layer");
    }

    if (desc.base_mip >= texture.mip_levels)
        return fail(error, "base_mip is out of range");
    const uint32_t mip_count = desc.mip_count == 0 ? texture.mip_levels - desc.base_mip : desc.mip_count;
    if (mip_count == 0)
        return fail(error, "mip_count resolves to 0");
    if (desc.base_mip + mip_count > texture.mip_levels)
        return fail(error, "mip range exceeds the texture mip levels");

    return true;
}

bool rhi_validate_sampler_desc(const SamplerDesc &desc, std::string &error)
{
    if (desc.lod_min_clamp < 0.0f)
        return fail(error, "lod_min_clamp must not be negative");
    if (desc.lod_max_clamp < desc.lod_min_clamp)
        return fail(error, "lod_max_clamp must be greater than or equal to lod_min_clamp");
    return true;
}

bool rhi_validate_render_pipeline_desc(const RenderPipelineDesc &desc, std::string &error)
{
    if (!desc.pipeline_layout.is_valid())
        return fail(error, "pipeline_layout handle is invalid");
    if (!desc.vertex.module.is_valid())
        return fail(error, "vertex shader module is missing");
    if (desc.fragment.targets.empty() && desc.depth_stencil.format == Format::Undefined)
        return fail(error, "the pipeline declares neither a color target nor a depth format");
    if (!desc.fragment.targets.empty() && !desc.fragment.module.is_valid())
        return fail(error, "fragment shader module is missing while color targets are declared");
    if (desc.samples_count != 1)
        return fail(error, "only samples_count == 1 is supported (no MSAA yet)");

    for (size_t i = 0; i < desc.fragment.targets.size(); ++i)
    {
        const ColorTargetState &target = desc.fragment.targets[i];
        if (target.format == Format::Undefined)
            return fail(error, "color target " + std::to_string(i) + ": format is Undefined");
        if (rhi_format_is_depth(target.format))
            return fail(error, "color target " + std::to_string(i) + ": format must not be a depth format");
    }

    if (desc.depth_stencil.format != Format::Undefined && !rhi_format_is_depth(desc.depth_stencil.format))
        return fail(error, "depth_stencil: format must be a depth format");

    // 当前两个后端的每条命令只支持一个顶点缓冲 slot（与 pass_set_vertex_buffer 的约定一致）。
    if (desc.vertex.buffers.size() > 1)
        return fail(error, "only one vertex buffer is supported for now");

    bool used_locations[16] = {};
    for (size_t buffer_index = 0; buffer_index < desc.vertex.buffers.size(); ++buffer_index)
    {
        const BufferLayout &buffer = desc.vertex.buffers[buffer_index];
        const std::string prefix = "vertex buffer " + std::to_string(buffer_index);

        if (buffer.array_stride == 0)
            return fail(error, prefix + ": array_stride must be greater than 0");
        if (buffer.attributes.empty())
            return fail(error, prefix + ": at least one attribute is required");

        for (const BufferLayout::Attribute &attribute : buffer.attributes)
        {
            const uint32_t size = rhi_vertex_format_size(attribute.format);
            if (size == 0)
                return fail(error, prefix + ": unknown vertex format");
            if (attribute.shader_location >= 16)
                return fail(error, prefix + ": shader_location must be less than 16");
            if (used_locations[attribute.shader_location])
                return fail(error, prefix + ": shader_location " + std::to_string(attribute.shader_location) +
                                       " is declared more than once");
            used_locations[attribute.shader_location] = true;
            if (static_cast<uint64_t>(attribute.offset) + size > buffer.array_stride)
                return fail(error, prefix + ": attribute overruns array_stride at shader_location " +
                                       std::to_string(attribute.shader_location));
        }
    }

    return true;
}
