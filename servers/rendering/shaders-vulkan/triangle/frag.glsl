#version 450

layout(location = 0) in vec3 v_color;
layout(location = 0) out vec4 frag_color;

void main()
{
    // 颜色空间约定（G-03）：Vulkan 交换链使用 *_UnormSrgb 格式，写出的线性值
    // 由硬件完成 sRGB 编码，因此这里不能再手工调用 linear_to_srgb()。
    frag_color = vec4(v_color, 1.0);
}
