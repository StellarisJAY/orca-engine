#version 450

layout(location = 0) in VertexOut {
    vec3 world_position;
    vec3 normal;
    vec2 uv;
} vertex_out;

layout(std140, set=1, binding=0) uniform MaterialUniform {
    vec4 albedo;
    float metallic;
    float roughness;
    float parallax_scale;
    float normal_scale;
    vec2 uv_scale;
    float ao_strength;
    int parallax_flip;
} material_uniform;

layout(std140, set=1, binding=1) uniform sampler material_sampler;
layout(std140, set=1, binding=2) uniform texture2D texture_albedo;
layout(std140, set=1, binding=3) uniform texture2D texture_normal;
layout(std140, set=1, binding=4) uniform texture2D texture_roughness;
layout(std140, set=1, binding=5) uniform texture2D texture_metallic;
layout(std140, set=1, binding=6) uniform texture2D texture_ao;
layout(std140, set=1, binding=7) uniform texture2D texture_height;

void main()
{
    // TODO fragment shader
    frag_color = vec4(1.0,1.0,1.0,1.0);
}
