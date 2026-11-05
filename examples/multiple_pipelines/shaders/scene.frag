#version 450

// Color interpolated across the scene triangle.
layout(location = 0) in vec3 fragmentColor;

layout(location = 0) out vec4 outColor;

void main() {
    outColor = vec4(fragmentColor, 1.0);
}
