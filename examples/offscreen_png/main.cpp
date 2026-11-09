#include <oxide/oxide.hpp>

#include <cstddef>
#include <cstdint>
#include <exception>
#include <vector>

// Triangle vertex: location 0 is a 2D position and location 1 is an RGB color.
struct Vertex {
	float position[2];
	float color[3];
};

// Renders one frame into an offscreen image without a window and writes the
// result to a PNG. The VulkanDevice is constructed without GLFW or a surface.
int main() {
	try {
		oxide::VulkanDevice device("Offscreen PNG");

		oxide::OffscreenTarget target(device, {
		    .width  = 800,
		    .height = 600,
		});

		oxide::VertexInputDescription vertex_input;
		vertex_input.addBinding(0, sizeof(Vertex))
		    .addAttribute(0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, position))
		    .addAttribute(1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, color));

		oxide::PipelineConfigInfo pipeline_config =
		    oxide::Pipeline::defaultPipelineConfigInfo();
		pipeline_config.setVertexInput(vertex_input);
		pipeline_config.setRenderPass(target.renderPass());

		oxide::ShaderLibrary shader_library(OXIDE_SHADER_DIR);
		const oxide::GraphicsShaderFiles triangle_shaders =
		    shader_library.graphics("offscreen_png");
		const oxide::ShaderReflection triangle_reflection =
		    oxide::ShaderReflection::fromGraphics(triangle_shaders);
		oxide::PipelineLayout triangle_layout(device, triangle_reflection);
		pipeline_config.setPipelineLayout(
		    oxide::BorrowedPipelineLayout(triangle_layout.handle()));

		oxide::Pipeline pipeline(device, triangle_shaders, pipeline_config);

		const std::vector<Vertex> vertices = {
			{ { 0.0f, -0.75f },  { 0.95f, 0.25f, 0.25f } },
			{ { 0.75f, 0.65f },  { 0.25f, 0.95f, 0.35f } },
			{ { -0.75f, 0.65f }, { 0.25f, 0.45f, 0.95f } },
		};
		oxide::VertexBuffer vertex_buffer(device, vertices);

		oxide::DeviceIdleGuard device_idle_guard(device);

		VkCommandBuffer command_buffer = device.beginSingleTimeCommands();
		target.beginRenderPass(command_buffer, { 0.06f, 0.07f, 0.10f, 1.0f });
		target.setViewportAndScissor(command_buffer);
		pipeline.bind(command_buffer);
		vertex_buffer.bind(command_buffer);
		pipeline.draw(command_buffer, static_cast<std::uint32_t>(vertices.size()));
		target.endRenderPass(command_buffer);
		device.endSingleTimeCommands(command_buffer);

		target.savePng("offscreen.png");
		OXIDE_LOG_INFO << "wrote offscreen.png (" << target.extent().width << "x"
		               << target.extent().height << ")";
	} catch (const std::exception &error) {
		OXIDE_LOG_FATAL << "unhandled exception: " << error.what();
		return 1;
	}
	return 0;
}
