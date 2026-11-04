#include <oxide/glfw/glfw_oxide.hpp>
#include <oxide/tools/fps.hpp>
#include <oxide/tools/log.hpp>
#include <oxide/vulkan/buffer.hpp>
#include <oxide/vulkan/pipeline_layout.hpp>
#include <oxide/vulkan/render_pipeline.hpp>
#include <oxide/vulkan/uniform_buffer_ring.hpp>
#include <oxide/vulkan/vulkan_context.hpp>
#include <oxide/vulkan/vulkan_device.hpp>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <vector>

#include <cglm/cglm.h>

struct Camera {
	mat4 model;
	mat4 view;
	mat4 projection;
};

struct Vertex {
	vec3 position;
	vec3 color;
};

struct pushConstants {
	float time;
};

int main() {
	// lib setup
	oxide::GLFWContext   glfw;
	oxide::Window        window({ 800, 600, "Control Hiss Shader", true });
	oxide::VulkanDevice  device(window.handle(), "Control Hiss Shader");
	oxide::VulkanContext vulkan(device, &window);

	// cam ubo setup
	Camera camera;
	glm_mat4_identity(camera.model);
	glm_mat4_identity(camera.view);
	glm_mat4_identity(camera.projection);
	oxide::UniformBufferRing cameraBuffer(device, oxide::MAX_FRAMES_IN_FLIGHT, sizeof(Camera), &camera);

	// vertex setup
	oxide::VertexInputDescription vertexInput;
	vertexInput.addBinding(0, sizeof(Vertex))
	    .addAttribute(0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, position))
	    .addAttribute(1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, color));

	// cube mesh setup
	std::vector<Vertex> cube = {
		{ { 1.0f, 1.0f, 1.0f },    { 1.0f, 0.0f, 0.0f } },
		{ { -1.0f, 1.0f, 1.0f },   { 1.0f, 0.0f, 0.0f } },
		{ { -1.0f, -1.0f, 1.0f },  { 1.0f, 0.0f, 0.0f } },
		{ { 1.0f, -1.0f, 1.0f },   { 1.0f, 0.0f, 0.0f } },
		{ { 1.0f, 1.0f, -1.0f },   { 1.0f, 0.0f, 0.0f } },
		{ { -1.0f, 1.0f, -1.0f },  { 1.0f, 0.0f, 0.0f } },
		{ { -1.0f, -1.0f, -1.0f }, { 1.0f, 0.0f, 0.0f } },
		{ { 1.0f, -1.0f, -1.0f },  { 1.0f, 0.0f, 0.0f } }
	};
	std::vector<uint16_t> cubeIndices = {
		0, 1, 2, 2, 3, 0, // front
		4, 6, 5, 6, 4, 7, // back
		4, 5, 1, 1, 0, 4, // top
		7, 2, 6, 2, 7, 3, // bottom
		4, 0, 3, 3, 7, 4, // right
		5, 2, 1, 2, 5, 6  // left
	};
	oxide::Mesh cubeMesh(device, cube, cubeIndices);

	// push constants setup
	oxide::PushConstant pushconstant = oxide::pushConstant<pushConstants>(VK_SHADER_STAGE_FRAGMENT_BIT);

	// pipeline setup
	oxide::PipelineConfigInfo pipelineConfig         = oxide::Pipeline::defaultPipelineConfigInfo();
	pipelineConfig.depthStencilInfo.depthTestEnable  = VK_TRUE;
	pipelineConfig.depthStencilInfo.depthWriteEnable = VK_TRUE;
	pipelineConfig.rasterizationInfo.cullMode        = VK_CULL_MODE_BACK_BIT;
	pipelineConfig.rasterizationInfo.frontFace       = VK_FRONT_FACE_COUNTER_CLOCKWISE;
	oxide::PipelineLayout pipelineLayout(device, { cameraBuffer.layout() }, { pushconstant.range() });
	pipelineConfig.setVertexInput(vertexInput);
	pipelineConfig.pipelineLayout = pipelineLayout.handle();
	pipelineConfig.renderPass     = vulkan.renderPass();
	oxide::Pipeline        pipeline(device,
	                                OXIDE_SHADER_DIR "/hiss.vert.spv",
	                                OXIDE_SHADER_DIR "/hiss.frag.spv",
	                                pipelineConfig);
	oxide::DeviceIdleGuard idleGuard(device);

	while (!window.shouldClose()) {
		window.pollEvents();

		if (window.keyPressed(GLFW_KEY_ESCAPE) || window.keyPressed(GLFW_KEY_Q)) {
			glfwSetWindowShouldClose(window.handle(), GLFW_TRUE);
		}

		oxide::Frame frame = vulkan.beginFrame();
		if (frame.commandBuffer == VK_NULL_HANDLE) {
			continue;
		}

		double currentTime = window.time();
		if (!window.keyReleased(GLFW_KEY_LEFT_SHIFT)) {
			currentTime = currentTime * 5.0;
		} else {
		}

		glm_mat4_identity(camera.model);
		vec3 rotationAxis = { 0.7f, 1.0f, 0.2f };
		glm_rotate(camera.model, static_cast<float>(currentTime * 0.2), rotationAxis);

		vec3 eye    = { 0.0f, 0.0f, 3.5f };
		vec3 center = { 0.0f, 0.0f, 0.0f };
		vec3 up     = { 0.0f, 1.0f, 0.0f };
		glm_lookat(eye, center, up, camera.view);

		float aspect_ratio = static_cast<float>(vulkan.extent().width) / static_cast<float>(vulkan.extent().height);
		glm_perspective(glm_rad(45.0f), aspect_ratio, 0.1f, 100.0f, camera.projection);
		camera.projection[1][1] *= -1.0f; // Vulkan Y-flip

		cameraBuffer.update(frame.frameIndex, camera);

		pushConstants pc{ static_cast<float>(currentTime / 5) };
		pipelineLayout.pushConstant(frame.commandBuffer, pushconstant, pc);

		vulkan.beginRenderPass(frame, { 0.0f, 0.0f, 0.0f, 0.0f });
		cubeMesh.bind(frame.commandBuffer);
		cameraBuffer.bind(frame.commandBuffer, pipelineLayout.handle(), frame.frameIndex, 0);
		pipeline.bind(frame.commandBuffer);
		vulkan.setViewportAndScissor(frame.commandBuffer);
		cubeMesh.draw(frame.commandBuffer);
		vulkan.endRenderPass(frame);
		vulkan.endFrame(frame);
	}
}
