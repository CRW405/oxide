#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec2 inTexCoord;

layout(location = 0) out vec3 fragColor;
layout(location = 1) out vec2 fragTexCoord;

// Descriptor set 0 belongs to the texture; the uniform buffer is set 1.
layout(set = 1, binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 projection;
} ubo;

// The same push constant block is visible to the vertex and fragment stages.
layout(push_constant) uniform Push {
    vec4 tint;
    float time;
} push;

void main() {
    vec4 position = ubo.model * vec4(inPosition, 1.0f);

    // Add a small wave so the vertex stage visibly consumes the push constant.
    position.z += sin(push.time + inPosition.x * 4.0f) * 0.03f;

    gl_Position = ubo.projection * ubo.view * position;
    fragColor = inColor;
    fragTexCoord = inTexCoord;
}
