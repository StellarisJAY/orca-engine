#include "rendering_server.h"
#include "rendering_uniforms.h"
#include <algorithm>

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
}