#pragma once

#include <string>

inline const std::string builtin_vertex_shader = R"(
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

)";

inline const std::string builtin_fragment_shader = R"(
#version 330 core
precision mediump float;

uniform vec4 u_ambient_color;
uniform float u_ambient_strength;

uniform vec4 u_albedo;
uniform float u_roughness;
uniform float u_metallic;


uniform vec3 u_light_direction;
uniform int u_light_type;
uniform vec3 u_light_color;

in vec2 v_uv;
in vec3 v_normal;
in vec3 v_view_direction;

out vec4 fragColor;

// 法线分布函数，微表面模型，表面越光滑，法线越接近半程向量，高光越集中
float GGX(float NdotH)
{
    return 1;
}

// 菲涅尔方程，不同视线方向反射光不同
float Fresnel(vec3 V, vec3 H, vec3 F0)
{
    return 1;
}

// 几何分布，表面自遮挡，越粗糙遮挡越严重
float Smith()
{
    return 1;
}

void main() {
    // 现在只是平行光，所以L是照射方向取反
    vec3 L = normalize(-u_light_direction);
    // 半程向量
    vec3 H = normalize(L + v_view_direction);
    // 需要的点乘
    float NH = dot(H, v_normal); // 法线与半程
    float NV = dot(v_view_direction, v_normal); // 法线与视角方向
    float NL = dot(L, v_normal); // 法线与光向

    // F0基础反射率，金属用albedo，非金属0.004
    // vec3 F0 = mix(vec3(0.004), u_albedo, u_metallic);

    vec3 light_color = dot(normalize(-u_light_direction), v_normal) * u_light_color;
    fragColor = vec4(light_color, 1.0) * u_albedo * u_ambient_color * u_ambient_strength;
}
)";