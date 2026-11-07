#version 450

layout(push_constant) uniform PushConstants {
    float time;
} push;

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 out_color;

void main() {
    vec2 point = uv * 2.0 - 1.0;
    point.x *= 1.5;

    float radius = length(point);
    float angle = atan(point.y, point.x);
    float bands = 0.5 + 0.5 * sin(10.0 * radius - 4.0 * push.time + 3.0 * angle);

    vec3 color = mix(vec3(0.05, 0.10, 0.35), vec3(1.0, 0.45, 0.10), bands);
    color *= smoothstep(1.0, 0.2, radius);
    out_color = vec4(color, 1.0);
}
