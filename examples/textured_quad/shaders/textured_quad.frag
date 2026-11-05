#version 450

// Texture coordinate interpolated across the quad.
layout(location = 0) in vec2 fragmentUv;

layout(location = 0) out vec4 outColor;

// Combined image sampler bound at set 0, binding 0 by TextureDescriptorSet.
layout(set = 0, binding = 0) uniform sampler2D quadTexture;

void main() {
    // Flip V so the image's top row maps to the top of the quad.
    vec2 flippedUv = vec2(fragmentUv.x, 1.0 - fragmentUv.y);
    outColor = texture(quadTexture, flippedUv);
}
