#version 450

layout(location=0) in vec3 inPos;
layout(location=1) in vec3 inColor;

layout(location=0) out vec3 fragColor;
layout(location=1) out vec3 fragPos;

layout(binding=0) uniform CameraUBO {
    mat4 model;
    mat4 view;
    mat4 projection;
} camera;

void main() {
    fragColor = inColor;

    vec4 worldPos = camera.model * vec4(inPos, 1.0) + vec4(0.0, 0.0, 0.0, 0.5);
    fragPos = worldPos.xyz;

    gl_Position = camera.projection * camera.view * worldPos;
}
