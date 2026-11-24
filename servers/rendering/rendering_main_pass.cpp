#include "rendering_server.h"
#include "rendering_uniforms.h"
#include <algorithm>

void RenderingServer::init_pass_scene()
{
    auto rendering_device = RenderingDeviceRefactor::get_singleton();
    RenderPipelineDesc pipeline_desc;
    pipeline_desc.label = "main-pipeline";
    // 加载内置shader

    // 图元属性
    pipeline_desc.primitive.cull_mode = Primitive::CullMode::Back;
    pipeline_desc.primitive.front_face = Primitive::FrontFace::CCW;
    pipeline_desc.primitive.topology = Primitive::Topology::TriangleList;
    // 深度测试
    pipeline_desc.depth_stencil.depth_compare = CompareFunction::LessEqual;
    pipeline_desc.depth_stencil.depth_write_enabled = true;
    pipeline_desc.depth_stencil.format = Format::Depth32Float;
    // 管线布局
    PipelineLayoutDesc pipeline_layout_desc;
    pipeline_layout_desc.label = "main";
    pipeline_layout_desc.bind_group_layouts.emplace_back(frame_objects_layout);
    pipeline_layout_desc.bind_group_layouts.emplace_back(material_layout);
    pipeline_desc.pipeline_layout = rendering_device->create_pipeline_layout(pipeline_layout_desc);
    
    main_pipeline = rendering_device->create_render_pipeline(pipeline_desc);
}

void RenderingServer::render_pass_scene(SceneData *scene_data, EnvironmentData *env_data, CameraData *camera_data)
{
    auto rendering_device = RenderingDeviceRefactor::get_singleton();
    real_t camera_transform[16];
    camera_data->transform.affine_inverse().get_mat4(camera_transform);
    const real_t *projection = camera_data->projection.data;

    // 当前帧的全局uniforms
    FrameUniforms frame_uniforms;
    // view变换
    std::copy(frame_uniforms.view, frame_uniforms.view + 16, camera_transform);
    // projection变换
    std::copy(frame_uniforms.projection, frame_uniforms.projection + 16, camera_data->projection.data);
    // 相机世界坐标
    std::copy(frame_uniforms.camera_position, frame_uniforms.camera_position + 3, camera_data->transform.origin.coords);
    // 环境光颜色和强度
    real_t ambient_color[3];
    env_data->ambient_color.normalize_rgb(ambient_color);
    std::copy(frame_uniforms.ambient_color_strength, frame_uniforms.ambient_color_strength + 3, ambient_color);
    std::copy(frame_uniforms.ambient_color_strength + 3, frame_uniforms.ambient_color_strength + 4, env_data->ambient_strength);
    // TODO 灯光uniforms

    // 写入uniform buffer
    rendering_device->queue_write_buffer(frame_uniform_buffer, 0, &frame_uniforms, sizeof(FrameUniforms));

    uint32_t viewport_w, viewport_h;
    rendering_device->get_swapchain_size(viewport_w, viewport_h);
    RenderPassDesc pass_desc;
    pass_desc.width = viewport_w;
    pass_desc.height = viewport_h;
    ColorAttachmentDesc color_attachment_desc;
    std::fill(color_attachment_desc.clear_color, color_attachment_desc.clear_color + 3, 0.5);
    color_attachment_desc.load_op = LoadOp::Clear;
    color_attachment_desc.store_op = StoreOp::Discard;

    // 开始render pass，绑定渲染管线
    const auto render_pass = rendering_device->encoder_begin_render_pass(encoder, pass_desc);
    rendering_device->pass_set_pipeline(render_pass, main_pipeline);
    rendering_device->pass_set_viewport(render_pass, 0, 0, viewport_w, viewport_h, 0, 1.0);

    // 绘制物体
    for(const auto& item : draw_items)
    {
        const auto material_data = item.material;
        
    }

    rendering_device->pass_end(render_pass);
}