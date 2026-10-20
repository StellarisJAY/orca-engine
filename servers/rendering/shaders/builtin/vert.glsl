#version 330 core
layout(location = 0) in vec3 a_position;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec2 a_uv;

out vec2 v_uv;
out vec3 v_normal;
out vec3 v_view_direction;

uniform mat4 u_model;
uniform mat4 u_view;
uniform mat4 u_projection;
uniform vec3 u_camera_position;

uniform vec3 u_light_position;

void main() {
    // mvp 顶点变换
    vec4 surface_world_position = u_model * vec4(a_position, 1.0);
    gl_Position = u_projection * u_view * surface_world_position;
    v_uv = a_uv;
    // 法线变换
    v_normal = normalize(mat3(u_model) * a_normal);
    // 视线方向
    v_view_direction = normalize(u_camera_position - vec3(surface_world_position));
}
