#version 450

// Position of this pixel in normalized device coordinates.
layout(location = 0) in vec2 fragmentCoord;

layout(location = 0) out vec4 outColor;

// Specialization constant discovered by SPIRV-Reflect. main.cpp overrides it
// when creating the pipeline, which caps the work done per pixel.
layout(constant_id = 0) const int ITERATION_LIMIT = 24;

// Push constant block; matches FractalSettings in main.cpp.
layout(push_constant) uniform FractalSettings {
    int recursion_depth;
    float zoom;
    float aspect_ratio;
} fractal;

void main() {
    int recursionDepth = min(fractal.recursion_depth, ITERATION_LIMIT);

    // Map NDC screen space [-1, 1] to fractal coordinate space [0, 1].
    vec2 centeredCoord = (fragmentCoord + 1.0) * 0.5 - vec2(0.5);
    centeredCoord.x *= fractal.aspect_ratio;

    vec2 point = centeredCoord / fractal.zoom + vec2(0.5);
    bool insideFractal = true;

    // Iterated function system: each iteration folds the point into one of
    // the three sub-triangles.
    for (int iteration = 0; iteration < recursionDepth; iteration++) {
        point *= 2.0;

        // Mutually exclusive quadrant checks.
        if (point.x > 1.0 && point.y > 1.0) {
            insideFractal = false; // Central inverted triangle hole.
        } else if (point.x > 1.0) {
            point.x -= 1.0; // Bottom-right sub-triangle.
        } else if (point.y > 1.0) {
            point.y -= 1.0; // Top-left sub-triangle.
        }
    }

    // Pixels outside the fractal or inside a hole become background.
    if (!insideFractal || point.x < 0.0 || point.y < 0.0 || (point.x + point.y > 1.0)) {
        outColor = vec4(0.0, 0.0, 0.0, 0.0); // Background / hole.
    } else {
        outColor = vec4(1.0, 1.0, 1.0, 1.0); // Fractal shape.
    }
}
