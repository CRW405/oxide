#version 450

void main() {
    // Small triangle in the lower-left corner, generated from gl_VertexIndex
    // so the overlay needs no vertex buffer.
    const vec2 overlayTriangle[3] = vec2[](
        vec2(-0.9, -0.85),
        vec2(-0.35, -0.85),
        vec2(-0.9, -0.55)
    );
    gl_Position = vec4(overlayTriangle[gl_VertexIndex], 0.0, 1.0);
}
