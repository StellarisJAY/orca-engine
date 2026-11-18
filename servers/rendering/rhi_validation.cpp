#include "rhi_validation.h"

#include <cmath>
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

    /// @brief 当前接口允许的缓冲区用途位。
    ///
    /// STORAGE 与纹理的 STORAGE 一样标注为"预留给 future compute / 存储缓冲"，
    /// 本期直接拒绝，避免后端静默忽略未知用途。
    constexpr uint8_t SUPPORTED_BUFFER_USAGE =
        GPUBufferUsage::VERTEX | GPUBufferUsage::INDEX | GPUBufferUsage::UNIFORM | GPUBufferUsage::COPY_DST |
        GPUBufferUsage::COPY_SRC;

    /// @brief 一个缓冲区只能有一种"主用途"（GL 的目标绑定约束）。
    constexpr uint8_t PRIMARY_BUFFER_USAGE =
        GPUBufferUsage::VERTEX | GPUBufferUsage::INDEX | GPUBufferUsage::UNIFORM | GPUBufferUsage::STORAGE;
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

uint32_t rhi_format_texel_size(Format format)
{
    switch (format)
    {
    case Format::R8Unorm: return 1;
    case Format::RG8Unorm: return 2;
    case Format::R16Float: return 2;
    case Format::RGBA8Unorm:
    case Format::RGBA8UnormSrgb:
    case Format::BGRA8Unorm:
    case Format::BGRA8UnormSrgb:
    case Format::Depth32Float: return 4;
    case Format::RGBA16Float: return 8;
    case Format::Undefined:
    default: return 0;
    }
}

bool rhi_validate_buffer_desc(const GPUBufferDesc &desc, std::string &error)
{
    if (desc.size == 0)
        return fail(error, "size must be greater than 0");
    if (desc.usage == 0)
        return fail(error, "usage must not be empty");
    if ((desc.usage & ~SUPPORTED_BUFFER_USAGE) != 0)
        return fail(error, "usage contains unknown or currently unsupported bits");

    // GL 的一个缓冲对象只能绑定一个目标；两个主用途同时出现会让 gl_buffer_target
    // 静默取第一个，Vulkan 侧也会出现"创建成功但用法不一致"的隐患，因此统一拒绝。
    const uint8_t primary = desc.usage & PRIMARY_BUFFER_USAGE;
    if (primary != 0 && (primary & (primary - 1)) != 0)
        return fail(error, "at most one of VERTEX / INDEX / UNIFORM / STORAGE may be set");
    return true;
}

