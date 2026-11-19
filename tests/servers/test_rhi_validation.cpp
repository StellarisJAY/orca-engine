#include "doctest/doctest.h"
#include "rhi_validation.h"

#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace
{
    TextureDescRefactor make_valid_texture()
    {
        TextureDescRefactor desc;
        desc.format = Format::RGBA8Unorm;
        desc.width = 64;
        desc.height = 64;
        desc.mip_levels = 1;
        desc.samples_count = 1;
        desc.usage = TextureUsage::TEXTURE_BINDING;
        return desc;
    }

    TextureViewDesc make_valid_view()
    {
        TextureViewDesc desc;
        desc.dim = TextureViewDimension::Texture2D;
        desc.aspect = TextureAspect::Color;
        return desc;
    }

    RenderPipelineDesc make_valid_pipeline()
    {
        RenderPipelineDesc desc;
        desc.pipeline_layout.handle = 1;
        desc.vertex.module.handle = 2;
        desc.fragment.module.handle = 2;

        ColorTargetState target;
        target.format = Format::RGBA8Unorm;
        desc.fragment.targets.push_back(target);
        return desc;
    }

    GPUBufferDesc make_valid_buffer()
    {
        GPUBufferDesc desc;
        desc.size = 1024;
        desc.usage = GPUBufferUsage::VERTEX | GPUBufferUsage::COPY_DST;
        return desc;
    }
} // namespace

TEST_CASE("RHI validation: format predicates")
{
    CHECK(rhi_format_is_depth(Format::Depth32Float));
    CHECK_FALSE(rhi_format_is_depth(Format::RGBA8Unorm));
    CHECK_FALSE(rhi_format_is_depth(Format::RGBA8UnormSrgb));

    CHECK(rhi_format_is_srgb(Format::RGBA8UnormSrgb));
    CHECK(rhi_format_is_srgb(Format::BGRA8UnormSrgb));
    CHECK_FALSE(rhi_format_is_srgb(Format::RGBA8Unorm));
    CHECK_FALSE(rhi_format_is_srgb(Format::BGRA8Unorm));

    CHECK(rhi_vertex_format_size(VertexFormat::Float32) == 4);
    CHECK(rhi_vertex_format_size(VertexFormat::Float32x2) == 8);
    CHECK(rhi_vertex_format_size(VertexFormat::Float32x3) == 12);
    CHECK(rhi_vertex_format_size(VertexFormat::Float32x4) == 16);
    CHECK(rhi_vertex_format_size(VertexFormat::Uint32) == 4);
    CHECK(rhi_vertex_format_size(VertexFormat::Uint8x4Normalized) == 4);

    // 纹理 texel 字节数：上传时计算紧密打包行宽（bytes_per_row == 0 的默认值）
    CHECK(rhi_format_texel_size(Format::R8Unorm) == 1);
    CHECK(rhi_format_texel_size(Format::RG8Unorm) == 2);
    CHECK(rhi_format_texel_size(Format::R16Float) == 2);
    CHECK(rhi_format_texel_size(Format::RGBA8Unorm) == 4);
    CHECK(rhi_format_texel_size(Format::RGBA8UnormSrgb) == 4);
    CHECK(rhi_format_texel_size(Format::BGRA8Unorm) == 4);
    CHECK(rhi_format_texel_size(Format::BGRA8UnormSrgb) == 4);
    CHECK(rhi_format_texel_size(Format::Depth32Float) == 4);
    CHECK(rhi_format_texel_size(Format::RGBA16Float) == 8);
    CHECK(rhi_format_texel_size(Format::Undefined) == 0);
}

