#include <oxide/oxide.hpp>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <vector>

#include <cglm/cglm.h>

// Camera matrices shared by every vertex through a per-frame uniform buffer.
struct CameraUniforms {
	mat4 model;
	mat4 view;
	mat4 projection;
};

// Cube vertex: location 0 is a position and location 1 is an RGB color.
struct CubeVertex {
	vec3 position;
	vec3 color;
};

// Matches the push constant block declared in hiss.frag.
struct HissPushConstants {
	float time;
};

int main() {
	// Library setup.
	oxide::Application    app(800, 600, "Control Hiss Shader");
	oxide::Window        &window = app.window();
	oxide::VulkanDevice  &device = app.device();
	oxide::VulkanContext &vulkan_context = app.context();

	// Per-frame camera uniform buffer. The ring keeps one copy per frame in
	// flight so updates cannot race work the GPU is still using.
	CameraUniforms camera_uniforms;
	glm_mat4_identity(camera_uniforms.model);
	glm_mat4_identity(camera_uniforms.view);
	glm_mat4_identity(camera_uniforms.projection);
	oxide::UniformBufferRing camera_uniform_buffer(
	    device, oxide::MAX_FRAMES_IN_FLIGHT, camera_uniforms);

	// Describe the CubeVertex layout to the pipeline.
	oxide::VertexInputDescription vertex_input_description;
	vertex_input_description.addBinding(0, sizeof(CubeVertex))
	    .addAttribute(0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(CubeVertex, position))
	    .addAttribute(1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(CubeVertex, color));

	// Cube corners. The index buffer below assembles them into six faces.
	std::vector<CubeVertex> cube_vertices = {
		{ { 1.0f, 1.0f, 1.0f },    { 1.0f, 0.0f, 0.0f } },
		{ { -1.0f, 1.0f, 1.0f },   { 1.0f, 0.0f, 0.0f } },
		{ { -1.0f, -1.0f, 1.0f },  { 1.0f, 0.0f, 0.0f } },
		{ { 1.0f, -1.0f, 1.0f },   { 1.0f, 0.0f, 0.0f } },
		{ { 1.0f, 1.0f, -1.0f },   { 1.0f, 0.0f, 0.0f } },
		{ { -1.0f, 1.0f, -1.0f },  { 1.0f, 0.0f, 0.0f } },
		{ { -1.0f, -1.0f, -1.0f }, { 1.0f, 0.0f, 0.0f } },
		{ { 1.0f, -1.0f, -1.0f },  { 1.0f, 0.0f, 0.0f } }
	};
	// Two triangles per face, wound counter-clockwise.
	std::vector<std::uint16_t> cube_indices = {
		0, 1, 2, 2, 3, 0, // front
		4, 6, 5, 6, 4, 7, // back
		4, 5, 1, 1, 0, 4, // top
		7, 2, 6, 2, 7, 3, // bottom
		4, 0, 3, 3, 7, 4, // right
		5, 2, 1, 2, 5, 6  // left
	};
	oxide::Mesh cube_mesh(device, cube_vertices, cube_indices);

	// The fragment shader receives the animation time as a push constant.
	oxide::PushConstant time_push_constant =
	    oxide::pushConstant<HissPushConstants>(VK_SHADER_STAGE_FRAGMENT_BIT);

	// Pipeline setup: depth testing and back-face culling for the rotating cube.
	oxide::PipelineConfigInfo pipeline_config =
	    oxide::Pipeline::defaultPipelineConfigInfo();
	pipeline_config.depthStencilInfo.depthTestEnable  = VK_TRUE;
	pipeline_config.depthStencilInfo.depthWriteEnable = VK_TRUE;
	pipeline_config.rasterizationInfo.cullMode        = VK_CULL_MODE_BACK_BIT;
	pipeline_config.rasterizationInfo.frontFace       = VK_FRONT_FACE_COUNTER_CLOCKWISE;

	oxide::PipelineLayout pipeline_layout(
	    device, { camera_uniform_buffer.layout() }, { time_push_constant.range() });
	pipeline_config.setVertexInput(vertex_input_description);
	pipeline_config.setPipelineLayout(
	    oxide::BorrowedPipelineLayout(pipeline_layout.handle()));

	oxide::ShaderLibrary     shader_library(OXIDE_SHADER_DIR);
	oxide::SwapchainPipeline hiss_pipeline(
	    vulkan_context, device, shader_library.graphics("hiss"), pipeline_config);
	oxide::DeviceIdleGuard device_idle_guard(device);

	while (!window.shouldClose()) {
		window.pollEvents();

		if (window.closeRequested())
			break;

		oxide::FrameScope frame_scope =
		    vulkan_context.beginFrameScope({ 0.0f, 0.0f, 0.0f, 0.0f });
		if (!frame_scope) {
			continue;
		}

		// Animate the cube's rotation. Elapsed time is scaled up unless Shift
		// was just released, which makes the liquid motion easier to inspect.
		double elapsed_seconds = window.time();
		if (!window.keyReleased(GLFW_KEY_LEFT_SHIFT)) {
			elapsed_seconds = elapsed_seconds * 5.0;
		}

		glm_mat4_identity(camera_uniforms.model);
		vec3 rotation_axis = { 0.7f, 1.0f, 0.2f };
		glm_rotate(
		    camera_uniforms.model, static_cast<float>(elapsed_seconds * 0.2),
		    rotation_axis);

		// Fixed camera looking at the origin.
		vec3 camera_eye    = { 0.0f, 0.0f, 5.0f };
		vec3 camera_target = { 0.0f, 0.0f, 0.0f };
		vec3 camera_up     = { 0.0f, 1.0f, 0.0f };
		glm_lookat(camera_eye, camera_target, camera_up, camera_uniforms.view);

		// Perspective projection matched to the current swapchain aspect ratio.
		float aspect_ratio = static_cast<float>(vulkan_context.extent().width) /
		                     static_cast<float>(vulkan_context.extent().height);
		glm_perspective(
		    glm_rad(45.0f), aspect_ratio, 0.1f, 100.0f, camera_uniforms.projection);
		camera_uniforms.projection[1][1] *= -1.0f; // Vulkan's Y axis points down.

		camera_uniform_buffer.update(frame_scope.frameIndex(), camera_uniforms);

		HissPushConstants push_constants_data{
			static_cast<float>(elapsed_seconds / 5)
		};
		pipeline_layout.pushConstant(
		    frame_scope.commandBuffer(), time_push_constant, push_constants_data);

		cube_mesh.bind(frame_scope.commandBuffer());
		camera_uniform_buffer.bind(
		    frame_scope.commandBuffer(), pipeline_layout.handle(),
		    frame_scope.frameIndex(), 0);
		hiss_pipeline.bind(frame_scope.commandBuffer());
		vulkan_context.setViewportAndScissor(frame_scope.commandBuffer());
		cube_mesh.draw(frame_scope.commandBuffer());
	}
}
