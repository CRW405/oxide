#version 450

layout(push_constant) uniform PushConstants {
    float time;
} push;

layout(location = 0) in vec2 in_position;
layout(location = 1) in vec3 in_color;

layout(location = 0) out vec3 frag_color;
layout(location = 1) out vec2 frag_local;

void main() {
    // Slow rotation makes the antialiased polygon edges visible.
    float c = cos(push.time * 0.55);
    float s = sin(push.time * 0.55);
    vec2 rotated = vec2(c * in_position.x - s * in_position.y,
                        s * in_position.x + c * in_position.y);

    gl_Position = vec4(rotated, 0.0, 1.0);
    frag_color = in_color;
    frag_local = in_position;
}