TEST_CASE("RHI validation: buffer desc")
{
    std::string error;

    GPUBufferDesc desc = make_valid_buffer();
    CHECK(rhi_validate_buffer_desc(desc, error));

    // 顶点 / 索引 / uniform 各自只带一个主用途也合法；COPY_DST 是写入的前提。
    desc = make_valid_buffer();
    desc.usage = GPUBufferUsage::INDEX | GPUBufferUsage::COPY_DST;
    CHECK(rhi_validate_buffer_desc(desc, error));

    desc = make_valid_buffer();
    desc.usage = GPUBufferUsage::UNIFORM;
    CHECK(rhi_validate_buffer_desc(desc, error));

    desc = make_valid_buffer();
    desc.size = 0;
    CHECK_FALSE(rhi_validate_buffer_desc(desc, error));
    CHECK(error.find("size") != std::string::npos);

    desc = make_valid_buffer();
    desc.usage = 0;
    CHECK_FALSE(rhi_validate_buffer_desc(desc, error));
    CHECK(error.find("usage") != std::string::npos);

    // STORAGE 与纹理的 STORAGE 一样是预留能力，本期必须被拒绝。
    desc = make_valid_buffer();
    desc.usage = GPUBufferUsage::STORAGE;
    CHECK_FALSE(rhi_validate_buffer_desc(desc, error));

    // 未定义的用途位不能被静默忽略。
    desc = make_valid_buffer();
    desc.usage = static_cast<uint8_t>(GPUBufferUsage::VERTEX | 0x40);
    CHECK_FALSE(rhi_validate_buffer_desc(desc, error));

    // 一个缓冲对象只能有一个主用途：两个主用途同时出现必须被拒绝。
    desc = make_valid_buffer();
    desc.usage = GPUBufferUsage::VERTEX | GPUBufferUsage::INDEX;
    CHECK_FALSE(rhi_validate_buffer_desc(desc, error));
}

TEST_CASE("RHI validation: buffer write range")
{
    std::string error;

    CHECK(rhi_validate_buffer_write(1024, 0, 1024, error));
    CHECK(rhi_validate_buffer_write(1024, 512, 512, error));
    CHECK_FALSE(rhi_validate_buffer_write(1024, 1024, 0, error)); // size == 0 非法

    // 越界与偏移超出缓冲尾都必须被拒绝（不能出现 offset + size 溢出绕过检查）。
    CHECK_FALSE(rhi_validate_buffer_write(1024, 1024, 4, error));
    CHECK_FALSE(rhi_validate_buffer_write(1024, 0, 1025, error));
    CHECK_FALSE(rhi_validate_buffer_write(1024, 2048, 4, error));
    CHECK_FALSE(rhi_validate_buffer_write(1024, UINT64_MAX, 4, error));
}

TEST_CASE("RHI validation: texture desc")
{
    std::string error;
    TextureDescRefactor desc = make_valid_texture();
    CHECK(rhi_validate_texture_desc(desc, error));

    desc.format = Format::Undefined;
    CHECK_FALSE(rhi_validate_texture_desc(desc, error));
    CHECK(error.find("format") != std::string::npos);

    desc = make_valid_texture();
    desc.width = 0;
    CHECK_FALSE(rhi_validate_texture_desc(desc, error));

    desc = make_valid_texture();
    desc.samples_count = 2;
    CHECK_FALSE(rhi_validate_texture_desc(desc, error));

    desc = make_valid_texture();
    desc.mip_levels = 0;
    CHECK_FALSE(rhi_validate_texture_desc(desc, error));

    desc = make_valid_texture();
    desc.mip_levels = 8; // 64x64 最多 7 层（0..6）
    CHECK_FALSE(rhi_validate_texture_desc(desc, error));

    desc = make_valid_texture();
    desc.dim = TextureDimension::Cube;
    desc.width = 64;
    desc.height = 32;
    CHECK_FALSE(rhi_validate_texture_desc(desc, error));

    desc = make_valid_texture();
    desc.usage = 0;
    CHECK_FALSE(rhi_validate_texture_desc(desc, error));

    desc = make_valid_texture();
    desc.usage = TextureUsage::TEXTURE_BINDING | TextureUsage::STORAGE;
    CHECK_FALSE(rhi_validate_texture_desc(desc, error));

    desc = make_valid_texture();
    desc.usage = TextureUsage::TEXTURE_BINDING | TextureUsage::COPY_DST | TextureUsage::COPY_SRC;
    CHECK(rhi_validate_texture_desc(desc, error));
}

