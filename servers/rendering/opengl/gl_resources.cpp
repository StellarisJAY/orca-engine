#include "gl_internal.h"
#include "gl_rendering_device_refactor.h"
#include "os/memory.h"

#include <glad/gl.h>

#include <cstdio>
#include <string>

// =============================================================================
// 资源对象（M2）：纹理 / 视图 / 采样器 / 绑定组布局 / 绑定组 / 管线布局 / 渲染管线
//
// 分工：
//   - 描述校验与映射是纯逻辑（gl_validate_* / gl_format_info），可被单元测试直接覆盖；
//   - create_* 负责 GL 对象创建（需要当前上下文），destroy_* 立即释放（GL 语义下安全）；
//   - 绑定组的"物理位置"（uniform block binding point / texture unit）由
//     pipeline layout 统一分配，管线创建时再按名字（BindGroupLayoutEntry.name）
//     解析到具体 program 的 uniform block / uniform location。
//
// 生命周期契约（与接口头文件一致）：
//   destroy_* 之后调用方句柄副本失效；bind group / pipeline 不得比它们引用的
//   layout 活得更久（WebGPU 同样要求资源依赖先于被依赖者释放）。
// =============================================================================

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

    GLenum address_mode(AddressMode mode)
    {
        switch (mode)
        {
        case AddressMode::ClampToEdge: return GL_CLAMP_TO_EDGE;
        case AddressMode::MirrorRepeat: return GL_MIRRORED_REPEAT;
        case AddressMode::Repeat:
        default: return GL_REPEAT;
        }
    }

    GLenum filter_mode(FilterMode mode)
    {
        return mode == FilterMode::Nearest ? GL_NEAREST : GL_LINEAR;
    }

    /// @brief 把 min_filter + mipmap_filter 组合成 GL 的 minification 过滤模式。
    GLenum min_filter_mode(FilterMode min_filter, FilterMode mipmap_filter)
    {
        const bool linear_min = min_filter == FilterMode::Linear;
        const bool linear_mip = mipmap_filter == FilterMode::Linear;
        if (linear_min)
            return linear_mip ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR_MIPMAP_NEAREST;
        return linear_mip ? GL_NEAREST_MIPMAP_LINEAR : GL_NEAREST_MIPMAP_NEAREST;
    }

    GLenum texture_target(TextureDimension dim)
    {
        return dim == TextureDimension::Cube ? GL_TEXTURE_CUBE_MAP : GL_TEXTURE_2D;
    }

    GLenum cube_face_target(uint32_t layer)
    {
        return static_cast<GLenum>(GL_TEXTURE_CUBE_MAP_POSITIVE_X + layer);
    }

    uint32_t vertex_format_size(VertexFormat format)
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

    /// @brief 校验一个布局项里的名字与类型约束。
    bool validate_layout_entry(const BindGroupLayoutEntry &entry, uint32_t &sampler_count, std::string &error)
    {
        if (entry.visibility == 0)
            return fail(error, "binding " + std::to_string(entry.binding) + ": visibility is empty");
        if ((entry.visibility & ~(ShaderStage::VERTEX | ShaderStage::FRAGMENT)) != 0)
            return fail(error, "binding " + std::to_string(entry.binding) + ": unknown visibility bits");

        switch (entry.type)
        {
        case BindingType::UniformBuffer:
            // GLSL 3.30 不支持 layout(binding = N)，必须靠 uniform block 名定位
            if (entry.name.empty())
                return fail(error, "binding " + std::to_string(entry.binding) +
                                       ": uniform buffer entry requires the GLSL block name");
            break;
        case BindingType::Texture2D:
        case BindingType::TextureCube:
        case BindingType::DepthTexture:
            if (entry.name.empty())
                return fail(error, "binding " + std::to_string(entry.binding) +
                                       ": texture entry requires the GLSL sampler uniform name");
            break;
        case BindingType::Sampler:
            // 采样器对象直接绑到纹理单元，不需要 uniform 名字。
            // 约定：一个绑定组最多一个 Sampler 项，作用于该组所有纹理单元。
            ++sampler_count;
            if (sampler_count > 1)
                return fail(error, "a bind group supports at most one sampler entry");
            break;
        default:
            return fail(error, "binding " + std::to_string(entry.binding) + ": unknown binding type");
        }
        return true;
    }

    /// @brief 把绑定组资源条目快照成 GLBindGroupBinding 并做资源级校验。
    bool collect_binding(const BindGroupLayoutEntry &entry, const BindGroupEntry &match, GLBindGroupBinding &out,
                         std::string &error)
    {
        const std::string binding_name = "binding " + std::to_string(entry.binding);

        if (match.resource.type != entry.type)
            return fail(error, binding_name + ": resource type does not match the layout entry type");

        out.binding = entry.binding;
        out.type = entry.type;

        switch (entry.type)
        {
        case BindingType::UniformBuffer:
        {
            const GPUBufferHandle &buffer = match.resource.buffer;
            if (!buffer.is_valid() || buffer.data == nullptr)
                return fail(error, binding_name + ": uniform buffer handle is invalid");

            const GLBuffer *object = static_cast<const GLBuffer *>(buffer.data);
            const uint64_t offset = match.resource.offset;
            const uint64_t size = match.resource.size;

            if (offset > object->size)
                return fail(error, binding_name + ": offset is beyond the buffer size");
            if (size != 0 && offset + size > object->size)
                return fail(error, binding_name + ": offset + size exceeds the buffer size");

            const uint64_t available = size != 0 ? size : object->size - offset;
            if (entry.min_binding_size > 0 && available < entry.min_binding_size)
                return fail(error, binding_name + ": bound range is smaller than min_binding_size");

            out.buffer = buffer;
            out.offset = offset;
            out.size = size;
            break;
        }
        case BindingType::Texture2D:
        case BindingType::TextureCube:
        case BindingType::DepthTexture:
        {
            const TextureViewHandle &view = match.resource.texture_view;
            if (!view.is_valid() || view.data == nullptr)
                return fail(error, binding_name + ": texture view handle is invalid");

            const GLTextureView *object = static_cast<const GLTextureView *>(view.data);
            if (entry.type == BindingType::DepthTexture)
            {
                if (object->aspect != TextureAspect::DepthOnly)
                    return fail(error, binding_name + ": depth texture entry requires a DepthOnly view");
            }
            else
            {
                if (object->aspect != TextureAspect::Color)
                    return fail(error, binding_name + ": color texture entry requires a Color view");
                if (entry.type == BindingType::TextureCube && object->dim != TextureViewDimension::Cube)
                    return fail(error, binding_name + ": TextureCube entry requires a Cube view");
                if (entry.type == BindingType::Texture2D && object->dim != TextureViewDimension::Texture2D)
                    return fail(error, binding_name + ": Texture2D entry requires a Texture2D view");
            }

            out.view = view;
            break;
        }
        case BindingType::Sampler:
        {
            const SamplerHandle &sampler = match.resource.sampler;
            if (!sampler.is_valid() || sampler.data == nullptr)
                return fail(error, binding_name + ": sampler handle is invalid");
            out.sampler = sampler;
            break;
        }
        default:
            return fail(error, binding_name + ": unknown binding type");
        }
        return true;
    }
} // namespace

