#include <oxide/glfw/glfw_oxide.hpp>
#include <oxide/tools/log.hpp>
#include <oxide/vulkan/buffer.hpp>
#include <oxide/vulkan/image_texture.hpp>
#include <oxide/vulkan/pipeline_layout.hpp>
#include <oxide/vulkan/render_pipeline.hpp>
#include <oxide/vulkan/texture_descriptor.hpp>
#include <oxide/vulkan/vulkan_context.hpp>
#include <oxide/vulkan/vulkan_device.hpp>

#include <cstdint>
#include <cstdio>
#include <exception>
#include <vector>

struct texture_vertex {
	float position[2];
	float color[3];
	float uv[2]; // a uv is a 2D coordinate that maps a point on the texture to a point on the mesh
};

int main() {
	std::printf("Running Image Texture Example\n");
	try {
		std::string           app_name = "Image Texture Example";
		oxide::GLFWContext   glfw;
		oxide::Window        window({ 800, 600, app_name, true });
		oxide::VulkanDevice  device(window.handle(), app_name);
		oxide::VulkanContext vulkan(device, &window);

		// set up texture and the default sampled-image descriptor set
		oxide::ImageTexture         texture(device, OXIDE_ASSET_DIR "/smiley_0.png");
		oxide::TextureDescriptorSet textureDescriptor(device, texture);

		// pipeline setup
		oxide::PipelineLayout     pipeline_layout(device, { textureDescriptor.layout() }, {});
		oxide::PipelineConfigInfo pipeline_Config = oxide::Pipeline::defaultPipelineConfigInfo();

		// vertex input description
		oxide::VertexInputDescription vertexInput;
		vertexInput.addBinding(0, sizeof(texture_vertex))
		    .addAttribute(0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(texture_vertex, position))
		    .addAttribute(1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(texture_vertex, color))
		    .addAttribute(2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(texture_vertex, uv));
		pipeline_Config.setVertexInput(vertexInput)
		    .setRenderPass(vulkan.renderPass())
		    .setPipelineLayout(pipeline_layout.handle());

		oxide::Pipeline pipeline(device,
		                          OXIDE_SHADER_DIR "/simple_texture.vert.spv",
		                          OXIDE_SHADER_DIR "/simple_texture.frag.spv",
		                          pipeline_Config);

		std::vector<texture_vertex> vertices = {
			{ { -0.5f, -0.5f }, { 1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f } },
			{ { 0.5f, -0.5f },  { 0.0f, 1.0f, 0.0f }, { 1.0f, 1.0f } },
			{ { 0.5f, 0.5f },   { 0.0f, 0.0f, 1.0f }, { 1.0f, 0.0f } },
			{ { -0.5f, 0.5f },  { 1.0f, 1.0f, 1.0f }, { 0.0f, 0.0f } }
		};
		const std::vector<std::uint16_t> indices = { 0, 1, 2, 2, 3, 0 };
		oxide::Mesh                     mesh(device, vertices, indices);

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

			vulkan.beginRenderPass(frame, { 0.0f, 0.0f, 0.0f, 0.0f });

			mesh.bind(frame.commandBuffer);
			textureDescriptor.bind(frame.commandBuffer, pipeline_layout.handle());

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
