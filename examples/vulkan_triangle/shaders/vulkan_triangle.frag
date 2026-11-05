#version 450

// Color interpolated from the three triangle vertices.
layout(location = 0) in vec3 fragmentColor;

layout(location = 0) out vec4 outColor;

void main() {
    outColor = vec4(fragmentColor, 1.0);
}
