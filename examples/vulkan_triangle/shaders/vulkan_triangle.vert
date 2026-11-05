#version 450

// Vertex inputs supplied by TriangleVertex in main.cpp.
layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec3 inColor;

// Interpolated color passed to the fragment shader.
layout(location = 0) out vec3 fragmentColor;

void main() {
    // Normalized device coordinates; z = 0 keeps the triangle inside the
    // depth range.
    gl_Position = vec4(inPosition, 0.0, 1.0);
    fragmentColor = inColor;
}
