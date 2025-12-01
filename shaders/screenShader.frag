#version 430 core

in vec2 v_uv;
out vec4 FragColor;

uniform sampler2D u_tex;   // texture to render
uniform vec4 u_color;      // optional tint (default 1,1,1,1)

void main() {
    vec4 texColor = texture(u_tex, v_uv);
    FragColor = texColor * u_color;
}
