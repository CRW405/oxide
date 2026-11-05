#include <oxide/oxide.hpp>

#include <exception>
#include <vector>

struct Vertex {
	float position[2];
	float color[3];
};

int main() {
	oxide::Application    app(500, 500, "Classic Vulkan Triangle");
	oxide::Window        &window = app.window();
	oxide::VulkanDevice  &device = app.device();
	oxide::VulkanContext &vulkan = app.context();

	oxide::VertexInputDescription vertex_input;
	vertex_input.addBinding(0, sizeof(Vertex))
	    .addAttribute(0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, position))
	    .addAttribute(1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, color));

	oxide::PipelineConfigInfo pipeline_config = oxide::Pipeline::defaultPipelineConfigInfo();
	pipeline_config.setVertexInput(vertex_input);

	oxide::ShaderLibrary     shaders(OXIDE_SHADER_DIR);
	oxide::SwapchainPipeline pipeline(
	    vulkan, device, shaders.graphics("vulkan_triangle"), pipeline_config);

	const std::vector<Vertex> triangle = {
		{ { -0.5f, -0.5f }, { 1.0f, 0.0f, 0.0f } },
		{ { 0.5f, -0.5f },  { 0.0f, 1.0f, 0.0f } },
		{ { 0.0f, 0.5f },   { 0.0f, 0.0f, 1.0f } }
	};
	oxide::VertexBuffer    vertex_buffer(device, triangle);
	oxide::DeviceIdleGuard idle_guard(device);

	while (!window.shouldClose()) {
		window.pollEvents();
		if (window.closeRequested()) break;

		oxide::FrameScope frame = vulkan.beginFrameScope({ 0.0f, 0.0f, 0.0f, 1.0f });
		if (!frame) {
			continue;
		}

		pipeline.bind(frame.commandBuffer());
		vulkan.setViewportAndScissor(frame.commandBuffer());
		vertex_buffer.bind(frame.commandBuffer());
		pipeline.pipeline().draw(frame.commandBuffer(), 3);
	}
	return 0;
}
