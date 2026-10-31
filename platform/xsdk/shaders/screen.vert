#version 450
layout(location = 0) in vec2 a_position;
layout(location = 2) in vec2 a_uv;
layout(location = 0) out vec2 uv;
void main() { gl_Position = vec4(a_position, 0.0, 1.0); uv = a_uv; }