TEST_CASE("RHI validation: texture view desc")
{
    RhiTextureInfo texture;
    texture.format = Format::RGBA8Unorm;
    texture.dim = TextureDimension::Texture2D;
    texture.width = 64;
    texture.height = 64;
    texture.mip_levels = 4;
    texture.layers = 1;

    std::string error;
    TextureViewDesc view = make_valid_view();
    CHECK(rhi_validate_texture_view_desc(texture, view, error));

    // mip_count == 0 解析为"到最后一层"
    view.mip_count = 0;
    CHECK(rhi_validate_texture_view_desc(texture, view, error));

    view.base_mip = 2;
    view.mip_count = 3;
    CHECK_FALSE(rhi_validate_texture_view_desc(texture, view, error));

    view = make_valid_view();
    view.base_mip = 4;
    CHECK_FALSE(rhi_validate_texture_view_desc(texture, view, error));

    view = make_valid_view();
    view.dim = TextureViewDimension::Cube;
    CHECK_FALSE(rhi_validate_texture_view_desc(texture, view, error));

    view = make_valid_view();
    view.layer_count = 2;
    CHECK_FALSE(rhi_validate_texture_view_desc(texture, view, error));

    view = make_valid_view();
    view.aspect = TextureAspect::DepthOnly;
    CHECK_FALSE(rhi_validate_texture_view_desc(texture, view, error));

    // 深度纹理只能用 DepthOnly 视图
    RhiTextureInfo depth_texture = texture;
    depth_texture.format = Format::Depth32Float;
    view = make_valid_view();
    CHECK_FALSE(rhi_validate_texture_view_desc(depth_texture, view, error));
    view.aspect = TextureAspect::DepthOnly;
    CHECK(rhi_validate_texture_view_desc(depth_texture, view, error));

    // cubemap：6 层，必须按 Cube 维度查看；eCube 视图要求 base_layer 是 6 的倍数
    RhiTextureInfo cube;
    cube.format = Format::RGBA8Unorm;
    cube.dim = TextureDimension::Cube;
    cube.width = 64;
    cube.height = 64;
    cube.mip_levels = 1;
    cube.layers = 6;
    view = make_valid_view();
    view.dim = TextureViewDimension::Cube;
    CHECK(rhi_validate_texture_view_desc(cube, view, error));
    view.base_layer = 5; // 非 6 的倍数
    view.layer_count = 2;
    CHECK_FALSE(rhi_validate_texture_view_desc(cube, view, error));

    // 1D 纹理没有可用的视图维度
    RhiTextureInfo texture1d = texture;
    texture1d.dim = TextureDimension::Texture1D;
    view = make_valid_view();
    CHECK_FALSE(rhi_validate_texture_view_desc(texture1d, view, error));
}

