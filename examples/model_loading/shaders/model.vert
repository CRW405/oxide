#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 0) out vec3 fragmentNormal;

layout(binding = 0) uniform CameraUniforms {
	mat4 model;
	mat4 view;
	mat4 projection;
} camera;

void main() {
	vec4 viewPosition = camera.view * camera.model * vec4(inPosition, 1.0);
	fragmentNormal = mat3(camera.view * camera.model) * inNormal;
	gl_Position = camera.projection * viewPosition;
}
