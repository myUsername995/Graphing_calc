#version 430

layout(location = 0) in vec2 a_pos;   // full-screen triangle/quad positions
layout(location = 1) in vec2 a_uv;    // texture coordinates

out vec2 v_uv;

void main() {
    v_uv = a_uv;
    gl_Position = vec4(a_pos, 0.0, 1.0);
}