TEST_CASE("RHI validation: texture view range")
{
    RhiTextureInfo cube;
    cube.format = Format::RGBA8Unorm;
    cube.dim = TextureDimension::Cube;
    cube.width = 64;
    cube.height = 64;
    cube.mip_levels = 5;
    cube.layers = 6;

    RhiTextureViewRange range;
    std::string error;

    // mip_count / layer_count 为 0 时解析成"到最后一层/面"
    TextureViewDesc desc;
    desc.dim = TextureViewDimension::Cube;
    CHECK(rhi_resolve_texture_view_range(cube, desc, range, error));
    CHECK(range.dim == TextureViewDimension::Cube);
    CHECK(range.aspect == TextureAspect::Color);
    CHECK(range.base_mip == 0);
    CHECK(range.mip_count == 5);
    CHECK(range.base_layer == 0);
    CHECK(range.layer_count == 6);

    // 显式范围与"0 展开"都要落到同一个结果上
    desc.base_mip = 1;
    desc.mip_count = 2;
    CHECK(rhi_resolve_texture_view_range(cube, desc, range, error));
    CHECK(range.base_mip == 1);
    CHECK(range.mip_count == 2);
    desc.mip_count = 0;
    CHECK(rhi_resolve_texture_view_range(cube, desc, range, error));
    CHECK(range.mip_count == 4);

    // cubemap 视图的 base_layer / layer_count 都必须是 6 的倍数（Vulkan eCube 约束）
    desc = TextureViewDesc{};
    desc.dim = TextureViewDimension::Cube;
    desc.base_layer = 3;
    CHECK_FALSE(rhi_resolve_texture_view_range(cube, desc, range, error));
    CHECK(error.find("multiple of 6") != std::string::npos);

    desc = TextureViewDesc{};
    desc.dim = TextureViewDimension::Cube;
    desc.layer_count = 3;
    CHECK_FALSE(rhi_resolve_texture_view_range(cube, desc, range, error));
    CHECK(error.find("multiple of 6") != std::string::npos);

    // 超大 mip_count 不能靠 uint32 溢出绕过区间检查
    desc = TextureViewDesc{};
    desc.dim = TextureViewDimension::Cube;
    desc.mip_count = UINT32_MAX;
    CHECK_FALSE(rhi_resolve_texture_view_range(cube, desc, range, error));

    // 2D 纹理解析为单层
    RhiTextureInfo texture;
    texture.format = Format::RGBA8Unorm;
    texture.dim = TextureDimension::Texture2D;
    texture.width = 64;
    texture.height = 64;
    texture.mip_levels = 4;
    texture.layers = 1;
    CHECK(rhi_resolve_texture_view_range(texture, TextureViewDesc{}, range, error));
    CHECK(range.base_mip == 0);
    CHECK(range.mip_count == 4);
    CHECK(range.base_layer == 0);
    CHECK(range.layer_count == 1);

    // 1D 纹理没有视图维度（接口不支持）
    RhiTextureInfo texture1d = texture;
    texture1d.dim = TextureDimension::Texture1D;
    CHECK_FALSE(rhi_resolve_texture_view_range(texture1d, TextureViewDesc{}, range, error));
}

TEST_CASE("RHI validation: sampler desc")
{
    std::string error;
    SamplerDesc desc;
    desc.lod_min_clamp = 0.0f;
    desc.lod_max_clamp = 1000.0f;
    CHECK(rhi_validate_sampler_desc(desc, error));

    desc.lod_min_clamp = -1.0f;
    CHECK_FALSE(rhi_validate_sampler_desc(desc, error));

    desc.lod_min_clamp = 2.0f;
    desc.lod_max_clamp = 1.0f;
    CHECK_FALSE(rhi_validate_sampler_desc(desc, error));

    // NaN / Inf 会绕过顺序比较，必须显式拒绝
    desc = SamplerDesc{};
    desc.lod_min_clamp = std::numeric_limits<float>::quiet_NaN();
    CHECK_FALSE(rhi_validate_sampler_desc(desc, error));
    CHECK(error.find("finite") != std::string::npos);

    desc = SamplerDesc{};
    desc.lod_max_clamp = std::numeric_limits<float>::infinity();
    CHECK_FALSE(rhi_validate_sampler_desc(desc, error));
    CHECK(error.find("finite") != std::string::npos);

    // maxLod 超过 VK_LOD_CLAMP_NONE（1000）在 Vulkan 里是非法描述，共享层提前拒绝
    desc = SamplerDesc{};
    desc.lod_max_clamp = 2000.0f;
    CHECK_FALSE(rhi_validate_sampler_desc(desc, error));
    CHECK(error.find("1000") != std::string::npos);

    // compare 相关字段不参与 lod 校验（compare_op 的取值域由枚举本身保证）
    desc = SamplerDesc{};
    desc.compare_enable = true;
    desc.compare_op = CompareFunction::GreaterEqual;
    CHECK(rhi_validate_sampler_desc(desc, error));
}

