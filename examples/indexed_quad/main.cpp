#include <oxide/oxide.hpp>

#include <exception>
#include <vector>

struct Vertex {
	float position[2];
	float color[3];
};

int main() {
	try {
		oxide::Application    app(640, 480, "Indexed Quad");
		oxide::Window        &window = app.window();
		oxide::VulkanDevice  &device = app.device();
		oxide::VulkanContext &vulkan = app.context();

		oxide::VertexInputDescription input;
		input.addBinding(0, sizeof(Vertex))
		    .addAttribute(0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, position))
		    .addAttribute(1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, color));

		auto config = oxide::Pipeline::defaultPipelineConfigInfo();
		config.setVertexInput(input);
		oxide::ShaderLibrary shaders(OXIDE_SHADER_DIR);
		oxide::SwapchainPipeline pipeline(
		    vulkan, device, shaders.graphics("indexed_quad"), config);

		const std::vector<Vertex> vertices = {
			{ { -0.6f, -0.6f }, { 1.0f, 0.2f, 0.2f } },
			{ { 0.6f, -0.6f },  { 0.2f, 1.0f, 0.2f } },
			{ { 0.6f, 0.6f },   { 0.2f, 0.4f, 1.0f } },
			{ { -0.6f, 0.6f },  { 1.0f, 0.8f, 0.2f } },
		};
		const std::vector<std::uint16_t> indices = { 0, 1, 2, 2, 3, 0 };
		oxide::Mesh quad(device, vertices, indices);
		oxide::DeviceIdleGuard idle_guard(device);

		while (!window.shouldClose()) {
			window.pollEvents();
			if (window.closeRequested()) break;
			oxide::FrameScope frame = vulkan.beginFrameScope({ 0.04f, 0.04f, 0.06f, 1.0f });
			if (!frame) continue;

			pipeline.bind(frame.commandBuffer());
			vulkan.setViewportAndScissor(frame.commandBuffer());
			quad.bind(frame.commandBuffer());
			quad.draw(frame.commandBuffer());
		}
	} catch (const std::exception &error) {
		OXIDE_LOG_FATAL << error.what();
		return 1;
	}
}
