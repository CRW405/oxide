#version 450

layout(location = 0) in vec3 fragmentNormal;
layout(location = 0) out vec4 outColor;

void main() {
	vec3  normal         = normalize(fragmentNormal);
	vec3  lightDirection = normalize(vec3(0.5, 0.8, 1.0));
	float diffuse        = max(dot(normal, lightDirection), 0.0);
	outColor             = vec4(vec3(0.2, 0.55, 0.95) * (0.25 + 0.75 * diffuse), 1.0);
}
