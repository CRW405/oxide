#version 450

// Vertex inputs: a position in normalized device coordinates and a texture
// coordinate in [0, 1].
layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec2 inUv;

// Interpolated texture coordinate passed to the fragment shader.
layout(location = 0) out vec2 fragmentUv;

void main() {
    gl_Position = vec4(inPosition, 0.0, 1.0);
    fragmentUv = inUv;
}
