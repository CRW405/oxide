#version 450

// Set 0 is the resolved scene color; set 1 is the resolved glow mask.
layout(set = 0, binding = 0) uniform sampler2D scene_texture;
layout(set = 1, binding = 0) uniform sampler2D glow_texture;

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 out_color;

void main() {
    vec3 scene = texture(scene_texture, uv).rgb;
    float glow = texture(glow_texture, uv).r;

    vec3 background = mix(vec3(0.015, 0.020, 0.045),
                          vec3(0.055, 0.085, 0.160), uv.y);
    vec2 centered = uv * 2.0 - 1.0;
    float vignette = smoothstep(1.5, 0.35, length(centered));

    vec3 color = background + scene + vec3(0.25, 0.55, 1.0) * glow * 0.85;
    out_color = vec4(color * vignette, 1.0);
}
