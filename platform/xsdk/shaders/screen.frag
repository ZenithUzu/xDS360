#version 450
layout(binding = 0) uniform sampler2D image;
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 color;
// getFrame writes 0xAABBGGRR into an A8R8G8B8 texture.
void main() { color = texture(image, uv).bgra; }
