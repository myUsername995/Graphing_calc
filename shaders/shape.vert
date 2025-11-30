#version 430 core
layout(location = 0) in vec2 a_pos;

uniform vec4 u_color;

out vec4 v_color;

void main() {
    v_color = u_color;
    gl_Position = vec4(a_pos, 0.0, 1.0);
}
