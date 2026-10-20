#version 330 core
precision mediump float;

uniform vec4 u_ambient_color;
uniform float u_ambient_strength;

in vec2 v_uv;
in vec3 v_normal;

out vec4 fragColor;

void main() {
    fragColor = u_ambient_color * u_ambient_strength;
}
