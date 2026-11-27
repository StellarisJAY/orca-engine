#version 450

layout(location = 0) out vec3 gPosition;
layout(location = 1) out vec3 gNormal;
layout(location = 2) out vec4 gAlbedo;
layout(location = 3) out vec2 gMetallicRoughness;

layout(location=0) in VertexOut {
    vec3 world_position;
    vec3 normal;
    vec2 uv;
    mat3 TBN;
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
    int texture_mask;
} material_uniform;

// 贴图存在性位掩码，位序与 C++ 侧 TextureSlot 一致。
#define TEX_ALBEDO_BIT    (1 << 0)
#define TEX_NORMAL_BIT    (1 << 1)
#define TEX_ROUGHNESS_BIT (1 << 2)
#define TEX_METALLIC_BIT  (1 << 3)
#define TEX_AO_BIT        (1 << 4)
#define TEX_HEIGHT_BIT    (1 << 5)
#define HAS_TEX(bit) ((material_uniform.texture_mask & (bit)) != 0)

layout(set=1, binding=1) uniform sampler material_sampler;
layout(set=1, binding=2) uniform texture2D texture_albedo;
layout(set=1, binding=3) uniform texture2D texture_normal;
layout(set=1, binding=4) uniform texture2D texture_roughness;
layout(set=1, binding=5) uniform texture2D texture_metallic;
layout(set=1, binding=6) uniform texture2D texture_ao;
layout(set=1, binding=7) uniform texture2D texture_height;

void main()
{
    vec2 uv = vertex_out.uv;
    // position
    gPosition = vertex_out.world_position;
    // 法线贴图采样
    gNormal = vertex_out.normal;
    if(HAS_TEX(TEX_NORMAL_BIT))
    {
        vec3 tangent_normal = texture(sampler2D(texture_normal, material_sampler), uv).rgb;
        tangent_normal = tangent_normal * 2.0 - 1.0;
        tangent_normal.xy *= material_uniform.normal_scale;
        tangent_normal = normalize(tangent_normal);
        gNormal = normalize(vertex_out.TBN * tangent_normal); // 法线空间转世界空间
    }
    // 金属度、粗糙度贴图采样
    gMetallicRoughness = vec2(material_uniform.metallic, material_uniform.roughness);
    if(HAS_TEX(TEX_METALLIC_BIT))
    {
        gMetallicRoughness.x = texture(sampler2D(texture_metallic, material_sampler), uv).r;
    }
    if(HAS_TEX(TEX_ROUGHNESS_BIT))
    {
        gMetallicRoughness.y = texture(sampler2D(texture_roughness, material_sampler), uv).r;
    }
    // 基础色采样
    gAlbedo = material_uniform.albedo;
    if(HAS_TEX(TEX_ALBEDO_BIT))
    {
        gAlbedo = texture(sampler2D(texture_albedo, material_sampler), uv);
    }
}