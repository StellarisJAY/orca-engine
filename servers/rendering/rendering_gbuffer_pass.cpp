#include "rendering_pass.h"
#include "shader_loader.h"
#include "rendering_server.h"
#include "rendering_uniforms.h"

namespace
{
    void init_shaders(ShaderHandle &main_vertex_shader, ShaderHandle &main_fragment_shader, BufferLayout &vertex_layout)
    {
        auto rendering_device = RenderingDeviceRefactor::get_singleton();
        // 加载内置shader
        const auto shader_binary_source = ShaderLoader::load_spirv("builtin-gbuffer");
        if (!shader_binary_source.is_valid())
        {
            throw std::runtime_error("unable to load shader sources");
        }

        // position,normal,uv,tangent,bitangent
        // vec3, vec3, vec2, vec3, vec3
        vertex_layout.array_stride = sizeof(float) * 14;
        // vec3 posiiton
        vertex_layout.attributes.emplace_back(BufferLayout::Attribute{
            .shader_location = 0,
            .offset = 0,
            .format = VertexFormat::Float32x3});
        // vec3 normal
        vertex_layout.attributes.emplace_back(BufferLayout::Attribute{
            .shader_location = 1,
            .offset = sizeof(float) * 3,
            .format = VertexFormat::Float32x3});
        // vec2 uv
        vertex_layout.attributes.emplace_back(BufferLayout::Attribute{
            .shader_location = 2,
            .offset = sizeof(float) * 6,
            .format = VertexFormat::Float32x2});
        // vec3 tangent
        vertex_layout.attributes.emplace_back(BufferLayout::Attribute{
            .shader_location = 3,
            .offset = sizeof(float) * 8,
            .format = VertexFormat::Float32x3});
        // vec3 bitangent
        vertex_layout.attributes.emplace_back(BufferLayout::Attribute{
            .shader_location = 4,
            .offset = sizeof(float) * 11,
            .format = VertexFormat::Float32x3});

        ShaderModuleDesc vertex_desc;
        vertex_desc.type = ShaderType::VERTEX;
        vertex_desc.source_kind = ShaderSourceKind::SPIRV;
        vertex_desc.source = std::string(reinterpret_cast<const char *>(shader_binary_source.vertex.data()), shader_binary_source.vertex.size());
        main_vertex_shader = rendering_device->create_shader_module(vertex_desc);

        ShaderModuleDesc frag_desc;
        frag_desc.type = ShaderType::FRAGMENT;
        frag_desc.source_kind = ShaderSourceKind::SPIRV;
        frag_desc.source = std::string(reinterpret_cast<const char *>(shader_binary_source.fragment.data()), shader_binary_source.fragment.size());
        main_fragment_shader = rendering_device->create_shader_module(frag_desc);
    }
}

void RenderingGBufferPass::init(const RenderPassContext &context)
{
    auto rendering_device = RenderingDeviceRefactor::get_singleton();
    RenderPipelineDesc pipeline_desc;
    BufferLayout vertex_layout;
    init_shaders(vertex_shader, fragment_shader, vertex_layout);
    pipeline_desc.vertex.module = vertex_shader;
    pipeline_desc.vertex.buffers.emplace_back(vertex_layout);
    pipeline_desc.fragment.module = fragment_shader;

    PipelineLayoutDesc layout;
    layout.bind_group_layouts.emplace_back(context.frame_objects_layout);
    layout.bind_group_layouts.emplace_back(context.material_layout);
    pipeline_layout = rendering_device->create_pipeline_layout(layout);

    pipeline_desc.pipeline_layout = pipeline_layout;
    pipeline_desc.primitive.cull_mode = Primitive::CullMode::Back;
    pipeline_desc.primitive.front_face = Primitive::FrontFace::CCW;
    pipeline_desc.primitive.topology = Primitive::Topology::TriangleList;
    pipeline_desc.depth_stencil.depth_compare = CompareFunction::LessEqual;
    pipeline_desc.depth_stencil.depth_write_enabled = true;

    pipeline = rendering_device->create_render_pipeline(pipeline_desc);
}

void RenderingGBufferPass::process(const RenderPassContext &context, const std::vector<DrawItem> &draw_items, void *custom_data)
{
    auto rendering_device = RenderingDeviceRefactor::get_singleton();
    RenderPassDesc pass_desc;
    uint32_t swp_w, swp_h;
    rendering_device->get_swapchain_size(swp_w, swp_h);
    pass_desc.width = swp_w;
    pass_desc.height = swp_h;
    pass_desc.color_attachments.emplace_back(ColorAttachmentDesc{
        .view = context.g_position,
        .clear_color = {0, 0, 0, 0},
    });
    pass_desc.color_attachments.emplace_back(ColorAttachmentDesc{
        .view = context.g_normal,
        .clear_color = {0, 0, 1, 0},
    });
    pass_desc.color_attachments.emplace_back(ColorAttachmentDesc{
        .view = context.g_metallic_roughness,
        .clear_color = {0, 0.5, 0, 0},
    });
    pass_desc.color_attachments.emplace_back(ColorAttachmentDesc{
        .view = context.g_albedo,
        .clear_color = {1, 1, 1, 1},
    });

    auto render_pass = rendering_device->encoder_begin_render_pass(context.encoder, pass_desc);
    rendering_device->pass_set_pipeline(render_pass, pipeline);
    rendering_device->pass_set_viewport(render_pass, 0, 0, swp_w, swp_h);
    rendering_device->pass_set_bind_group(render_pass, 0, context.frame_obj_bind_group, nullptr, 0);

    for(const auto& item : draw_items)
    {
        rendering_device->pass_set_vertex_buffer(render_pass, 0, item.vertex_buffer, 0);
        rendering_device->pass_set_index_buffer(render_pass, item.index_buffer);
        rendering_device->pass_draw_indexed(render_pass, item.index_count);
    }

    rendering_device->pass_end(render_pass);
}

void RenderingGBufferPass::destroy()
{
    auto rendering_device = RenderingDeviceRefactor::get_singleton();
    rendering_device->destroy_render_pipeline(pipeline);
    rendering_device->destroy_pipeline_layout(pipeline_layout);
    rendering_device->destroy_shader_module(vertex_shader);
    rendering_device->destroy_shader_module(fragment_shader);
}