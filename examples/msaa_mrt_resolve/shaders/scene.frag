#version 450

layout(push_constant) uniform PushConstants {
    float time;
} push;

layout(location = 0) in vec3 frag_color;
layout(location = 1) in vec2 frag_local;

// Multiple render targets: location 0 is the scene color, location 1 is a
// glow mask consumed by the composite pass.
layout(location = 0) out vec4 out_color;
layout(location = 1) out vec4 out_glow;

void main() {
    float radius = length(frag_local);
    vec3 color = frag_color * (0.55 + 0.45 * radius);

    float pulse = 0.65 + 0.35 * sin(push.time * 2.0);
    float edge = smoothstep(0.45, 1.05, radius) * pulse;

    out_color = vec4(color, 1.0);
    out_glow = vec4(vec3(edge), 1.0);
}
