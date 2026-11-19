#version 450

// Vulkan 变体：显式描述符绑定（set 0 = 逐帧相机 UBO），与 GL 版本的隐式 uniform 不同。
// 颜色空间按 G-03 的 Vulkan 约定：sRGB 交换链由硬件编码，shader 不再手工编码。
layout(location = 0) in vec3 a_position;
layout(location = 0) out vec3 v_uv;

layout(set = 0, binding = 0) uniform SkyboxUniforms
{
    mat4 u_view;
    mat4 u_projection;
} uniforms;

void main()
{
    v_uv = a_position;
    mat4 view = mat4(mat3(uniforms.u_view));
    vec4 pos = uniforms.u_projection * view * vec4(a_position, 1.0);
    // pos.xyww 把天空盒压到远平面，深度测试默认是 LEQUAL，和旧 GL 路径一致。
    gl_Position = pos.xyww;
    // Vulkan 的 NDC y 轴向下，GL 向上；引擎矩阵仍按 GL 约定生成，这里显式翻转。
    gl_Position.y = -gl_Position.y;
}
