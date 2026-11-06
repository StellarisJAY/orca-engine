#version 330 core
precision highp float;

#define PI 3.1415926538

// 贴图存在性位掩码，位序与 C++ 侧 TextureSlot 一致。
#define TEX_ALBEDO_BIT    (1 << 0)
#define TEX_NORMAL_BIT    (1 << 1)
#define TEX_ROUGHNESS_BIT (1 << 2)
#define TEX_METALLIC_BIT  (1 << 3)
#define TEX_AO_BIT        (1 << 4)
#define TEX_HEIGHT_BIT    (1 << 5)

// 颜色管线：光照全部在线性空间计算。
// - 颜色贴图以 sRGB 内部格式上传（GL_SRGB8/SRGB8_ALPHA8），采样时由硬件解码成线性；
// - C++ 侧上传的 albedo / 环境光 / 灯光颜色已经过 Color::linear 做 sRGB -> linear；
// - 因此最终颜色是线性值，写进非 sRGB 的默认帧缓冲前必须手动编码回 sRGB，
//   否则整幅画面会偏暗（mid-tone 被当成线性值直接显示）。
vec3 linear_to_srgb(vec3 color)
{
    color = clamp(color, 0.0, 1.0);
    vec3 low = color * 12.92;
    vec3 high = 1.055 * pow(color, vec3(1.0 / 2.4)) - 0.055;
    return mix(low, high, step(vec3(0.0031308), color));
}

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

// 0 表示没有绑定对应贴图，缺失的槽位不会被采样。
uniform int u_texture_mask;
#define HAS_TEX(bit) ((u_texture_mask & (bit)) != 0)

uniform float u_normal_scale;
uniform float u_ao_strength;
uniform vec2 u_uv_scale;

uniform float u_parallax_scale;
uniform bool u_parallax_flip;

uniform vec3 u_light_direction;
uniform int u_light_type;
uniform vec3 u_light_color;
uniform float u_light_intensity;

uniform vec3 u_camera_position;

