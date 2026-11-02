#include <oxide/glfw/glfw_oxide.hpp>
#include <oxide/tools/log.hpp>
#include <oxide/vulkan/buffer.hpp>
#include <oxide/vulkan/pipeline_layout.hpp>
#include <oxide/vulkan/render_pipeline.hpp>
#include <oxide/vulkan/uniform_buffer_ring.hpp>
#include <oxide/vulkan/vulkan_context.hpp>
#include <oxide/vulkan/vulkan_device.hpp>

#define CGLM_FORCE_DEPTH_ZERO_TO_ONE
#include <cglm/cglm.h>

#include <cstdint>
#include <cstdio>
#include <exception>
#include <vector>

struct ubo_vertex {
	float position[3];
	float color[3];
};

struct UniformBufferObject {
	mat4 model;
	mat4 view;
	mat4 projection;
};

int main() {
	std::printf("Running Uniform Buffer Example\n");
	try {
		std::string           app_name = "Uniform Buffer Example";
		oxide::GLFWContext   glfw;
		oxide::Window        window({ 800, 600, app_name, true });
		oxide::VulkanDevice  device(window.handle(), app_name);
		oxide::VulkanContext vulkan(device, &window);

		// upload the quad and create the uniform buffer that holds the MVP matrices
		const std::vector<ubo_vertex> vertices = {
			{ { -0.5f, -0.5f, 0.0f }, { 1.0f, 0.0f, 0.0f } },
			{ { 0.5f, -0.5f, 0.0f },  { 0.0f, 1.0f, 0.0f } },
			{ { 0.5f, 0.5f, 0.0f },   { 0.0f, 0.0f, 1.0f } },
			{ { -0.5f, 0.5f, 0.0f },  { 1.0f, 1.0f, 1.0f } }
		};
		const std::vector<std::uint16_t> indices = { 0, 1, 2, 2, 3, 0 };
		oxide::Mesh                     mesh(device, vertices, indices);

		UniformBufferObject ubo{};
		glm_mat4_identity(ubo.model);
		glm_mat4_identity(ubo.view);
		glm_mat4_identity(ubo.projection);

		// One UBO and descriptor set per frame in flight. This prevents the CPU
		// from overwriting data that the previous frame is still reading.
		oxide::UniformBufferRing uniform_buffers(device,
		                                         oxide::MAX_FRAMES_IN_FLIGHT,
		                                         sizeof(UniformBufferObject),
		                                         &ubo);

		// pipeline setup
		oxide::PipelineLayout pipeline_layout(device, { uniform_buffers.layout() }, {});
		oxide::PipelineConfigInfo pipeline_Config = oxide::Pipeline::defaultPipelineConfigInfo();

		// vertex input description
		oxide::VertexInputDescription vertexInput;
		vertexInput.addBinding(0, sizeof(ubo_vertex))
		    .addAttribute(0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(ubo_vertex, position))
		    .addAttribute(1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(ubo_vertex, color));
		pipeline_Config.setVertexInput(vertexInput)
		    .setRenderPass(vulkan.renderPass())
		    .setPipelineLayout(pipeline_layout.handle());

		oxide::Pipeline pipeline(device,
		                          OXIDE_SHADER_DIR "/ubo.vert.spv",
		                          OXIDE_SHADER_DIR "/ubo.frag.spv",
		                          pipeline_Config);

		// Declared after the GPU resources so it is destroyed first, including on
		// exception unwinding from the render loop.
		oxide::DeviceIdleGuard idle_guard(device);

		while (!window.shouldClose()) {
			window.pollEvents();
			if (window.keyPressed(GLFW_KEY_ESCAPE) || window.keyPressed(GLFW_KEY_Q)) {
				glfwSetWindowShouldClose(window.handle(), GLFW_TRUE);
			}

			const oxide::Frame frame = vulkan.beginFrame();
			if (frame.commandBuffer == VK_NULL_HANDLE) {
				continue;
			}

			// update the model, view, and projection matrices for this frame
			const float time = static_cast<float>(window.time());
			glm_mat4_identity(ubo.model);
			vec3 rotationAxis = { 1.0f, 1.0f, 0.0f };
			glm_rotate(ubo.model, time * glm_rad(45.0f), rotationAxis);

			vec3 eye    = { 0.0f, 0.0f, 2.5f };
			vec3 center = { 0.0f, 0.0f, 0.0f };
			vec3 up     = { 0.0f, 1.0f, 0.0f };
			glm_lookat(eye, center, up, ubo.view);

			const float aspect_ratio = static_cast<float>(vulkan.extent().width) /
			                           static_cast<float>(vulkan.extent().height);
			glm_perspective(glm_rad(45.0f), aspect_ratio, 0.1f, 100.0f, ubo.projection);
			ubo.projection[1][1] *= -1.0f;

			uniform_buffers.update(frame.frameIndex, ubo);

			vulkan.beginRenderPass(frame, { 0.0f, 0.0f, 0.0f, 0.0f });

			mesh.bind(frame.commandBuffer);
			uniform_buffers.bind(frame.commandBuffer, pipeline_layout.handle(), frame.frameIndex);

			pipeline.bind(frame.commandBuffer);
			vulkan.setViewportAndScissor(frame.commandBuffer);

			mesh.draw(frame.commandBuffer);

			vulkan.endRenderPass(frame);
			vulkan.endFrame(frame);
		}

	} catch (const std::exception &error) {
		OXIDE_LOG_FATAL << "unhandled exception: " << error.what();
		return 1;
	}
	return 0;
}
