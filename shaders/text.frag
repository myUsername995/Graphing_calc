#version 430 core

in vec2 v_uv;
out vec4 FragColor;

uniform sampler2D u_tex;
uniform vec4 u_color;

void main() {
    float alpha = texture(u_tex, v_uv).a; // use red channel for glyph alpha
    FragColor = vec4(u_color.rgb, u_color.a * alpha);
}