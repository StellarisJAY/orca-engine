#version 450

// V4 工具链的最小验证用顶点着色器：
// 不绑定任何描述符，靠 gl_VertexIndex 生成一个彩色三角形，用于确认
// glslc -> .spv -> create_shader_module 这条链路可用（V5/V6 再接入管线与绘制）。
layout(location = 0) out vec3 v_color;

void main()
{
    const vec2 positions[3] = vec2[3](
        vec2(0.0, 0.7),
        vec2(-0.6, -0.4),
        vec2(0.6, -0.4));
    const vec3 colors[3] = vec3[3](
        vec3(1.0, 0.2, 0.2),
        vec3(0.2, 1.0, 0.2),
        vec3(0.2, 0.3, 1.0));

    // Vulkan 的 NDC y 轴向下，GL 向上；引擎矩阵仍按 GL 约定生成，这里显式翻转。
    gl_Position = vec4(positions[gl_VertexIndex], 0.0, 1.0);
    gl_Position.y = -gl_Position.y;
    v_color = colors[gl_VertexIndex];
}
