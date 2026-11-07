#version 450

layout(binding = 0) uniform sampler2D scene_texture;

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 out_color;

void main() {
    // Both passes use the same Vulkan full-screen UV convention, so the
    // offscreen image is sampled directly without a vertical flip.
    vec3 color = texture(scene_texture, uv).rgb;

    vec2 centered = uv * 2.0 - 1.0;
    float vignette = smoothstep(1.35, 0.45, length(centered));
    out_color = vec4(color * vignette, 1.0);
}
