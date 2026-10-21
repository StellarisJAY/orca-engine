#version 330 core
precision mediump float;

#define PI 3.1415926538

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
    float a2 = u_roughness * u_roughness * u_roughness * u_roughness;
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

void main() {
    // 现在只是平行光，所以L是照射方向取反
    vec3 L = normalize(-u_light_direction);
    // 半程向量
    vec3 H = normalize(L + v_view_direction);
    // 需要的点乘
    float NdotH = max(dot(H, v_normal), 0.0); // 法线与半程
    float NdotV = max(dot(v_view_direction, v_normal), 0.001); // 法线与视角方向
    float NdotL = max(dot(L, v_normal), 0.0); // 法线与光向
    float VdotH = dot(H, v_view_direction);

    if(NdotL <= 0.0)  {
        fragColor = u_albedo * u_ambient_color * u_ambient_strength;
        return;
    }

    // F0基础反射率，金属用albedo，非金属0.04
    vec3 F0 = vec3(0.04);
    vec3 albedo = vec3(u_albedo);
    F0 = mix(F0, albedo, u_metallic);

    // 漫反射
    vec3 f_diffuse = albedo / PI;

    float D = GGX(NdotH);
    vec3 F = Fresnel(VdotH, F0);
    float G = GeometrySmith(NdotV, NdotL, pow(u_roughness+1, 2)/8.0);
    vec3 f_specular = D * F * G / (4.0 * NdotV * NdotL);

    vec3 Kd = (1 - F) * (1 - u_metallic);
    vec3 Lo = (Kd * f_diffuse + f_specular) * u_light_color * NdotL * 2.0;
    vec3 ambient = albedo * u_ambient_color.rgb * u_ambient_strength;
    fragColor = vec4(Lo + ambient, 1.0);
}