// ---------------------------------------------------------------------------
// 格式映射
// ---------------------------------------------------------------------------

bool gl_format_is_depth(Format format)
{
    return format == Format::Depth32Float;
}

bool gl_format_info(Format format, GLFormatInfo &out)
{
    GLFormatInfo info;
    switch (format)
    {
    case Format::R8Unorm:
        info = GLFormatInfo{GL_R8, GL_RED, GL_UNSIGNED_BYTE, 1, false};
        break;
    case Format::RG8Unorm:
        info = GLFormatInfo{GL_RG8, GL_RG, GL_UNSIGNED_BYTE, 2, false};
        break;
    case Format::RGBA8Unorm:
        info = GLFormatInfo{GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, 4, false};
        break;
    case Format::RGBA8UnormSrgb:
        info = GLFormatInfo{GL_SRGB8_ALPHA8, GL_RGBA, GL_UNSIGNED_BYTE, 4, false};
        break;
    case Format::R16Float:
        info = GLFormatInfo{GL_R16F, GL_RED, GL_FLOAT, 2, false};
        break;
    case Format::RGBA16Float:
        info = GLFormatInfo{GL_RGBA16F, GL_RGBA, GL_FLOAT, 8, false};
        break;
    case Format::Depth32Float:
        info = GLFormatInfo{GL_DEPTH_COMPONENT32F, GL_DEPTH_COMPONENT, GL_FLOAT, 4, true};
        break;
    case Format::Undefined:
    default:
        return false;
    }
    out = info;
    return true;
}

// ---------------------------------------------------------------------------
// 描述校验
// ---------------------------------------------------------------------------

