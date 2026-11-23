#include "rendering_server.h"
#include "rendering_uniforms.h"

void RenderingServer::render_pass_scene(SceneData *scene_data, EnvironmentData *env_data, CameraData *camera_data)
{
    auto rendering_device = RenderingDevice::get_singleton();
    real_t camera_transform[16];
    camera_data->transform.affine_inverse().get_mat4(camera_transform);
    const real_t *projection = camera_data->projection.data;

    FrameUniforms frame_uniforms;
    frame_uniforms.view = camera_transform;
    frame_uniforms.projection = camera_data->projection.data;
    frame_uniforms.camera_position = camera_data->transform.origin;
}