#version 330 core
layout(location = 0) in vec3 a_position;

out vec3 v_uv;
uniform mat4 u_view;
uniform mat4 u_projection;

void main() {
    v_uv = a_position;
    gl_Position = u_projection * u_view * vec4(a_position, 1.0);
}
