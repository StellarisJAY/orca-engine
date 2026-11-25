#version 450

// 物体顶点坐标
layout(location = 0) in vec3 a_position;

layout(std140, set=0, binding=1) uniform ObjectUniform {
    mat4 model; // 物体世界空间变换
} object_uniform;

layout(std140, set=2, binding=0) uniform LightUniform {
    mat4 light_view; // 灯光view变换
    mat4 light_projection; // 灯光投影变换
} light_uniform;

void main()
{
    // 从灯光位置观察世界
    gl_Position = light_uniform.light_projection * light_uniform.light_view * object_uniform.model * vec4(a_position, 1.0);
}