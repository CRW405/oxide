#include <oxide/oxide.hpp>

#include <exception>
#include <vector>

struct Vertex {
	float position[2];
	float color[3];
};

int main() {
	try {
		oxide::Application    app(900, 600, "Engine Workflow");
		oxide::Window        &window = app.window();
		oxide::VulkanDevice  &device = app.device();
		oxide::VulkanContext &vulkan = app.context();
		oxide::ShaderLibrary  shaders(OXIDE_SHADER_DIR);

		// Each material or render pass can own a separate pipeline config.
		oxide::VertexInputDescription scene_input;
		scene_input.addBinding(0, sizeof(Vertex))
		    .addAttribute(0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, position))
		    .addAttribute(1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, color));

		oxide::PipelineConfigInfo scene_config = oxide::Pipeline::defaultPipelineConfigInfo();
		scene_config.setVertexInput(scene_input);
		oxide::SwapchainPipeline scene_pipeline(
		    vulkan, device, shaders.graphics("scene"), scene_config);

		oxide::PipelineConfigInfo overlay_config = oxide::Pipeline::defaultPipelineConfigInfo();
		oxide::SwapchainPipeline overlay_pipeline(
		    vulkan, device, shaders.graphics("overlay"), overlay_config);

		// This compute pass has no window dependency and could run between any
		// two graphics passes in a larger renderer.
		oxide::ComputePipeline simulation(device, shaders.compute("simulation"));

		const std::vector<Vertex> vertices = {
			{ { -0.65f, -0.5f }, { 0.9f, 0.2f, 0.2f } },
			{ { 0.0f, 0.65f },   { 0.2f, 0.9f, 0.3f } },
			{ { 0.65f, -0.5f },  { 0.2f, 0.4f, 1.0f } }
		};
		oxide::VertexBuffer vertex_buffer(device, vertices);
		oxide::DeviceIdleGuard idle_guard(device);

		while (!window.shouldClose()) {
			window.pollEvents();
			if (window.closeRequested()) break;

			// Use a raw Frame here to show that compute work can happen before
			// the graphics render pass. Most simple apps can use FrameScope.
			const oxide::Frame frame = vulkan.beginFrame();
			if (frame.commandBuffer == VK_NULL_HANDLE) {
				continue;
			}

			simulation.bind(frame.commandBuffer);
			simulation.dispatch(frame.commandBuffer);

			vulkan.beginRenderPass(frame, { 0.02f, 0.02f, 0.04f, 1.0f });
			vulkan.setViewportAndScissor(frame.commandBuffer);

			scene_pipeline.bind(frame.commandBuffer);
			vertex_buffer.bind(frame.commandBuffer);
			scene_pipeline.pipeline().draw(frame.commandBuffer, 3);

			// The overlay uses gl_VertexIndex, so it needs no vertex buffer.
			overlay_pipeline.bind(frame.commandBuffer);
			overlay_pipeline.pipeline().draw(frame.commandBuffer, 3);

			vulkan.endRenderPass(frame);
			vulkan.endFrame(frame);
		}
	} catch (const std::exception &error) {
		OXIDE_LOG_FATAL << "unhandled exception: " << error.what();
		return 1;
	}
	return 0;
}