in vec2 v_uv;
in vec3 v_normal;
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
vec3 Fresnel(float VdotH, vec3 F0)
{
    return F0 + (1.0 - F0) * pow(1.0 - VdotH, 5.0);
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

#define POM_MAX_STEPS 32
#define POM_MIN_STEPS 8

// 约定 1.0 为表面最高点。
// 高度图必须使用 LOD0：带 mipmap 的高度场会随距离模糊，
// 深度剖面变化会在画面上形成距离相关的分层和条带。
float sample_height(vec2 uv)
{
    float h = textureLod(u_height_texture, uv, 0.0).r;
    return u_parallax_flip ? 1.0 - h : h;
}

vec2 parallax_mapping(vec2 uv, vec3 view_tangent)
{
    if (!HAS_TEX(TEX_HEIGHT_BIT) || u_parallax_scale <= 0.0)
        return uv;

    // 背面或极端掠射不做偏移，避免 view.z 接近 0 时偏移爆炸。
    if (view_tangent.z <= 1e-4)
        return uv;

    // 视差高度以 UV 为单位，限制最大值，避免默认值或 -bm 过大时
    // 一步跨过多个纹素，产生明显的分层撕裂。
    float scale = clamp(u_parallax_scale, 0.0, 0.1);

    // 视线在切线空间投影到 UV 平面。掠射角时限制分母，避免偏移过大。
    vec2 ray_dir = view_tangent.xy / max(view_tangent.z, 0.25);

    // 正对时少走几层，掠射时多走几层。
    float ndv = clamp(view_tangent.z, 0.0, 1.0);
    int steps = int(mix(float(POM_MAX_STEPS), float(POM_MIN_STEPS), ndv));
    float layer_depth = 1.0 / float(steps);

    float prev_t = 0.0;
    vec2 prev_uv = uv;
    // surface_depth: 0 表示表面就在顶层，1 表示在底层。
    float prev_surface = 1.0 - sample_height(uv);
    if (prev_surface <= 0.0)
        return uv;

    for (int i = 1; i <= POM_MAX_STEPS; ++i)
    {
        if (i > steps)
            break;

        float t = float(i) * layer_depth;
        vec2 sample_uv = uv - ray_dir * (scale * t);

        float surface = 1.0 - sample_height(sample_uv);

        // 射线深度到达表面深度，说明穿过了高度场。
        if (t >= surface)
        {
            // 在上一采样层和当前采样层之间线性插值，消除层阶。
            float f_prev = prev_t - prev_surface;
            float f_cur = t - surface;
            float denom = f_prev - f_cur;
            float alpha = abs(denom) > 1e-5
                        ? clamp(f_prev / denom, 0.0, 1.0)
                        : 1.0;
            vec2 hit_uv = mix(prev_uv, sample_uv, alpha);

            // 轮廓处淡出，避免掠射角出现硬边和撕裂。
            float fade = smoothstep(0.0, 0.25, ndv);
            return mix(uv, hit_uv, fade);
        }

        prev_t = t;
        prev_uv = sample_uv;
        prev_surface = surface;
    }

    // 没有有效交点时保持原 UV，绝不能返回最大偏移。
    return uv;
}

void main() {
    // uv缩放
    vec2 base_uv = v_uv * u_uv_scale;
    // 使用基础 UV 的导数，避免视差分支导致 mip 选择跳变。
    vec2 duv_dx = dFdx(base_uv);
    vec2 duv_dy = dFdy(base_uv);

    // 逐像素视线方向比插值后的归一化向量更准确。
    vec3 V = normalize(u_camera_position - v_world_position);
    // v_TBN 是切线空间 -> 世界空间，左乘其转置得到切线空间视线。
    vec3 tangent_view = normalize(transpose(v_TBN) * V);

    // 视差贴图
    vec2 uv = parallax_mapping(base_uv, tangent_view);

    // 基础色；没有 albedo 贴图时只使用标量颜色。
    vec3 albedo = u_albedo.rgb;
    if (HAS_TEX(TEX_ALBEDO_BIT))
        albedo *= textureGrad(u_albedo_texture, uv, duv_dx, duv_dy).rgb;

    // 金属度；没有贴图时保留标量金属度，不能用默认黑色贴图把它乘成 0。
    float metallic = u_metallic;
    if (HAS_TEX(TEX_METALLIC_BIT))
        metallic *= textureGrad(u_metallic_texture, uv, duv_dx, duv_dy).r;
    metallic = clamp(metallic, 0.0, 1.0);

    // 粗糙度；没有贴图时保留标量粗糙度。
    float roughness = u_roughness;
    if (HAS_TEX(TEX_ROUGHNESS_BIT))
        roughness *= textureGrad(u_roughness_texture, uv, duv_dx, duv_dy).r;
    roughness = clamp(roughness, 0.04, 1.0);

    // 环境光遮蔽；没有贴图时按 1 处理。
    float ao = 1.0;
    if (HAS_TEX(TEX_AO_BIT))
        ao = mix(1.0,
                 textureGrad(u_ao_texture, uv, duv_dx, duv_dy).r,
                 clamp(u_ao_strength, 0.0, 1.0));

    // 法线贴图：只有材质真正绑定了法线贴图时才走切线空间；
    // 否则直接使用顶点插值得到的几何法线，避免退化 TBN 破坏 v_normal。
    vec3 normal = normalize(v_normal);
    if (HAS_TEX(TEX_NORMAL_BIT))
    {
        vec3 tangent_normal = textureGrad(u_normal_texture, uv, duv_dx, duv_dy).rgb;
        tangent_normal = tangent_normal * 2.0 - 1.0;
        tangent_normal.xy *= u_normal_scale;
        tangent_normal = normalize(tangent_normal);
        normal = normalize(v_TBN * tangent_normal);
    }

    // 环境光项；有贴图时 AO 参与遮蔽，无贴图时 AO 为 1。
    vec3 ambient = albedo * u_ambient_color.rgb
                 * max(u_ambient_strength, 0.0) * ao;

    // 现在只是平行光，所以 L 是照射方向取反。
    vec3 light_dir = -u_light_direction;
    if (u_light_intensity <= 0.0 || dot(light_dir, light_dir) < 1e-8)
    {
        fragColor = vec4(linear_to_srgb(ambient), 1.0);
        return;
    }

    vec3 L = normalize(light_dir);
    // 半程向量；L + V 退化成零向量时退回法线，避免 NaN。
    vec3 half_sum = L + V;
    vec3 H = dot(half_sum, half_sum) > 1e-8 ? normalize(half_sum) : normal;

    // 需要的点乘
    float NdotH = max(dot(H, normal), 0.0); // 法线与半程
    float NdotV = max(dot(V, normal), 0.001); // 法线与视角方向
    float NdotL = max(dot(L, normal), 0.0); // 法线与光向
    float VdotH = max(dot(H, V), 0.0);

    // 无光面
    if (NdotL <= 0.0)
    {
        fragColor = vec4(linear_to_srgb(ambient), 1.0);
        return;
    }

    // F0基础反射率，金属用albedo，非金属0.04
    vec3 F0 = vec3(0.04);
    F0 = mix(F0, albedo, metallic);

    // 漫反射
    vec3 f_diffuse = albedo / PI;

    float D = GGX(NdotH, roughness);
    vec3 F = Fresnel(VdotH, F0);
    float G = GeometrySmith(NdotV, NdotL, pow(roughness + 1.0, 2.0) / 8.0);
    vec3 f_specular = D * F * G / (4.0 * NdotV * NdotL);

    vec3 Kd = (1.0 - F) * (1.0 - metallic);
    // 灯光强度由 Light 参数提供，不再写死乘 4。
    vec3 radiance = u_light_color * max(u_light_intensity, 0.0);
    vec3 Lo = (Kd * f_diffuse * ao + f_specular) * radiance * NdotL;
    // 线性光照结果编码成 sRGB 后输出画面。
    fragColor = vec4(linear_to_srgb(Lo + ambient), 1.0);
}
