#include <oxide/glfw/glfw_oxide.hpp>
#include <oxide/tools/fps.hpp>
#include <oxide/tools/log.hpp>
#include <oxide/vulkan/pipeline_layout.hpp>
#include <oxide/vulkan/render_pipeline.hpp>
#include <oxide/vulkan/vulkan_context.hpp>
#include <oxide/vulkan/vulkan_device.hpp>

#include <cmath>
#include <cstdio>
#include <exception>

struct Camera {
	mat4 model;
	mat4 view;
	mat4 projection;
};

struct Vertex {
	vec3 position;
	vec3 color;
};

int main() {
	// lib setup
	oxide::GLFWContext     glfw;
	oxide::Window          window({ 500, 500, "Control Hiss Shader", true });
	oxide::VulkanDevice    device(window.handle(), "Control Hiss Shader");
	oxide::VulkanContext   vulkan(device, &window);
	oxide::DeviceIdleGuard idleGuard(device);

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
	vec3                white = { 1.0f, 1.0f, 1.0f };
	std::vector<Vertex> cube  = {
		{ { 1.0f, 1.0f, 1.0f },    white },
		{ { -1.0f, 1.0f, 1.0f },   white },
		{ { -1.0f, -1.0f, 1.0f },  white },
		{ { 1.0f, -1.0f, 1.0f },   white },
		{ { 1.0f, 1.0f, -1.0f },   white },
		{ { -1.0f, 1.0f, -1.0f },  white },
		{ { -1.0f, -1.0f, -1.0f }, white },
		{ { 1.0f, -1.0f, -1.0f },  white }
	};
	std::vector<uint16_t> cubeIndices = {
		0, 1, 2, 2, 3, 0, // front
		4, 5, 6, 6, 7, 4, // back
		4, 5, 1, 1, 0, 4, // top
		7, 6, 2, 2, 3, 7, // bottom
		4, 0, 3, 3, 7, 4, // right
		5, 1, 2, 2, 6, 5  // left
	};
	oxide::Mesh cubeMesh(device, cube, cubeIndices);

	// pipeline setup
	oxide::PipelineConfigInfo pipelineConfig         = oxide::Pipeline::defaultPipelineConfigInfo();
	pipelineConfig.depthStencilInfo.depthTestEnable  = VK_TRUE;
	pipelineConfig.depthStencilInfo.depthWriteEnable = VK_TRUE;
	pipelineConfig.rasterizationInfo.cullMode        = VK_CULL_MODE_BACK_BIT;
	oxide::PipelineLayout pipelineLayout(device, { cameraBuffer.layout() });
	pipelineConfig.setVertexInput(vertexInput);
	pipelineConfig.pipelineLayout = pipelineLayout.handle();
	pipelineConfig.renderPass     = vulkan.renderPass();
	oxide::Pipeline pipeline(device,
	                         OXIDE_SHADER_DIR "/hiss.vert.spv",
	                         OXIDE_SHADER_DIR "/hiss.frag.spv",
	                         pipelineConfig);

	double dTime = 0.0;

	while (!window.shouldClose()) {
		window.pollEvents();

		if (window.isKeyPressed(GLFW_KEY_ESCAPE)) {
			glfwSetWindowShouldClose(window.handle(), GLFW_TRUE);
		}

		oxide::Frame frame = vulkan.beginFrame();
		if (frame.commandBuffer == VK_NULL_HANDLE) {
			continue;
		}

		double currentTime = window.time();
		dTime              = currentTime - dTime;
		glm_mat4_identity(camera.model);
		vec3 rotationAxis = { 1.0f, 1.0f, 0.0f };
		glm_rotate(camera.model, static_cast<float>(currentTime), rotationAxis);
		vec3 eye    = { 0.0f, 0.0f, 3.0f };
		vec3 center = { 0.0f, 0.0f, 0.0f };
		vec3 up     = { 0.0f, 1.0f, 0.0f };
		glm_lookat(eye, center, up, camera.view);
		floar aspect_ratio = static_cast<float>(vulkan.extent().width) / static_cast<float>(vulkan.extent().height);
		glm_perspective(glm_rad(45.0f), aspect_ratio, 0.1f, 100.0f, camera.projection);
		camera.projection[1][1] *= -1.0f;
		cameraBuffer.update(frame, camera);

		vulkan.beginRenderPass(frame);
		cubeMesh.bind(frame.commandBuffer);
		cameraBuffer.bind(frame.commandBuffer, pipelineLayout.handle(), frame.frameIndex, 0);
		pipeline.bind(frame.commandBuffer);
		vulkan.setViewportAndScissor(frame.commandBuffer);
		cubeMesh.draw(frame.commandBuffer);
		vulkan.endRenderPass(frame);
		vulkan.endFrame(frame);
	}
}
