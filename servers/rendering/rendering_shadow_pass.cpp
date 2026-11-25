#include "rendering_server.h"
#include "shader_loader.h"
#include "rendering_pass.h"

constexpr static uint32_t DEPTH_MAP_W = 1024, DEPTH_MAP_H = 1024;

RID RenderingServer::create_light()
{
    return storage.lights.create(LightData{
        .transform = Transform3D(),
        .final_color = Color(255, 255, 255),
        .type = 0,
        .intensity = 1.0f,
    });
}

void RenderingServer::set_light_transform(RID light, const Transform3D &transform)
{
    if (auto light_data = storage.lights.get_data(light))
    {
        light_data->transform = transform;
    }
}

void RenderingServer::set_light_color(RID light, const Color &color)
{
    LightData *light_data = storage.lights.get_data(light);
    if (light_data)
    {
        // TODO 温度和颜色计算最终颜色
        light_data->final_color = color;
        light_data->base_color = color;
    }
}

void RenderingServer::set_light_type(RID light, int type)
{
    if (auto light_data = storage.lights.get_data(light))
    {
        light_data->type = type;
    }
}

void RenderingServer::set_light_scene(RID light, RID scene)
{
    SceneData *scene_data = storage.scenes.get_data(scene);
    if (scene_data)
    {
        for (auto l : scene_data->lights)
        {
            if (l == light)
                return;
        }
        scene_data->lights.push_back(light);
    }
}

void RenderingServer::set_light_cast_shadow(RID light, bool shadow)
{
    auto rendering_device = RenderingDeviceRefactor::get_singleton();
    auto light_data = storage.lights.get_data(light);
    if (light_data == nullptr)
        return;
    if (light_data->cast_shadow && shadow)
        return;
    // 关闭阴影，销毁灯光深度图
    if (light_data->cast_shadow && !shadow)
    {
        if (light_data->depth_map_view.is_valid())
            rendering_device->destroy_texture_view(light_data->depth_map_view);
        if (light_data->depth_map.is_valid())
            rendering_device->destroy_texture(light_data->depth_map);
        return;
    }
    // 开启阴影，创建灯光深度图
    if (shadow)
    {
        const auto depth_map = rendering_device->create_texture(TextureDescRefactor{
            .dim = TextureDimension::Texture2D, // TODO 点光源的立方体贴图
            .format = Format::Depth32Float,
            .width = DEPTH_MAP_W,
            .height = DEPTH_MAP_H,
            .usage = TextureUsage::RENDER_ATTACHMENT,
        });
        const auto depth_map_view = rendering_device->create_texture_view(depth_map, TextureViewDesc{
                                                                                         .dim = TextureViewDimension::Texture2D,
                                                                                         .aspect = TextureAspect::DepthOnly,
                                                                                     });
        light_data->depth_map = depth_map;
        light_data->depth_map_view = depth_map_view;
    }
}

void RenderingServer::set_light_temperature(RID light, float temperature)
{
    LightData *light_data = storage.lights.get_data(light);
    if (light_data)
    {
        // TODO 温度和颜色计算最终颜色
        light_data->temperature = temperature;
    }
}

void RenderingServer::set_light_direction(RID light, const Vector3 &direction)
{
    if (auto light_data = storage.lights.get_data(light))
    {
        light_data->direction = direction;
        light_data->light_space_transform = Transform3D::look_at(direction * light_data->shadow_distance * -1, direction, Vector3::UP);
    }
}

void RenderingServer::set_light_intensity(RID light, float intensity)
{
    if (auto light_data = storage.lights.get_data(light))
    {
        light_data->intensity = std::max(0.0f, intensity);
    }
}

namespace
{
    void init_depth_map_shaders(ShaderHandle &vertex_shader, ShaderHandle &fragment_shader, BufferLayout &vertex_layout)
    {
        auto rendering_device = RenderingDeviceRefactor::get_singleton();
        const auto shader_source = ShaderLoader::load_spirv("builtin-depth");
        if (!shader_source.is_valid())
        {
            throw std::runtime_error("unable to load depth map shaders");
        }

        vertex_shader = rendering_device->create_shader_module(ShaderModuleDesc{
            .source = std::string(reinterpret_cast<const char *>(shader_source.vertex.data()), shader_source.vertex.size()),
            .type = ShaderType::VERTEX,
        });

        fragment_shader = rendering_device->create_shader_module(ShaderModuleDesc{
            .source = std::string(reinterpret_cast<const char *>(shader_source.fragment.data()), shader_source.fragment.size()),
            .type = ShaderType::FRAGMENT,
        });

        vertex_layout = BufferLayout{
            .array_stride = sizeof(float) * 14,
        };
        vertex_layout.attributes.emplace_back(BufferLayout::Attribute{
            .shader_location = 0,
            .offset = 0,
            .format = VertexFormat::Float32x3,
        });
    }
}

void RenderingDepthMapPass::init(const RenderPassContext &context)
{
    auto rendering_device = RenderingDeviceRefactor::get_singleton();
    RenderPipelineDesc pipeline_desc;
    BufferLayout vertex_layout;
    init_depth_map_shaders(vertex_shader, fragment_shader, vertex_layout);
    pipeline_desc.vertex.module = vertex_shader;
    pipeline_desc.vertex.buffers.emplace_back(vertex_layout);
    pipeline_desc.fragment.module = fragment_shader;
    pipeline_desc.fragment.targets.emplace_back(ColorTargetState{
        .format = Format::Depth32Float,
    });
    pipeline_desc.depth_stencil.format = Format::Depth32Float;
    pipeline_desc.depth_stencil.depth_compare = CompareFunction::LessEqual;
    pipeline_desc.depth_stencil.depth_write_enabled = true;

    pipeline_desc.primitive.cull_mode = Primitive::CullMode::Back;
    pipeline_desc.primitive.front_face = Primitive::FrontFace::CCW;
    pipeline_desc.primitive.topology = Primitive::Topology::TriangleList;

    depthmap_pipeline = rendering_device->create_render_pipeline(pipeline_desc);
}

void RenderingDepthMapPass::process(const RenderPassContext &context, const std::vector<DrawItem> &draw_items, void *custom_data)
{
    auto rendering_device = RenderingDeviceRefactor::get_singleton();
    const auto light_data = static_cast<LightData*>(custom_data);
    if(!light_data) throw std::runtime_error("missing light data for depth map render pass");

    RenderPassDesc pass_desc;
    pass_desc.width = DEPTH_MAP_W;
    pass_desc.height = DEPTH_MAP_H;
    pass_desc.depth_attachments.emplace_back(DepthAttachmentDesc{
        .view = light_data->depth_map_view,
    });
    auto render_pass = rendering_device->encoder_begin_render_pass(context.encoder, pass_desc);
    rendering_device->pass_set_pipeline(render_pass, depthmap_pipeline);
    rendering_device->pass_set_bind_group(render_pass, 0, context.frame_obj_bind_group);

    rendering_device->pass_end(render_pass);
}

void RenderingDepthMapPass::destroy()
{
    auto rendering_device = RenderingDeviceRefactor::get_singleton();
    if(depthmap_pipeline.is_valid())
        rendering_device->destroy_render_pipeline(depthmap_pipeline);
    if(vertex_shader.is_valid())
        rendering_device->destroy_shader_module(vertex_shader);
    if(fragment_shader.is_valid())
        rendering_device->destroy_shader_module(fragment_shader);
}