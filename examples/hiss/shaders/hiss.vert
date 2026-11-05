#version 450

// Per-vertex attributes from CubeVertex in main.cpp.
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;

// Values interpolated across the cube faces and consumed by hiss.frag.
layout(location = 0) out vec3 fragmentColor;
layout(location = 1) out vec3 fragmentPosition;
layout(location = 2) out vec3 fragmentNormal;

// Per-frame camera matrices supplied by camera_uniform_buffer in main.cpp.
layout(binding = 0) uniform CameraUniforms {
	mat4 model;
	mat4 view;
	mat4 projection;
} camera;

void main() {
	fragmentColor = inColor;

	// Transform into view space once and reuse the result for position and
	// normal.
	vec4 viewPosition = camera.view * camera.model * vec4(inPosition, 1.0);
	fragmentPosition  = viewPosition.xyz;

	// The cube is centered on the origin, so the local position doubles as an
	// approximate face normal. Rotate it into view space.
	fragmentNormal = mat3(camera.view * camera.model) * inPosition;

	gl_Position = camera.projection * viewPosition;
}
