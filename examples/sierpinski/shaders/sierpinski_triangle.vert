#version 450

// Fragment position in normalized device coordinates.
layout(location = 0) out vec2 fragmentCoord;

void main() {
    // Oversized triangle that covers the whole screen. The fragment shader
    // evaluates the Sierpinski fractal for every pixel.
    const vec2 screenTriangle[3] = vec2[](
        vec2(-1.0, -1.0),
        vec2( 3.0, -1.0),
        vec2(-1.0,  3.0)
    );
    gl_Position = vec4(screenTriangle[gl_VertexIndex], 0.0, 1.0);
    fragmentCoord = screenTriangle[gl_VertexIndex];
}
