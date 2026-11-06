#version 330 core
precision highp float;

out vec4 FragColor;

in vec3 v_uv;

uniform samplerCube u_skybox_texture;

// 立方体贴图以 sRGB 内部格式上传，texture() 采样结果已经解码成线性值；
// 写进非 sRGB 的默认帧缓冲前必须编码回 sRGB，否则天空盒会整体偏暗。
vec3 linear_to_srgb(vec3 color)
{
    color = clamp(color, 0.0, 1.0);
    vec3 low = color * 12.92;
    vec3 high = 1.055 * pow(color, vec3(1.0 / 2.4)) - 0.055;
    return mix(low, high, step(vec3(0.0031308), color));
}

void main()
{
    FragColor = vec4(linear_to_srgb(texture(u_skybox_texture, v_uv).rgb), 1.0);
}
