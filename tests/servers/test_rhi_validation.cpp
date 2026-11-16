#include "doctest/doctest.h"
#include "rhi_validation.h"

#include <string>

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

    // cubemap：6 层，必须按 Cube 维度查看
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
    view.base_layer = 5;
    view.layer_count = 2;
    CHECK_FALSE(rhi_validate_texture_view_desc(cube, view, error));
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
