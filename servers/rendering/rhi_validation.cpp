#include "rhi_validation.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace
{
    bool fail(std::string &error, const std::string &message)
    {
        error = message;
        return false;
    }

    /// @brief 当前接口允许的可见阶段位。
    constexpr uint8_t SUPPORTED_SHADER_STAGES = ShaderStage::VERTEX | ShaderStage::FRAGMENT;

    /// @brief 在布局项集合里查找 binding；找不到返回 nullptr。
    const RhiBindGroupLayoutEntry *find_layout_entry(const RhiBindGroupLayoutInfo &layout, uint32_t binding)
    {
        for (const RhiBindGroupLayoutEntry &entry : layout.entries)
        {
            if (entry.binding == binding)
                return &entry;
        }
        return nullptr;
    }

    /// @brief 解析 UniformBuffer 的绑定区间（size == 0 表示到缓冲区末尾）。
    bool resolve_buffer_binding_range(uint64_t buffer_size, uint64_t offset, uint64_t size,
                                      uint64_t &resolved_size, std::string &error)
    {
        if (offset > buffer_size)
            return fail(error, "the binding offset is beyond the buffer size");
        const uint64_t available = buffer_size - offset;
        resolved_size = size == 0 ? available : size;
        if (resolved_size > available)
            return fail(error, "the binding range exceeds the buffer size");
        if (resolved_size == 0)
            return fail(error, "the binding range resolves to 0 bytes");
        return true;
    }

    /// @brief 判断绑定资源是否与布局项的类型匹配（不含 UniformBuffer 区间检查）。
    bool validate_binding_resource(const RhiBindGroupLayoutEntry &layout_entry,
                                   const RhiBindingResourceInfo &resource, std::string &error)
    {
        const std::string prefix = "binding " + std::to_string(layout_entry.binding) + ": ";

        switch (layout_entry.type)
        {
        case BindingType::UniformBuffer:
        {
            if (!resource.has_buffer)
                return fail(error, prefix + "a UniformBuffer binding requires a buffer");
            if (resource.has_view || resource.has_sampler)
                return fail(error, prefix + "a UniformBuffer binding must not carry a texture view or sampler");
            if ((resource.buffer_usage & GPUBufferUsage::UNIFORM) == 0)
                return fail(error, prefix + "the buffer does not have UNIFORM usage");
            break;
        }
        case BindingType::Texture2D:
        case BindingType::TextureCube:
        case BindingType::DepthTexture:
        {
            if (!resource.has_view)
                return fail(error, prefix + "a texture binding requires a texture view");
            if (!resource.has_sampler)
                return fail(error, prefix + "a texture binding requires a sampler (combined image sampler)");
            if (resource.has_buffer)
                return fail(error, prefix + "a texture binding must not carry a buffer");

            if (layout_entry.type == BindingType::Texture2D)
            {
                if (resource.view_dim != TextureViewDimension::Texture2D)
                    return fail(error, prefix + "a Texture2D binding requires a 2D view");
                if (resource.view_aspect != TextureAspect::Color)
                    return fail(error, prefix + "a Texture2D binding requires the Color aspect");
                if (rhi_format_is_depth(resource.view_format))
                    return fail(error, prefix + "a Texture2D binding cannot use a depth format");
            }
            else if (layout_entry.type == BindingType::TextureCube)
            {
                if (resource.view_dim != TextureViewDimension::Cube)
                    return fail(error, prefix + "a TextureCube binding requires a cube view");
                if (resource.view_aspect != TextureAspect::Color)
                    return fail(error, prefix + "a TextureCube binding requires the Color aspect");
                if (rhi_format_is_depth(resource.view_format))
                    return fail(error, prefix + "a TextureCube binding cannot use a depth format");
            }
            else
            {
                if (resource.view_dim != TextureViewDimension::Texture2D)
                    return fail(error, prefix + "a DepthTexture binding requires a 2D view");
                if (resource.view_aspect != TextureAspect::DepthOnly)
                    return fail(error, prefix + "a DepthTexture binding requires the DepthOnly aspect");
                if (!rhi_format_is_depth(resource.view_format))
                    return fail(error, prefix + "a DepthTexture binding requires a depth format");
            }

            // 比较采样器只能用于带深度分量的视图（阴影 PCF 是唯一用途）。
            if (resource.sampler_compare_enable && !rhi_format_is_depth(resource.view_format))
                return fail(error, prefix + "a comparison sampler requires a depth format");
            break;
        }
        case BindingType::Sampler:
        {
            if (!resource.has_sampler)
                return fail(error, prefix + "a Sampler binding requires a sampler");
            if (resource.has_view || resource.has_buffer)
                return fail(error, prefix + "a Sampler binding must not carry a texture view or buffer");
            break;
        }
        }
        return true;
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

    /// @brief 一个缓冲区只能有一种"主用途"（接口层的保守约束，保持描述可移植）。
    constexpr uint8_t PRIMARY_BUFFER_USAGE =
        GPUBufferUsage::VERTEX | GPUBufferUsage::INDEX | GPUBufferUsage::UNIFORM | GPUBufferUsage::STORAGE;

    // ---- SPIR-V 解析（只覆盖模块校验需要的最小集合） --------------------------
    //
    // 头部固定 5 个 word（magic / version / generator / bound / schema）；
    // 之后是指令流：每个指令的第一个 word 高 16 位是 word count，低 16 位是 opcode。
    // 这里按小端主机解释 word（项目只支持 Windows x86/x64），不做字节交换。
    constexpr uint32_t SPIRV_MAGIC_NUMBER = 0x07230203u;
    constexpr size_t SPIRV_HEADER_WORDS = 5;
    constexpr uint16_t SPIRV_OP_ENTRY_POINT = 15;
    constexpr uint32_t SPIRV_EXECUTION_MODEL_VERTEX = 0;
    constexpr uint32_t SPIRV_EXECUTION_MODEL_FRAGMENT = 4;

    uint32_t spirv_word(const std::string &source, size_t index)
    {
        uint32_t word = 0;
        // 用 memcpy 而不是 reinterpret_cast：std::string 的数据不保证 4 字节对齐。
        std::memcpy(&word, source.data() + index * sizeof(uint32_t), sizeof(word));
        return word;
    }

    /// @brief 读取 SPIR-V 字面字符串（UTF-8，按 4 字节补齐，'\0' 结尾）。
    std::string spirv_literal_string(const std::string &source, size_t first_word, size_t word_count)
    {
        std::string text;
        for (size_t i = 0; i < word_count; ++i)
        {
            const uint32_t word = spirv_word(source, first_word + i);
            for (uint32_t byte_index = 0; byte_index < sizeof(uint32_t); ++byte_index)
            {
                const char character = static_cast<char>((word >> (byte_index * 8)) & 0xFFu);
                if (character == '\0')
                    return text;
                text.push_back(character);
            }
        }
        return text;
    }

    bool spirv_execution_model_matches(ShaderType type, uint32_t execution_model)
    {
        if (type == ShaderType::VERTEX)
            return execution_model == SPIRV_EXECUTION_MODEL_VERTEX;
        return execution_model == SPIRV_EXECUTION_MODEL_FRAGMENT;
    }
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

    // 两个主用途同时出现会让后端静默选择其中一个，产生"创建成功但用法不一致"的隐患，
    // 因此统一拒绝。
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
    // 视图。
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
    // VK_LOD_CLAMP_NONE == 1000.0：Vulkan 要求 maxLod 不超过它（VUID-VkSamplerCreateInfo-maxLod-01973）。
    if (desc.lod_max_clamp > 1000.0f)
        return fail(error, "lod_max_clamp must not exceed 1000.0");
    return true;
}

bool rhi_validate_bind_group_layout_desc(const BindGroupLayoutDesc &desc, std::string &error)
{
    for (size_t i = 0; i < desc.entries.size(); ++i)
    {
        const BindGroupLayoutEntry &entry = desc.entries[i];
        const std::string prefix = "binding " + std::to_string(entry.binding) + ": ";

        if (entry.visibility == 0)
            return fail(error, prefix + "visibility must not be empty");
        if ((entry.visibility & ~SUPPORTED_SHADER_STAGES) != 0)
            return fail(error, prefix + "visibility contains unknown stage bits");

        switch (entry.type)
        {
        case BindingType::UniformBuffer:
            break;
        case BindingType::Texture2D:
        case BindingType::TextureCube:
        case BindingType::DepthTexture:
        case BindingType::Sampler:
            if (entry.has_dynamic_offset)
                return fail(error, prefix + "has_dynamic_offset is only valid for UniformBuffer");
            if (entry.min_binding_size != 0)
                return fail(error, prefix + "min_binding_size is only valid for UniformBuffer");
            break;
        default:
            return fail(error, prefix + "unknown binding type");
        }

        // 重复的 binding 会让后端的 descriptor set layout 静默合并/失败，必须提前拒绝。
        for (size_t previous = 0; previous < i; ++previous)
        {
            if (desc.entries[previous].binding == entry.binding)
                return fail(error, prefix + "the binding number is declared more than once");
        }
    }
    return true;
}

bool rhi_resolve_bind_group_desc(const RhiBindGroupLayoutInfo &layout,
                                 const std::vector<RhiBindGroupEntryInfo> &entries,
                                 std::vector<RhiResolvedBinding> &bindings, std::string &error)
{
    // 1) 每个提供的 entry 都必须声明在布局里，且 binding 不能重复。
    for (size_t i = 0; i < entries.size(); ++i)
    {
        const RhiBindGroupEntryInfo &entry = entries[i];
        const std::string prefix = "binding " + std::to_string(entry.binding) + ": ";
        if (find_layout_entry(layout, entry.binding) == nullptr)
            return fail(error, prefix + "is not declared by the bind group layout");
        for (size_t previous = 0; previous < i; ++previous)
        {
            if (entries[previous].binding == entry.binding)
                return fail(error, prefix + "is provided more than once");
        }
    }

    // 2) 布局里的每一项都必须提供资源（缺项会让 shader 读到未写入的 descriptor）。
    bindings.clear();
    bindings.reserve(layout.entries.size());
    for (const RhiBindGroupLayoutEntry &layout_entry : layout.entries)
    {
        const std::string prefix = "binding " + std::to_string(layout_entry.binding) + ": ";
        const RhiBindGroupEntryInfo *provided = nullptr;
        for (const RhiBindGroupEntryInfo &entry : entries)
        {
            if (entry.binding == layout_entry.binding)
            {
                provided = &entry;
                break;
            }
        }
        if (provided == nullptr)
            return fail(error, prefix + "is missing from the bind group");

        const RhiBindingResourceInfo &resource = provided->resource;
        if (resource.type != layout_entry.type)
            return fail(error, prefix + "the resource type does not match the layout entry");

        if (!validate_binding_resource(layout_entry, resource, error))
            return false;

        RhiResolvedBinding resolved;
        resolved.binding = layout_entry.binding;
        resolved.type = layout_entry.type;
        resolved.has_dynamic_offset = layout_entry.has_dynamic_offset;
        if (layout_entry.type == BindingType::UniformBuffer)
        {
            uint64_t resolved_size = 0;
            std::string range_error;
            if (!resolve_buffer_binding_range(resource.buffer_size, resource.offset, resource.size,
                                              resolved_size, range_error))
                return fail(error, prefix + range_error);
            if (layout_entry.min_binding_size != 0 && resolved_size < layout_entry.min_binding_size)
            {
                return fail(error, prefix + "the binding range is smaller than min_binding_size (" +
                                       std::to_string(resolved_size) + " < " +
                                       std::to_string(layout_entry.min_binding_size) + ")");
            }
            resolved.offset = resource.offset;
            resolved.size = resolved_size;
        }
        bindings.push_back(resolved);
    }
    return true;
}

bool rhi_validate_pipeline_layout_desc(const PipelineLayoutDesc &desc, std::string &error)
{
    for (size_t i = 0; i < desc.bind_group_layouts.size(); ++i)
    {
        if (!desc.bind_group_layouts[i].is_valid())
            return fail(error, "bind_group_layouts[" + std::to_string(i) + "] handle is invalid");
    }
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

    // 当前每条命令只支持一个顶点缓冲 slot（与 pass_set_vertex_buffer 的约定一致）。
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

bool rhi_validate_shader_module_desc(const ShaderModuleDesc &desc, std::string &error)
{
    if (desc.source.empty())
        return fail(error, "source must not be empty");
    if (desc.entry_point.empty())
        return fail(error, "entry_point must not be empty");

    // GLSL 文本能否编译由后端决定（Vulkan 只接受预编译的 SPIR-V）。
    if (desc.source_kind != ShaderSourceKind::SPIRV)
        return true;

    if (desc.source.size() % sizeof(uint32_t) != 0)
        return fail(error, "SPIR-V binary size must be a multiple of 4 bytes");
    if (desc.source.size() < SPIRV_HEADER_WORDS * sizeof(uint32_t))
        return fail(error, "SPIR-V binary is shorter than the 20-byte header");
    if (spirv_word(desc.source, 0) != SPIRV_MAGIC_NUMBER)
        return fail(error, "source is not a SPIR-V binary (bad magic number); set source_kind = GLSL_TEXT for "
                           "text sources or precompile it with glslc");

    const uint32_t version = spirv_word(desc.source, 1);
    const uint32_t major = (version >> 16) & 0xFFu;
    const uint32_t minor = (version >> 8) & 0xFFu;
    if (major != 1 || minor > 6)
        return fail(error, "unsupported SPIR-V version " + std::to_string(major) + "." + std::to_string(minor));

    const size_t word_count = desc.source.size() / sizeof(uint32_t);
    bool entry_point_found = false;
    bool stage_matches = false;
    for (size_t cursor = SPIRV_HEADER_WORDS; cursor < word_count;)
    {
        const uint32_t first_word = spirv_word(desc.source, cursor);
        const uint16_t instruction_words = static_cast<uint16_t>(first_word >> 16);
        const uint16_t opcode = static_cast<uint16_t>(first_word & 0xFFFFu);
        // word count 为 0 或越界都说明指令流损坏，不能继续往后跳。
        if (instruction_words == 0 || instruction_words > word_count - cursor)
            return fail(error, "SPIR-V instruction stream is truncated at word " + std::to_string(cursor));

        if (opcode == SPIRV_OP_ENTRY_POINT)
        {
            // OpEntryPoint: [opcode][ExecutionModel][EntryPoint id][name...]
            if (instruction_words < 4)
                return fail(error, "SPIR-V OpEntryPoint is too short");
            const uint32_t execution_model = spirv_word(desc.source, cursor + 1);
            const std::string name = spirv_literal_string(desc.source, cursor + 3, instruction_words - 3);
            if (name == desc.entry_point)
            {
                entry_point_found = true;
                if (spirv_execution_model_matches(desc.type, execution_model))
                    stage_matches = true;
            }
        }
        cursor += instruction_words;
    }

    if (!entry_point_found)
        return fail(error, "entry point '" + desc.entry_point + "' is not declared by OpEntryPoint");
    if (!stage_matches)
        return fail(error, "entry point '" + desc.entry_point + "' does not match the requested shader stage");
    return true;
}
