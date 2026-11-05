#include <oxide/oxide.hpp>

#include <exception>
#include <vector>

struct Vertex {
	float position[2];
	float uv[2];
};

int main() {
	try {
		oxide::Application    app(640, 480, "Textured Quad");
		oxide::Window        &window = app.window();
		oxide::VulkanDevice  &device = app.device();
		oxide::VulkanContext &vulkan = app.context();

		oxide::ImageTexture texture(device, std::string(OXIDE_ASSET_DIR) + "/smiley_0.png");
		oxide::TextureDescriptorSet texture_set(device, texture);

		oxide::VertexInputDescription input;
		input.addBinding(0, sizeof(Vertex))
		    .addAttribute(0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, position))
		    .addAttribute(1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, uv));

		auto config = oxide::Pipeline::defaultPipelineConfigInfo();
		config.setVertexInput(input);
		oxide::PipelineLayout layout(device, { texture_set.layout() }, {});
		config.setPipelineLayout(oxide::BorrowedPipelineLayout(layout.handle()));

		oxide::ShaderLibrary shaders(OXIDE_SHADER_DIR);
		oxide::SwapchainPipeline pipeline(
		    vulkan, device, shaders.graphics("textured_quad"), config);

		const std::vector<Vertex> vertices = {
			{ { -0.7f, -0.7f }, { 0.0f, 1.0f } },
			{ { 0.7f, -0.7f },  { 1.0f, 1.0f } },
			{ { 0.7f, 0.7f },   { 1.0f, 0.0f } },
			{ { -0.7f, 0.7f },  { 0.0f, 0.0f } },
		};
		const std::vector<std::uint16_t> indices = { 0, 1, 2, 2, 3, 0 };
		oxide::Mesh quad(device, vertices, indices);
		oxide::DeviceIdleGuard idle_guard(device);

		while (!window.shouldClose()) {
			window.pollEvents();
			if (window.closeRequested()) break;
			oxide::FrameScope frame = vulkan.beginFrameScope({ 0.08f, 0.08f, 0.08f, 1.0f });
			if (!frame) continue;

			pipeline.bind(frame.commandBuffer());
			vulkan.setViewportAndScissor(frame.commandBuffer());
			texture_set.bind(frame.commandBuffer(), layout.handle());
			quad.bind(frame.commandBuffer());
			quad.draw(frame.commandBuffer());
		}
	} catch (const std::exception &error) {
		OXIDE_LOG_FATAL << error.what();
		return 1;
	}
}
