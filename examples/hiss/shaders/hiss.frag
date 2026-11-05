#version 450

layout(location = 0) in vec3 fragmentColor;
layout(location = 1) in vec3 fragmentPosition;
layout(location = 2) in vec3 fragmentNormal;

layout(location = 0) out vec4 outColor;

// Animation time in seconds, supplied as a push constant from main.cpp.
layout(push_constant) uniform HissPushConstants {
	float time;
}
pushConstants;

// Cheap hash-based pseudo-random value in [0, 1].
float random(vec2 coordinates) {
	return fract(sin(dot(coordinates, vec2(12.9898, 78.233))) * 43758.5453123);
}

// Bilinear value noise: hash the four corners of a grid cell and blend them
// with smoothstep-style weights.
float noise(vec2 coordinates) {
	vec2 cell  = floor(coordinates);
	vec2 local = fract(coordinates);

	float bottomLeft  = random(cell);
	float bottomRight = random(cell + vec2(1.0, 0.0));
	float topLeft     = random(cell + vec2(0.0, 1.0));
	float topRight    = random(cell + vec2(1.0, 1.0));

	vec2 weights = local * local * (3.0 - 2.0 * local);

	return mix(bottomLeft, bottomRight, weights.x) +
	       (topLeft - bottomLeft) * weights.y * (1.0 - weights.x) +
	       (topRight - bottomRight) * weights.x * weights.y;
}

// Fractal Brownian motion: sum several octaves of noise for richer detail.
float fbm(vec2 coordinates) {
	float value     = 0.0;
	float amplitude = 0.5;

	for (int octave = 0; octave < 5; octave++) {
		value += amplitude * noise(coordinates);
		coordinates *= 2.0;
		amplitude *= 0.5;
	}
	return value;
}

void main() {
	vec3 surfaceNormal = normalize(fragmentNormal);

	// Combine object-space position and normal so the pattern wraps around the
	// cube instead of looking like a flat projection.
	vec2 noiseCoordinates =
	    (fragmentPosition.xy * 0.8) + (surfaceNormal.xy * 0.4);

	float animationTime = pushConstants.time * 1.5;

	// Two layers of domain warping give the texture its swirling, liquid look.
	vec2 warpA = vec2(
	    fbm(noiseCoordinates + animationTime * 0.15),
	    fbm(noiseCoordinates + vec2(1.0, 1.0) - animationTime * 0.1));
	vec2 warpB = vec2(
	    fbm(noiseCoordinates + 4.0 * warpA + animationTime * 0.1),
	    fbm(noiseCoordinates + 4.0 * warpA + vec2(1.8, 2.5) -
	        animationTime * 0.15));
	float smokeDensity = fbm(noiseCoordinates + 2.5 * warpB);

	vec3 black = vec3(0.0, 0.0, 0.0);
	vec3 grey  = vec3(0.1, 0.1, 0.1);
	vec3 red   = vec3(1.0, 0.0, 0.0);

	// Black smoke on grey, with red highlights in the densest regions.
	vec3  smokeColor = mix(grey, black, clamp(smokeDensity * 1.5, 0.0, 1.0));
	float redGlow    = smoothstep(0.35, 0.8, smokeDensity);
	smokeColor       = mix(smokeColor, red, redGlow);

	outColor = vec4(smokeColor, 1.0);
}
