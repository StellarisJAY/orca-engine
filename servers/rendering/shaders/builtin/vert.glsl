#version 330 core
layout(location = 0) in vec3 a_position;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec2 a_uv;
// 在加载顶点文件时计算好切线和副切线
layout(location = 3) in vec3 a_tangent;
layout(location = 4) in vec3 a_bitangent;

out vec2 v_uv;
out vec3 v_normal;
out vec3 v_world_position;
// 切线空间 -> 世界空间，frag 中直接左乘切线空间法线。
out mat3 v_TBN;

uniform mat4 u_model;
uniform mat4 u_view;
uniform mat4 u_projection;

void main() {
    // mvp 顶点变换
    vec4 surface_world_position = u_model * vec4(a_position, 1.0);
    v_world_position = vec3(surface_world_position);
    gl_Position = u_projection * u_view * surface_world_position;
    v_uv = a_uv;

    // 法线必须用逆转置矩阵变换；mat3(u_model) 在非均匀缩放下会拉歪法线。
    mat3 model3 = mat3(u_model);
    mat3 normal_matrix = transpose(inverse(model3));
    if (abs(determinant(model3)) < 1e-8)
        normal_matrix = mat3(1.0);

    vec3 N = normal_matrix * a_normal;
    if (dot(N, N) < 1e-8)
        N = vec3(0.0, 0.0, 1.0);
    else
        N = normalize(N);
    v_normal = N;

    // 切线用模型矩阵变换后对法线做正交化，避免 UV 退化时产生 NaN。
    vec3 T = model3 * a_tangent;
    T -= N * dot(N, T);
    if (dot(T, T) < 1e-8)
    {
        vec3 up = abs(N.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
        T = cross(up, N);
    }
    T = normalize(T);

    // 使用副切线的方向决定手性，但副切线本身由 N × T 重建，保证正交。
    vec3 B = model3 * a_bitangent;
    vec3 NcrossT = cross(N, T);
    float handedness = dot(NcrossT, B) < 0.0 ? -1.0 : 1.0;
    B = NcrossT * handedness;

    v_TBN = mat3(T, B, N);
}
