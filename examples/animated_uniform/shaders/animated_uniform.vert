#version 450

layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 0) out vec3 color;

layout(set = 0, binding = 0) uniform Uniforms {
    vec2 offset;
    float scale;
} uniforms;

void main() {
    gl_Position = vec4(inPosition * uniforms.scale + uniforms.offset, 0.0, 1.0);
    color = inColor;
}