TEST_CASE("RHI validation: render pipeline desc")
{
    std::string error;

    RenderPipelineDesc desc = make_valid_pipeline();
    CHECK(rhi_validate_render_pipeline_desc(desc, error));

    desc.pipeline_layout.handle = 0;
    CHECK_FALSE(rhi_validate_render_pipeline_desc(desc, error));

    desc = make_valid_pipeline();
    desc.vertex.module.handle = 0;
    CHECK_FALSE(rhi_validate_render_pipeline_desc(desc, error));

    desc = make_valid_pipeline();
    desc.fragment.module.handle = 0; // 声明了颜色目标却没有 fragment 模块
    CHECK_FALSE(rhi_validate_render_pipeline_desc(desc, error));

    desc = make_valid_pipeline();
    desc.fragment.targets.clear();
    CHECK_FALSE(rhi_validate_render_pipeline_desc(desc, error)); // 既无颜色目标也无深度格式

    desc.fragment.targets.clear();
    desc.depth_stencil.format = Format::Depth32Float;
    CHECK(rhi_validate_render_pipeline_desc(desc, error));

    desc = make_valid_pipeline();
    desc.depth_stencil.format = Format::RGBA8Unorm; // 深度格式不是深度格式
    CHECK_FALSE(rhi_validate_render_pipeline_desc(desc, error));

    desc = make_valid_pipeline();
    desc.fragment.targets[0].format = Format::Depth32Float; // 颜色目标不能是深度格式
    CHECK_FALSE(rhi_validate_render_pipeline_desc(desc, error));

    desc = make_valid_pipeline();
    desc.samples_count = 2;
    CHECK_FALSE(rhi_validate_render_pipeline_desc(desc, error));

    // 顶点布局
    desc = make_valid_pipeline();
    BufferLayout buffer;
    buffer.array_stride = 12;
    BufferLayout::Attribute attribute;
    attribute.shader_location = 0;
    attribute.offset = 0;
    attribute.format = VertexFormat::Float32x3;
    buffer.attributes.push_back(attribute);
    desc.vertex.buffers.push_back(buffer);
    CHECK(rhi_validate_render_pipeline_desc(desc, error));

    desc.vertex.buffers[0].array_stride = 0;
    CHECK_FALSE(rhi_validate_render_pipeline_desc(desc, error));

    desc.vertex.buffers[0].array_stride = 12;
    desc.vertex.buffers[0].attributes[0].offset = 4; // 4 + 12 > 12
    CHECK_FALSE(rhi_validate_render_pipeline_desc(desc, error));

    desc.vertex.buffers[0].attributes[0].offset = 0;
    const BufferLayout::Attribute duplicate_attribute = desc.vertex.buffers[0].attributes[0];
    desc.vertex.buffers[0].attributes.push_back(duplicate_attribute);
    CHECK_FALSE(rhi_validate_render_pipeline_desc(desc, error)); // 重复 shader_location

    desc.vertex.buffers.push_back(buffer); // 只支持一个顶点缓冲 slot
    CHECK_FALSE(rhi_validate_render_pipeline_desc(desc, error));
}

namespace
{
    void append_spirv_word(std::vector<uint8_t> &bytes, uint32_t word)
    {
        for (uint32_t byte_index = 0; byte_index < 4; ++byte_index)
            bytes.push_back(static_cast<uint8_t>((word >> (byte_index * 8)) & 0xFFu));
    }

