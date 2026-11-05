#version 450

// Vertex inputs supplied by AnimatedVertex in main.cpp.
layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec3 inColor;

// Interpolated color passed to the fragment shader.
layout(location = 0) out vec3 fragmentColor;

// Per-frame transform values; matches AnimationUniforms in main.cpp.
layout(set = 0, binding = 0) uniform AnimationUniforms {
    vec2 offset;
    float scale;
} animation;

void main() {
    // Scale around the origin, then translate.
    vec2 transformedPosition = inPosition * animation.scale + animation.offset;
    gl_Position = vec4(transformedPosition, 0.0, 1.0);
    fragmentColor = inColor;
}
