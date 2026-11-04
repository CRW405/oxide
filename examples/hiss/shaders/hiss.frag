#version 450

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec3 fragPos;

layout(location = 0) out vec4 outColor;

layout(push_constant) uniform PushConstants {
    float time;
} pc;

float random(vec2 st) {
    return fract(sin(dot(st.xy, vec2(12.9898, 78.233))) * 43758.5453123);
}

float noise(vec2 st) {
    vec2 i = floor(st);
    vec2 f = fract(st);

    float a = random(i);
    float b = random(i + vec2(1.0, 0.0));
    float c = random(i + vec2(0.0, 1.0));
    float d = random(i + vec2(1.0, 1.0));

    vec2 u = f * f * (3.0 - 2.0 * f);

    return mix(a, b, u.x) +
            (c - a) * u.y * (1.0 - u.x) +
            (d - b) * u.x * u.y;
}

// Fractal Brownian Motion for more interesting noise
float fbm(vec2 st) {
    float value = 0.0;
    float amplitude = 0.5;

    for (int i = 0; i < 5; i++) {
        value += amplitude * noise(st);
        st *= 2.0;
        amplitude *= 0.5;
    }
    return value;
}

void main() {
    vec2 st = fragPos.xy * 1.2 + fragPos.zz * 0.5;

    float time = pc.time * 1.5;

    vec2 q = vec2(fbm(st + time * 0.15), fbm(st + vec2(1.0, 1.0) - time * 0.1));
    vec2 r = vec2(fbm(st + 4.0 * q + time * 0.1), fbm(st + 4.0 * q + vec2(1.8, 2.5) - time * 0.15));
    float smokeNoise = fbm(st + 2.5 * r);

    vec3 black = vec3(0.0, 0.0, 0.0);
    vec3 grey = vec3(0.1, 0.1, 0.1);
    vec3 red = vec3(1.0, 0.0, 0.0);

    vec3 color = mix(black, grey, clamp(smokeNoise * 1.5, 0.0, 1.0));

    float redIntensity = smoothstep(0.35, 0.8, smokeNoise);
    color = mix(color, red, redIntensity);

    outColor = vec4(color, 1.0);
}
