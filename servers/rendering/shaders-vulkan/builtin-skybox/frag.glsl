#version 450

layout(location = 0) in vec3 v_uv;
layout(location = 0) out vec4 frag_color;

layout(set = 1, binding = 0) uniform samplerCube u_skybox_texture;

void main()
{
    // 立方体贴图按 sRGB 内部格式上传，采样结果已由硬件解码成线性值；
    // 交换链是 *_UnormSrgb，写入时硬件再做 sRGB 编码，所以这里不能手工编码（G-03）。
    frag_color = vec4(texture(u_skybox_texture, v_uv).rgb, 1.0);
}