bool gl_validate_texture_desc(const TextureDescRefactor &desc, std::string &error)
{
    GLFormatInfo info;
    if (!gl_format_info(desc.format, info))
        return fail(error, "format is Undefined or unsupported by the OpenGL backend");
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
    if ((desc.usage & ~(TextureUsage::TEXTURE_BINDING | TextureUsage::RENDER_ATTACHMENT | TextureUsage::COPY_DST |
                        TextureUsage::COPY_SRC)) != 0)
        return fail(error, "usage contains unknown bits");
    return true;
}

bool gl_validate_texture_view_desc(const GLTexture &texture, const TextureViewDesc &desc, std::string &error)
{
    if (texture.texture == 0)
        return fail(error, "the parent texture object is not a valid GL texture");
    if (texture.mip_levels == 0)
        return fail(error, "the parent texture has no mip level");

    const bool texture_is_depth = gl_format_is_depth(texture.format);
    if (texture_is_depth && desc.aspect != TextureAspect::DepthOnly)
        return fail(error, "a depth texture can only be viewed with the DepthOnly aspect");
    if (!texture_is_depth && desc.aspect != TextureAspect::Color)
        return fail(error, "a color texture can only be viewed with the Color aspect");

    if (texture.dim == TextureDimension::Cube)
    {
        // GL 3.3 没有 glTextureView：cubemap 无法再被视为 2D 纹理
        if (desc.dim != TextureViewDimension::Cube)
            return fail(error, "a cubemap texture can only be viewed as Cube (GL 3.3 has no texture views)");

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

bool gl_validate_sampler_desc(const SamplerDesc &desc, std::string &error)
{
    if (desc.lod_min_clamp < 0.0f)
        return fail(error, "lod_min_clamp must not be negative");
    if (desc.lod_max_clamp < desc.lod_min_clamp)
        return fail(error, "lod_max_clamp must be greater than or equal to lod_min_clamp");
    return true;
}

bool gl_validate_render_pipeline_desc(const RenderPipelineDesc &desc, std::string &error)
{
    if (!desc.pipeline_layout.is_valid())
        return fail(error, "pipeline_layout handle is invalid");
    if (!desc.vertex.module.is_valid())
        return fail(error, "vertex shader module is missing");
    if (!desc.fragment.targets.empty() && !desc.fragment.module.is_valid())
        return fail(error, "fragment shader module is missing while color targets are declared");
    // GL 后端把顶点 + 片段源码链接成一个 program，所以两个阶段必须引用同一个模块
    if (desc.fragment.module.is_valid() && desc.fragment.module.handle != desc.vertex.module.handle)
        return fail(error, "vertex and fragment stages must reference the same shader module");
    if (desc.samples_count != 1)
        return fail(error, "only samples_count == 1 is supported (no MSAA yet)");

    if (desc.fragment.targets.empty() && desc.depth_stencil.format == Format::Undefined)
        return fail(error, "the pipeline declares neither a color target nor a depth format");

    for (size_t i = 0; i < desc.fragment.targets.size(); ++i)
    {
        GLFormatInfo info;
        if (!gl_format_info(desc.fragment.targets[i].format, info))
            return fail(error, "color target " + std::to_string(i) + ": format is Undefined or unsupported");
        if (info.is_depth)
            return fail(error, "color target " + std::to_string(i) + ": format must not be a depth format");
    }

    if (desc.depth_stencil.format != Format::Undefined)
    {
        GLFormatInfo info;
        if (!gl_format_info(desc.depth_stencil.format, info))
            return fail(error, "depth_stencil: format is unsupported");
        if (!info.is_depth)
            return fail(error, "depth_stencil: format must be a depth format");
    }

    // 本期只支持 slot 0 / offset 0 的顶点缓冲（见 pass_set_vertex_buffer）
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
            const uint32_t size = vertex_format_size(attribute.format);
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

// ---------------------------------------------------------------------------
// 纹理 / 视图 / 采样器
// ---------------------------------------------------------------------------

TextureHandle GLRenderingDeviceRefactor::create_texture(const TextureDescRefactor &desc)
{
    TextureHandle handle;

    std::string error;
    if (!gl_validate_texture_desc(desc, error))
    {
        std::printf("[ERROR] create_texture(%s): %s\n", desc.label.c_str(), error.c_str());
        return handle;
    }

    GLFormatInfo info;
    gl_format_info(desc.format, info);

    GLuint texture = 0;
    glGenTextures(1, &texture);
    if (texture == 0)
    {
        std::printf("[ERROR] create_texture(%s): glGenTextures failed\n", desc.label.c_str());
        return handle;
    }

    const GLenum target = texture_target(desc.dim);
    glBindTexture(target, texture);

    // GL 3.3 没有 glTexStorage2D：逐 mip 层分配不可变存储（data = nullptr）。
    // 这样 render target 与后续 glTexSubImage2D 上传都有完整存储。
    for (uint32_t level = 0; level < desc.mip_levels; ++level)
    {
        const uint32_t width = desc.width >> level > 0 ? desc.width >> level : 1;
        const uint32_t height = desc.height >> level > 0 ? desc.height >> level : 1;
        if (desc.dim == TextureDimension::Cube)
        {
            for (uint32_t layer = 0; layer < 6; ++layer)
            {
                glTexImage2D(cube_face_target(layer), static_cast<GLint>(level),
                             static_cast<GLint>(info.internal_format), static_cast<GLsizei>(width),
                             static_cast<GLsizei>(height), 0, static_cast<GLenum>(info.format),
                             static_cast<GLenum>(info.type), nullptr);
            }
        }
        else
        {
            glTexImage2D(GL_TEXTURE_2D, static_cast<GLint>(level), static_cast<GLint>(info.internal_format),
                         static_cast<GLsizei>(width), static_cast<GLsizei>(height), 0,
                         static_cast<GLenum>(info.format), static_cast<GLenum>(info.type), nullptr);
        }
    }

    // 未绑定 sampler 对象时生效的默认采样状态。绑定 sampler 后这些会被覆盖。
    glTexParameteri(target, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri(target, GL_TEXTURE_MAX_LEVEL, static_cast<GLint>(desc.mip_levels - 1));
    glTexParameteri(target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    if (desc.dim == TextureDimension::Cube)
        glTexParameteri(target, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glTexParameteri(target, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(target, GL_TEXTURE_MIN_FILTER,
                    desc.mip_levels > 1 ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
    if (info.is_depth)
        glTexParameteri(target, GL_TEXTURE_COMPARE_MODE, GL_NONE);

    glBindTexture(target, 0);

    GLTexture *object = memnew(GLTexture);
    object->texture = texture;
    object->dim = desc.dim;
    object->format = desc.format;
    object->width = desc.width;
    object->height = desc.height;
    object->mip_levels = desc.mip_levels;
    object->layers = desc.dim == TextureDimension::Cube ? 6 : 1;
    object->usage = desc.usage;

    handle.handle = texture;
    handle.data = object;
    return handle;
}

TextureViewHandle GLRenderingDeviceRefactor::create_texture_view(TextureHandle texture, const TextureViewDesc &desc)
{
    TextureViewHandle handle;

    if (!texture.is_valid() || texture.data == nullptr)
    {
        std::printf("[ERROR] create_texture_view: invalid texture handle\n");
        return handle;
    }

    GLTexture *object = static_cast<GLTexture *>(texture.data);
    std::string error;
    if (!gl_validate_texture_view_desc(*object, desc, error))
    {
        std::printf("[ERROR] create_texture_view: %s\n", error.c_str());
        return handle;
    }

    GLTextureView *view = memnew(GLTextureView);
    view->texture_handle = texture.handle;
    view->texture = object;
    view->dim = desc.dim;
    view->base_mip = desc.base_mip;
    // 把 "0 = 到最后一层" 在这里一次性解析，回放阶段不再做区间推算
    view->mip_count = desc.mip_count == 0 ? object->mip_levels - desc.base_mip : desc.mip_count;
    view->base_layer = desc.base_layer;
    view->layer_count = desc.layer_count == 0 ? object->layers - desc.base_layer : desc.layer_count;
    view->aspect = desc.aspect;

    handle.handle = alloc_handle_id();
    handle.data = view;
    return handle;
}

SamplerHandle GLRenderingDeviceRefactor::create_sampler(const SamplerDesc &desc)
{
    SamplerHandle handle;

    std::string error;
    if (!gl_validate_sampler_desc(desc, error))
    {
        std::printf("[ERROR] create_sampler: %s\n", error.c_str());
        return handle;
    }

    GLuint sampler = 0;
    glGenSamplers(1, &sampler);
    if (sampler == 0)
    {
        std::printf("[ERROR] create_sampler: glGenSamplers failed\n");
        return handle;
    }

    glSamplerParameteri(sampler, GL_TEXTURE_WRAP_S, static_cast<GLint>(address_mode(desc.address_u)));
    glSamplerParameteri(sampler, GL_TEXTURE_WRAP_T, static_cast<GLint>(address_mode(desc.address_v)));
    glSamplerParameteri(sampler, GL_TEXTURE_WRAP_R, static_cast<GLint>(address_mode(desc.address_w)));
    glSamplerParameteri(sampler, GL_TEXTURE_MAG_FILTER, static_cast<GLint>(filter_mode(desc.mag_filter)));
    glSamplerParameteri(sampler, GL_TEXTURE_MIN_FILTER,
                        static_cast<GLint>(min_filter_mode(desc.min_filter, desc.mipmap_filter)));
    glSamplerParameterf(sampler, GL_TEXTURE_MIN_LOD, desc.lod_min_clamp);
    glSamplerParameterf(sampler, GL_TEXTURE_MAX_LOD, desc.lod_max_clamp);
    if (desc.compare_enable)
    {
        glSamplerParameteri(sampler, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
        glSamplerParameteri(sampler, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
    }
    else
    {
        glSamplerParameteri(sampler, GL_TEXTURE_COMPARE_MODE, GL_NONE);
    }

    GLSampler *object = memnew(GLSampler);
    object->sampler = sampler;
    object->desc = desc;

    handle.handle = sampler;
    handle.data = object;
    return handle;
}

void GLRenderingDeviceRefactor::queue_write_texture(TextureHandle texture, const TextureWriteDesc &desc)
{
    if (!texture.is_valid() || texture.data == nullptr)
    {
        std::printf("[ERROR] queue_write_texture: invalid texture handle\n");
        return;
    }

    GLTexture *object = static_cast<GLTexture *>(texture.data);

    GLFormatInfo info;
    if (!gl_format_info(object->format, info))
    {
        std::printf("[ERROR] queue_write_texture: texture format has no GL mapping\n");
        return;
    }

    if (desc.data == nullptr)
    {
        std::printf("[ERROR] queue_write_texture: data pointer is null\n");
        return;
    }
    if (desc.mip_level >= object->mip_levels)
    {
        std::printf("[ERROR] queue_write_texture: mip_level %u is out of range (mip_levels = %u)\n",
                    desc.mip_level, object->mip_levels);
        return;
    }

    const uint32_t mip_width = object->width >> desc.mip_level > 0 ? object->width >> desc.mip_level : 1;
    const uint32_t mip_height = object->height >> desc.mip_level > 0 ? object->height >> desc.mip_level : 1;
    const uint32_t width = desc.width == 0 ? mip_width : desc.width;
    const uint32_t height = desc.height == 0 ? mip_height : desc.height;

    if (width == 0 || height == 0)
    {
        std::printf("[ERROR] queue_write_texture: upload region is empty\n");
        return;
    }
    if (desc.origin_x + width > mip_width || desc.origin_y + height > mip_height)
    {
        std::printf("[ERROR] queue_write_texture: upload region [%u, %u, %u, %u] exceeds mip %u size %ux%u\n",
                    desc.origin_x, desc.origin_y, width, height, desc.mip_level, mip_width, mip_height);
        return;
    }
    if (object->dim == TextureDimension::Cube && desc.array_layer >= 6)
    {
        std::printf("[ERROR] queue_write_texture: cubemap face %u is out of range\n", desc.array_layer);
        return;
    }

    uint32_t row_bytes = width * info.bytes_per_pixel;
    if (desc.bytes_per_row != 0)
    {
        if (desc.bytes_per_row % info.bytes_per_pixel != 0)
        {
            std::printf("[ERROR] queue_write_texture: bytes_per_row must be a multiple of %u\n",
                        info.bytes_per_pixel);
            return;
        }
        if (desc.bytes_per_row < row_bytes)
        {
            std::printf("[ERROR] queue_write_texture: bytes_per_row %u is smaller than %u required bytes\n",
                        desc.bytes_per_row, row_bytes);
            return;
        }
        row_bytes = desc.bytes_per_row;
    }

    const uint64_t required = static_cast<uint64_t>(height - 1) * row_bytes +
                              static_cast<uint64_t>(width) * info.bytes_per_pixel;
    if (desc.data_size < required)
    {
        std::printf("[ERROR] queue_write_texture: data_size %llu is smaller than the required %llu bytes\n",
                    static_cast<unsigned long long>(desc.data_size), static_cast<unsigned long long>(required));
        return;
    }

    const GLenum target = object->dim == TextureDimension::Cube
                              ? cube_face_target(desc.array_layer)
                              : static_cast<GLenum>(GL_TEXTURE_2D);
    const GLenum bind_target = texture_target(object->dim);

    // TODO(command): M5 把上传改成队列命令，保证与 queue_submit 的先后顺序；
    // 目前与 queue_write_buffer 一样是立即执行。
    glBindTexture(bind_target, object->texture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    if (desc.bytes_per_row != 0)
        glPixelStorei(GL_UNPACK_ROW_LENGTH, static_cast<GLint>(desc.bytes_per_row / info.bytes_per_pixel));
    glTexSubImage2D(target, static_cast<GLint>(desc.mip_level), static_cast<GLint>(desc.origin_x),
                    static_cast<GLint>(desc.origin_y), static_cast<GLsizei>(width), static_cast<GLsizei>(height),
                    static_cast<GLenum>(info.format), static_cast<GLenum>(info.type), desc.data);
    if (desc.bytes_per_row != 0)
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glBindTexture(bind_target, 0);
}

void GLRenderingDeviceRefactor::generate_mip_map(TextureHandle texture)
{
    if (!texture.is_valid() || texture.data == nullptr)
    {
        std::printf("[ERROR] generate_mip_map: invalid texture handle\n");
        return;
    }

    GLTexture *object = static_cast<GLTexture *>(texture.data);
    if (object->mip_levels <= 1)
        return; // 单层纹理没有 mip 可生成

    const GLenum target = texture_target(object->dim);
    glBindTexture(target, object->texture);
    glGenerateMipmap(target);
    glBindTexture(target, 0);
}

void GLRenderingDeviceRefactor::destroy_texture(TextureHandle texture)
{
    if (!texture.is_valid() || texture.data == nullptr)
        return;

    GLTexture *object = static_cast<GLTexture *>(texture.data);
    if (object->texture != 0)
    {
        const GLuint gl_texture = object->texture;
        glDeleteTextures(1, &gl_texture);
    }
    // 注意：引用该纹理的 TextureView 会变成悬空视图；调用方必须先销毁 view。
    memdelete(object);
}

void GLRenderingDeviceRefactor::destroy_texture_view(TextureViewHandle view)
{
    if (!view.is_valid() || view.data == nullptr)
        return;
    memdelete(static_cast<GLTextureView *>(view.data));
}

void GLRenderingDeviceRefactor::destroy_sampler(SamplerHandle sampler)
{
    if (!sampler.is_valid() || sampler.data == nullptr)
        return;

    GLSampler *object = static_cast<GLSampler *>(sampler.data);
    if (object->sampler != 0)
    {
        const GLuint gl_sampler = object->sampler;
        glDeleteSamplers(1, &gl_sampler);
    }
    memdelete(object);
}

// ---------------------------------------------------------------------------
// 绑定组 / 管线布局
// ---------------------------------------------------------------------------

BindGroupLayoutHandle GLRenderingDeviceRefactor::create_bind_group_layout(const BindGroupLayoutDesc &desc)
{
    BindGroupLayoutHandle handle;

    if (desc.entries.empty())
    {
        std::printf("[ERROR] create_bind_group_layout(%s): layout has no entry\n", desc.label.c_str());
        return handle;
    }

    uint32_t sampler_count = 0;
    for (size_t i = 0; i < desc.entries.size(); ++i)
    {
        const BindGroupLayoutEntry &entry = desc.entries[i];
        std::string error;
        if (!validate_layout_entry(entry, sampler_count, error))
        {
            std::printf("[ERROR] create_bind_group_layout(%s): %s\n", desc.label.c_str(), error.c_str());
            return handle;
        }
        for (size_t j = 0; j < i; ++j)
        {
            if (desc.entries[j].binding == entry.binding)
            {
                std::printf("[ERROR] create_bind_group_layout(%s): binding %u is declared more than once\n",
                            desc.label.c_str(), entry.binding);
                return handle;
            }
        }
    }

    // 布局创建不产生 GL 调用：物理位置由 pipeline layout 分配，
    // 名字到 program 的解析发生在 create_render_pipeline。
    GLBindGroupLayout *layout = memnew(GLBindGroupLayout);
    layout->entries = desc.entries;

    handle.handle = alloc_handle_id();
    handle.data = layout;
    return handle;
}

BindGroupHandle GLRenderingDeviceRefactor::create_bind_group(const BindGroupDesc &desc)
{
    BindGroupHandle handle;

    if (!desc.layout.is_valid() || desc.layout.data == nullptr)
    {
        std::printf("[ERROR] create_bind_group(%s): invalid bind group layout handle\n", desc.label.c_str());
        return handle;
    }

    GLBindGroupLayout *layout = static_cast<GLBindGroupLayout *>(desc.layout.data);

    GLBindGroup *group = memnew(GLBindGroup);
    group->layout = layout;

    // 1) 布局里的每一项都必须有对应资源（WebGPU 的 bind group 是完整的）
    for (const BindGroupLayoutEntry &entry : layout->entries)
    {
        const BindGroupEntry *match = nullptr;
        for (const BindGroupEntry &candidate : desc.entries)
        {
            if (candidate.binding == entry.binding)
            {
                match = &candidate;
                break;
            }
        }
        if (match == nullptr)
        {
            std::printf("[ERROR] create_bind_group(%s): binding %u declared by the layout has no resource\n",
                        desc.label.c_str(), entry.binding);
            memdelete(group);
            return handle;
        }

        GLBindGroupBinding binding;
        std::string error;
        if (!collect_binding(entry, *match, binding, error))
        {
            std::printf("[ERROR] create_bind_group(%s): %s\n", desc.label.c_str(), error.c_str());
            memdelete(group);
            return handle;
        }
        group->bindings.push_back(binding);
    }

    // 2) 资源里多出来的 binding 视为错误（布局没声明它就无从绑定）
    for (const BindGroupEntry &candidate : desc.entries)
    {
        bool declared = false;
        for (const BindGroupLayoutEntry &entry : layout->entries)
        {
            if (entry.binding == candidate.binding)
            {
                declared = true;
                break;
            }
        }
        if (!declared)
        {
            std::printf("[ERROR] create_bind_group(%s): binding %u is not declared by the layout\n",
                        desc.label.c_str(), candidate.binding);
            memdelete(group);
            return handle;
        }
    }

    // 3) 采样器配对规则：一个组的 Sampler 作用于本组所有纹理单元，没有纹理就没意义
    bool has_texture = false;
    bool has_sampler = false;
    for (const GLBindGroupBinding &binding : group->bindings)
    {
        if (binding.type == BindingType::Sampler)
            has_sampler = true;
        else if (binding.type != BindingType::UniformBuffer)
            has_texture = true;
    }
    if (has_sampler && !has_texture)
    {
        std::printf("[ERROR] create_bind_group(%s): sampler entry requires at least one texture entry in the group\n",
                    desc.label.c_str());
        memdelete(group);
        return handle;
    }

    handle.handle = alloc_handle_id();
    handle.data = group;
    return handle;
}

PipelineLayoutHandle GLRenderingDeviceRefactor::create_pipeline_layout(const PipelineLayoutDesc &desc)
{
    PipelineLayoutHandle handle;

    GLPipelineLayout *layout = memnew(GLPipelineLayout);

    uint32_t uniform_buffer_index = 0;
    uint32_t texture_unit_index = 0;

    for (size_t group_index = 0; group_index < desc.bind_group_layouts.size(); ++group_index)
    {
        const BindGroupLayoutHandle &group_handle = desc.bind_group_layouts[group_index];
        if (!group_handle.is_valid() || group_handle.data == nullptr)
        {
            std::printf("[ERROR] create_pipeline_layout(%s): bind group layout %u is invalid\n",
                        desc.label.c_str(), static_cast<unsigned>(group_index));
            memdelete(layout);
            return handle;
        }

        GLBindGroupLayout *group = static_cast<GLBindGroupLayout *>(group_handle.data);
        layout->groups.push_back(group);

        const uint32_t unit_begin = texture_unit_index;
        for (const BindGroupLayoutEntry &entry : group->entries)
        {
            GLPipelineResource resource;
            resource.group = static_cast<uint32_t>(group_index);
            resource.binding = entry.binding;
            resource.type = entry.type;
            resource.name = entry.name;

            if (entry.type == BindingType::UniformBuffer)
                resource.index = uniform_buffer_index++;
            else if (entry.type == BindingType::Sampler)
                resource.index = 0; // 采样器不占独立位置，作用于本组的纹理单元
            else
                resource.index = texture_unit_index++;

            layout->resources.push_back(resource);
        }

        layout->group_unit_begin.push_back(unit_begin);
        layout->group_unit_count.push_back(texture_unit_index - unit_begin);
    }

    layout->uniform_buffer_count = uniform_buffer_index;
    layout->texture_unit_count = texture_unit_index;

    handle.handle = alloc_handle_id();
    handle.data = layout;
    return handle;
}

void GLRenderingDeviceRefactor::destroy_bind_group(BindGroupHandle bind_group)
{
    if (!bind_group.is_valid() || bind_group.data == nullptr)
        return;
    memdelete(static_cast<GLBindGroup *>(bind_group.data));
}

void GLRenderingDeviceRefactor::destroy_bind_group_layout(BindGroupLayoutHandle layout)
{
    if (!layout.is_valid() || layout.data == nullptr)
        return;
    // 约束：引用该布局的 bind group / pipeline layout 必须已经销毁。
    memdelete(static_cast<GLBindGroupLayout *>(layout.data));
}

void GLRenderingDeviceRefactor::destroy_pipeline_layout(PipelineLayoutHandle layout)
{
    if (!layout.is_valid() || layout.data == nullptr)
        return;
    memdelete(static_cast<GLPipelineLayout *>(layout.data));
}

// ---------------------------------------------------------------------------
// 渲染管线
// ---------------------------------------------------------------------------

RenderPipelineHandle GLRenderingDeviceRefactor::create_render_pipeline(const RenderPipelineDesc &desc)
{
    RenderPipelineHandle handle;

    std::string error;
    if (!gl_validate_render_pipeline_desc(desc, error))
    {
        std::printf("[ERROR] create_render_pipeline(%s): %s\n", desc.label.c_str(), error.c_str());
        return handle;
    }

    GLShaderModule *vertex_module = static_cast<GLShaderModule *>(desc.vertex.module.data);
    if (vertex_module->program == 0)
    {
        std::printf("[ERROR] create_render_pipeline(%s): shader module has no linked program\n",
                    desc.label.c_str());
        return handle;
    }

    GLPipelineLayout *pipeline_layout = static_cast<GLPipelineLayout *>(desc.pipeline_layout.data);

    GLint max_texture_units = 0;
    glGetIntegerv(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS, &max_texture_units);
    if (max_texture_units > 0 &&
        pipeline_layout->texture_unit_count > static_cast<uint32_t>(max_texture_units))
    {
        std::printf("[ERROR] create_render_pipeline(%s): %u texture units requested but only %d available\n",
                    desc.label.c_str(), pipeline_layout->texture_unit_count, static_cast<int>(max_texture_units));
        return handle;
    }

    // 名字 → program 物理位置的解析（GLSL 3.30 没有 layout(binding = N)）
    GLint previous_program = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &previous_program);
    glUseProgram(vertex_module->program);

    GLRenderPipeline *pipeline = memnew(GLRenderPipeline);
    pipeline->resolved.resize(pipeline_layout->resources.size());

    for (size_t i = 0; i < pipeline_layout->resources.size(); ++i)
    {
        const GLPipelineResource &resource = pipeline_layout->resources[i];

        if (resource.type == BindingType::UniformBuffer)
        {
            const GLuint block = glGetUniformBlockIndex(vertex_module->program, resource.name.c_str());
            if (block == GL_INVALID_INDEX)
            {
                std::printf("[ERROR] create_render_pipeline(%s): uniform block '%s' (group %u binding %u) "
                            "is not used by the shader\n",
                            desc.label.c_str(), resource.name.c_str(), resource.group, resource.binding);
                glUseProgram(previous_program);
                memdelete(pipeline);
                return handle;
            }
            glUniformBlockBinding(vertex_module->program, block, resource.index);
            pipeline->resolved[i].uniform_block = block;
        }
        else if (resource.type != BindingType::Sampler)
        {
            // 纹理项决定纹理单元：glBindTexture 用的单元号在这里固定下来
            const GLint location = glGetUniformLocation(vertex_module->program, resource.name.c_str());
            if (location < 0)
            {
                // 可能是 shader 真的没用到（被优化掉），也可能是名字写错；
                // 前者合法，所以只警告不失败。
                std::printf("[WARN] create_render_pipeline(%s): sampler uniform '%s' (group %u binding %u) "
                            "not found in the program\n",
                            desc.label.c_str(), resource.name.c_str(), resource.group, resource.binding);
            }
            else
            {
                glUniform1i(location, static_cast<GLint>(resource.index));
            }
            pipeline->resolved[i].uniform_location = location;
        }
    }

    glUseProgram(previous_program);

    pipeline->program = vertex_module->program;
    pipeline->vertex_module = desc.vertex.module;
    pipeline->fragment_module = desc.fragment.module;
    pipeline->pipeline_layout_handle = desc.pipeline_layout;
    pipeline->pipeline_layout = pipeline_layout;
    pipeline->primitive = desc.primitive;
    pipeline->depth_stencil = desc.depth_stencil;
    pipeline->color_targets = desc.fragment.targets;
    pipeline->vertex_buffers = desc.vertex.buffers;
    pipeline->samples_count = desc.samples_count;

    handle.handle = alloc_handle_id();
    handle.data = pipeline;
    return handle;
}

void GLRenderingDeviceRefactor::destroy_render_pipeline(RenderPipelineHandle pipeline)
{
    if (!pipeline.is_valid() || pipeline.data == nullptr)
        return;
    // program 的所有权属于 ShaderModule（由 destroy_shader_module 删除），管线不重复删除。
    memdelete(static_cast<GLRenderPipeline *>(pipeline.data));
}