bool rhi_validate_buffer_write(uint64_t buffer_size, uint64_t offset, uint64_t size, std::string &error)
{
    if (size == 0)
        return fail(error, "write size must be greater than 0");
    if (offset > buffer_size)
        return fail(error, "write offset is beyond the buffer size");
    if (size > buffer_size - offset)
        return fail(error, "write range exceeds the buffer size");
    return true;
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

bool rhi_resolve_texture_view_range(const RhiTextureInfo &texture, const TextureViewDesc &desc,
                                    RhiTextureViewRange &range, std::string &error)
{
    if (texture.mip_levels == 0)
        return fail(error, "the parent texture has no mip level");

    // 1D 纹理没有对应的 TextureViewDimension（接口只暴露 2D/Cube），不能建视图。
    if (texture.dim == TextureDimension::Texture1D)
        return fail(error, "a 1D texture cannot be viewed as Texture2D or Cube");

    // aspect 必须与父纹理的实际格式一致：Vulkan 里颜色图像用 eDepth 作 aspect 是非法
    // 视图，GL 里深度纹理也没法当 color 采样。
    if (rhi_format_is_depth(texture.format))
    {
        if (desc.aspect != TextureAspect::DepthOnly)
            return fail(error, "a depth texture can only be viewed with the DepthOnly aspect");
    }
    else if (desc.aspect != TextureAspect::Color)
    {
        return fail(error, "a color texture can only be viewed with the Color aspect");
    }

    RhiTextureViewRange resolved;
    resolved.dim = desc.dim;
    resolved.aspect = desc.aspect;

    if (texture.dim == TextureDimension::Cube)
    {
        if (desc.dim != TextureViewDimension::Cube)
            return fail(error, "a cubemap texture can only be viewed as Cube");

        if (desc.base_layer >= texture.layers)
            return fail(error, "base_layer is out of range");
        // Vulkan 的 eCube 视图要求 baseArrayLayer 是 6 的倍数；本项目 cubemap 固定 6 层，
        // 实际只允许 base_layer == 0。
        if (desc.base_layer % 6 != 0)
            return fail(error, "a cubemap view's base_layer must be a multiple of 6");
        // 先做减法再比较，避免 base_layer + layer_count 的 uint32 溢出绕过区间检查。
        const uint32_t layer_count = desc.layer_count == 0 ? texture.layers - desc.base_layer : desc.layer_count;
        if (desc.layer_count > texture.layers - desc.base_layer)
            return fail(error, "layer range exceeds the texture layers");
        if (layer_count == 0)
            return fail(error, "layer_count resolves to 0");
        // 同理，eCube 视图的 layerCount 必须是 6 的倍数（单面/半立方体视图在 Vulkan 非法）。
        if (layer_count % 6 != 0)
            return fail(error, "a cubemap view's layer_count must be a multiple of 6");
        resolved.base_layer = desc.base_layer;
        resolved.layer_count = layer_count;
    }
    else
    {
        if (desc.dim != TextureViewDimension::Texture2D)
            return fail(error, "a 2D texture can only be viewed as Texture2D");
        if (desc.base_layer != 0 || (desc.layer_count != 0 && desc.layer_count != 1))
            return fail(error, "a 2D texture supports only a single layer");
        resolved.base_layer = 0;
        resolved.layer_count = 1;
    }

    if (desc.base_mip >= texture.mip_levels)
        return fail(error, "base_mip is out of range");
    const uint32_t mip_count = desc.mip_count == 0 ? texture.mip_levels - desc.base_mip : desc.mip_count;
    if (desc.mip_count > texture.mip_levels - desc.base_mip)
        return fail(error, "mip range exceeds the texture mip levels");
    if (mip_count == 0)
        return fail(error, "mip_count resolves to 0");
    resolved.base_mip = desc.base_mip;
    resolved.mip_count = mip_count;

    range = resolved;
    return true;
}

bool rhi_validate_texture_view_desc(const RhiTextureInfo &texture, const TextureViewDesc &desc,
                                    std::string &error)
{
    RhiTextureViewRange range;
    return rhi_resolve_texture_view_range(texture, desc, range, error);
}

bool rhi_validate_sampler_desc(const SamplerDesc &desc, std::string &error)
{
    // NaN/Inf 能绕过下面的顺序比较（NaN 与任何值比较都是 false），必须显式拒绝。
    if (!std::isfinite(desc.lod_min_clamp))
        return fail(error, "lod_min_clamp must be a finite value");
    if (!std::isfinite(desc.lod_max_clamp))
        return fail(error, "lod_max_clamp must be a finite value");
    if (desc.lod_min_clamp < 0.0f)
        return fail(error, "lod_min_clamp must not be negative");
    if (desc.lod_max_clamp < desc.lod_min_clamp)
        return fail(error, "lod_max_clamp must be greater than or equal to lod_min_clamp");
    // VK_LOD_CLAMP_NONE == 1000.0：Vulkan 要求 maxLod 不超过它（VUID-VkSamplerCreateInfo-maxLod-01973），
    // GL 没有这条限制，但为了让同一个描述在两个后端都合法，统一按 Vulkan 约束校验。
    if (desc.lod_max_clamp > 1000.0f)
        return fail(error, "lod_max_clamp must not exceed 1000.0");
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
