#version 330 core
precision highp float;

out vec4 FragColor;

in vec3 v_uv;

uniform samplerCube u_skybox_texture;

void main()
{
    FragColor = texture(u_skybox_texture, v_uv);
}