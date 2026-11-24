#version 450

// 顶点数据

layout(location = 0) in vec3 a_position;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec2 a_uv;
layout(location = 3) in vec3 a_tangent;
layout(location = 4) in vec3 a_bitangent;

// 帧全局uniforms
layout(std140, set=0, binding=0) uniform FrameUniforms {
    mat4 view;
    mat4 projection;
    vec4 camera_position;
} frame_uniforms;

// 物体uniforms
layout(std140, set=0, binding=1) uniform ObjectUniforms {
    mat4 model;
} object_uniforms;

// 顶点着色器向片段着色器输出
layout(location = 0) out VertexOut {
    vec3 world_position;
    vec3 normal;
    vec2 uv;
} vertex_out;

void main()
{
    vec4 world_position = object_uniforms.model * vec4(a_position, 1);
    gl_Position = frame_uniforms.projection * frame_uniforms.view * world_position;
    gl_Position.y = -gl_Position.y;
    vertex_out.world_position = world_position.xyz;
    vertex_out.normal = vec3(object_uniforms.model * vec4(a_normal, 0));
    vertex_out.uv = a_uv;
}
