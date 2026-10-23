#version 330 core
precision mediump float;

#define PI 3.1415926538

uniform vec4 u_ambient_color;
uniform float u_ambient_strength;

uniform vec4 u_albedo;
uniform float u_roughness;
uniform float u_metallic;

// 基础色
uniform sampler2D u_albedo_texture;
// 法线贴图
uniform sampler2D u_normal_texture; 
// 粗糙度贴图
uniform sampler2D u_roughness_texture;
// 金属度贴图
uniform sampler2D u_metallic_texture;
// AO贴图
uniform sampler2D u_ao_texture;
// 高度贴图
uniform sampler2D u_height_texture;

uniform float u_normal_scale;
uniform float u_ao_strength;
uniform vec2 u_uv_scale = vec2(2.0, 2.0);
uniform float u_height_scale = 3.0;

uniform vec3 u_light_direction;
uniform int u_light_type;
uniform vec3 u_light_color;

in vec2 v_uv;
in vec3 v_normal;
in vec3 v_view_direction;
in vec3 v_tangent_view_dir;
in vec3 v_world_position;
in mat3 v_TBN;

out vec4 fragColor;

// 法线分布函数，微表面模型，表面越光滑，法线越接近半程向量，高光越集中
float GGX(float NdotH, float roughness)
{
    float a2 = roughness * roughness * roughness * roughness;
    return a2 / (PI * pow(NdotH * NdotH * (a2 - 1) + 1, 2));
}

// 菲涅尔方程，不同视线方向反射光不同
vec3 Fresnel(float NdotV, vec3 F0)
{
    return F0 + (1.0 - F0) * pow(1.0 - NdotV, 5.0);
}

float GeometrySchlickGGX(float NdotV, float k)
{
    float nom   = NdotV;
    float denom = NdotV * (1.0 - k) + k;

    return nom / denom;
}

// 几何分布，表面自遮挡，越粗糙遮挡越严重
float GeometrySmith(float NdotV, float NdotL, float k)
{
    float ggx1 = GeometrySchlickGGX(NdotV, k);
    float ggx2 = GeometrySchlickGGX(NdotL, k);

    return ggx1 * ggx2;
}

vec2 parallax_mapping(vec2 uv, vec3 view_direction)
{
    // 高度
    float height = texture(u_height_texture, uv).r * u_height_scale;
    vec2 p = normalize(view_direction).xy * height;
    uv = uv - p;
    return uv;
}

void main() {
    // uv缩放
    vec2 uv = v_uv * u_uv_scale;
    // 视差贴图
    uv = parallax_mapping(uv, v_tangent_view_dir);
    // 基础色
    vec4 base_color = u_albedo * texture(u_albedo_texture, uv);
    vec3 albedo = vec3(base_color);
    // 金属度
    float metallic = clamp(u_metallic * texture(u_metallic_texture, uv).r, 0.0, 1.0);
    // 粗糙度
    float roughness = clamp(u_roughness * texture(u_roughness_texture, uv).r, 0.04, 1.0);
    // TODO 暂时没有AO

    // 法线贴图，从切向空间转换到世界空间
    vec3 normal = texture(u_normal_texture, uv).rgb;
    normal = normalize(normal * 2.0 - 1.0);
    normal = inverse(v_TBN) * normal;

    // 现在只是平行光，所以L是照射方向取反
    vec3 L = normalize(-u_light_direction);
    // 半程向量
    vec3 H = normalize(L + v_view_direction);
    // 需要的点乘
    float NdotH = max(dot(H, normal), 0.0); // 法线与半程
    float NdotV = max(dot(v_view_direction, normal), 0.001); // 法线与视角方向
    float NdotL = max(dot(L, normal), 0.0); // 法线与光向
    float VdotH = dot(H, v_view_direction);

    // 无光面
    if(NdotL <= 0.0)  {
        fragColor = vec4(albedo, 1.0) * u_ambient_color * u_ambient_strength;
        return;
    }

    // F0基础反射率，金属用albedo，非金属0.04
    vec3 F0 = vec3(0.04);
    F0 = mix(F0, albedo, metallic);

    // 漫反射
    vec3 f_diffuse = albedo / PI;

    float D = GGX(NdotH, roughness);
    vec3 F = Fresnel(VdotH, F0);
    float G = GeometrySmith(NdotV, NdotL, pow(roughness+1, 2)/8.0);
    vec3 f_specular = D * F * G / (4.0 * NdotV * NdotL);

    vec3 Kd = (1 - F) * (1 - metallic);
    vec3 Lo = (Kd * f_diffuse + f_specular) * u_light_color * NdotL * 4.0;
    vec3 ambient = albedo * u_ambient_color.rgb * u_ambient_strength;
    fragColor = vec4(Lo + ambient, 1.0);
}