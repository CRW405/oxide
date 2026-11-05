#include <oxide/oxide.hpp>

#include <cmath>
#include <exception>
#include <vector>

struct Vertex {
	float position[2];
	float color[3];
};

struct Uniforms {
	float offset[2];
	float scale;
	float padding;
};

int main() {
	try {
		oxide::Application    app(640, 480, "Animated Uniform");
		oxide::Window        &window = app.window();
		oxide::VulkanDevice  &device = app.device();
		oxide::VulkanContext &vulkan = app.context();

		Uniforms initial{ { 0.0f, 0.0f }, 1.0f, 0.0f };
		oxide::UniformBufferRing uniforms(
		    device, oxide::MAX_FRAMES_IN_FLIGHT, initial);

		oxide::VertexInputDescription input;
		input.addBinding(0, sizeof(Vertex))
		    .addAttribute(0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, position))
		    .addAttribute(1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, color));

		auto config = oxide::Pipeline::defaultPipelineConfigInfo();
		config.setVertexInput(input);
		oxide::PipelineLayout layout(device, { uniforms.layout() }, {});
		config.setPipelineLayout(oxide::BorrowedPipelineLayout(layout.handle()));
		oxide::ShaderLibrary shaders(OXIDE_SHADER_DIR);
		oxide::SwapchainPipeline pipeline(
		    vulkan, device, shaders.graphics("animated_uniform"), config);

		const std::vector<Vertex> triangle = {
			{ { -0.5f, -0.5f }, { 1.0f, 0.2f, 0.2f } },
			{ { 0.5f, -0.5f },  { 0.2f, 1.0f, 0.2f } },
			{ { 0.0f, 0.5f },   { 0.2f, 0.4f, 1.0f } },
		};
		oxide::VertexBuffer vertices(device, triangle);
		oxide::DeviceIdleGuard idle_guard(device);

		while (!window.shouldClose()) {
			window.pollEvents();
			if (window.closeRequested()) break;
			oxide::FrameScope frame = vulkan.beginFrameScope({ 0.02f, 0.02f, 0.05f, 1.0f });
			if (!frame) continue;

			const float time = static_cast<float>(window.time());
			Uniforms data{
				{ std::sin(time) * 0.35f, std::cos(time * 0.8f) * 0.25f },
				0.75f + std::sin(time * 1.7f) * 0.2f,
				0.0f,
			};
			uniforms.update(frame.frameIndex(), data);

			pipeline.bind(frame.commandBuffer());
			vulkan.setViewportAndScissor(frame.commandBuffer());
			uniforms.bind(frame.commandBuffer(), layout.handle(), frame.frameIndex());
			vertices.bind(frame.commandBuffer());
			pipeline.pipeline().draw(frame.commandBuffer(), 3);
		}
	} catch (const std::exception &error) {
		OXIDE_LOG_FATAL << error.what();
		return 1;
	}
}
