#version 450

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 outColor;
layout(set = 0, binding = 0) uniform sampler2D textureSampler;

void main() {
    outColor = texture(textureSampler, vec2(uv.x, 1.0 - uv.y));
}