    /// @brief 构造只含 5 word 头 + 一条 OpEntryPoint 的最小 SPIR-V，用于校验逻辑测试。
    std::string make_spirv(uint32_t execution_model, const std::string &entry_point,
                           uint32_t version = 0x00010000u)
    {
        std::vector<uint8_t> bytes;
        append_spirv_word(bytes, 0x07230203u); // magic
        append_spirv_word(bytes, version);
        append_spirv_word(bytes, 0);  // generator
        append_spirv_word(bytes, 10); // bound
        append_spirv_word(bytes, 0);  // schema

        // OpEntryPoint: [opcode][ExecutionModel][EntryPoint id][name...]
        const size_t name_words = (entry_point.size() + 1 + 3) / 4;
        append_spirv_word(bytes, static_cast<uint32_t>(((3 + name_words) << 16) | 15));
        append_spirv_word(bytes, execution_model);
        append_spirv_word(bytes, 1);
        for (size_t word_index = 0; word_index < name_words; ++word_index)
        {
            uint32_t word = 0;
            for (size_t byte_index = 0; byte_index < 4; ++byte_index)
            {
                const size_t char_index = word_index * 4 + byte_index;
                const uint8_t value =
                    char_index < entry_point.size() ? static_cast<uint8_t>(entry_point[char_index]) : 0;
                word |= static_cast<uint32_t>(value) << (byte_index * 8);
            }
            append_spirv_word(bytes, word);
        }
        return std::string(bytes.begin(), bytes.end());
    }
} // namespace

TEST_CASE("RHI validation: shader module descriptor")
{
    std::string error;
    ShaderModuleDesc desc;
    desc.source = make_spirv(0, "main"); // 0 = Vertex
    CHECK(rhi_validate_shader_module_desc(desc, error));

    // 空源码 / 空入口名
    desc.source.clear();
    CHECK_FALSE(rhi_validate_shader_module_desc(desc, error));
    desc.source = make_spirv(0, "main");
    desc.entry_point.clear();
    CHECK_FALSE(rhi_validate_shader_module_desc(desc, error));

    // 长度必须是 4 的倍数，且不能短于 20 字节头
    desc.entry_point = "main";
    desc.source = make_spirv(0, "main");
    desc.source.push_back('\0');
    CHECK_FALSE(rhi_validate_shader_module_desc(desc, error));

    desc.source.assign(20, 'x'); // 长度合法但 magic / 版本都不对
    CHECK_FALSE(rhi_validate_shader_module_desc(desc, error));

    // 版本范围：1.6 合法，1.7 未知
    desc.source = make_spirv(0, "main", 0x00010600u);
    CHECK(rhi_validate_shader_module_desc(desc, error));
    desc.source = make_spirv(0, "main", 0x00010700u);
    CHECK_FALSE(rhi_validate_shader_module_desc(desc, error));

    // 入口点必须真实存在于 OpEntryPoint
    desc.source = make_spirv(0, "vertex_main");
    CHECK_FALSE(rhi_validate_shader_module_desc(desc, error)); // 默认入口是 main
    desc.entry_point = "vertex_main";
    CHECK(rhi_validate_shader_module_desc(desc, error));

    // stage 一致性：顶点模块不能当 fragment 用，反之亦然
    desc.type = ShaderType::FRAGMENT;
    CHECK_FALSE(rhi_validate_shader_module_desc(desc, error));
    desc.source = make_spirv(4, "main"); // 4 = Fragment
    desc.entry_point = "main";
    CHECK(rhi_validate_shader_module_desc(desc, error));
    desc.type = ShaderType::VERTEX;
    CHECK_FALSE(rhi_validate_shader_module_desc(desc, error));

    // 指令流截断 / word count 为 0 必须被拒绝
    std::string truncated = make_spirv(0, "main");
    truncated.resize(truncated.size() - 4);
    desc.source = truncated;
    desc.type = ShaderType::VERTEX;
    CHECK_FALSE(rhi_validate_shader_module_desc(desc, error));

    std::string zero_word_count = make_spirv(0, "main");
    zero_word_count.append(4, '\0');
    desc.source = zero_word_count;
    CHECK_FALSE(rhi_validate_shader_module_desc(desc, error));

    // GLSL 文本由后端决定是否支持：共享校验只要求非空
    desc.source = "#version 450\nvoid main() {}\n";
    desc.source_kind = ShaderSourceKind::GLSL_TEXT;
    CHECK(rhi_validate_shader_module_desc(desc, error));
    desc.source.clear();
    CHECK_FALSE(rhi_validate_shader_module_desc(desc, error));
}
