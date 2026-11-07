#include <oxide/oxide.hpp>

#include <exception>

// Value sent to the offscreen scene shader each frame.
struct ScenePush {
	float time;
};

// Renders an animated scene into an offscreen target, then draws a second
// full-screen pass that samples that target on the swapchain.
int main() {
	try {
		oxide::Application    app(900, 600, "Render To Texture");
		oxide::Window        &window = app.window();
		oxide::VulkanDevice  &device = app.device();
		oxide::VulkanContext &vulkan_context = app.context();

		oxide::OffscreenTarget target(device, {
		    .width  = 800,
		    .height = 600,
		});

		oxide::ShaderLibrary shader_library(OXIDE_SHADER_DIR);

		// First pass: render into the offscreen target. The pipeline is not a
		// SwapchainPipeline because the target's render pass never changes.
		const oxide::PushConstant scene_push =
		    oxide::pushConstant<ScenePush>(VK_SHADER_STAGE_FRAGMENT_BIT);
		oxide::PipelineLayout scene_layout(device, { scene_push.range() });

		oxide::PipelineConfigInfo scene_config =
		    oxide::Pipeline::defaultPipelineConfigInfo();
		scene_config.setPipelineLayout(
		    oxide::BorrowedPipelineLayout(scene_layout.handle()));
		scene_config.setRenderPass(target.renderPass());
		oxide::Pipeline scene_pipeline(
		    device, shader_library.graphics("offscreen_scene"), scene_config);

		// Second pass: sample the offscreen color image on the swapchain.
		oxide::TextureDescriptorSet scene_descriptor(
		    device, target.colorView(), target.sampler());
		oxide::PipelineLayout sample_layout(
		    device, { scene_descriptor.layout() }, {});

		oxide::PipelineConfigInfo sample_config =
		    oxide::Pipeline::defaultPipelineConfigInfo();
		sample_config.setPipelineLayout(
		    oxide::BorrowedPipelineLayout(sample_layout.handle()));
		oxide::SwapchainPipeline sample_pipeline(
		    vulkan_context, device, shader_library.graphics("sample_pass"),
		    sample_config);

		oxide::DeviceIdleGuard device_idle_guard(device);

		const double start_time = window.time();
		while (!window.shouldClose()) {
			window.pollEvents();
			if (window.closeRequested())
				break;

			const oxide::Frame frame = vulkan_context.beginFrame();
			if (frame.commandBuffer == VK_NULL_HANDLE)
				continue;

			// Offscreen pass: fill the target with the animated pattern.
			target.beginRenderPass(frame.commandBuffer, { 0.0f, 0.0f, 0.0f, 1.0f });
			target.setViewportAndScissor(frame.commandBuffer);
			scene_pipeline.bind(frame.commandBuffer);
			scene_layout.pushConstant(
			    frame.commandBuffer, scene_push,
			    ScenePush{ static_cast<float>(window.time() - start_time) });
			scene_pipeline.draw(frame.commandBuffer, 3);
			target.endRenderPass(frame.commandBuffer);

			// Screen pass: sample the offscreen image with a full-screen quad.
			vulkan_context.beginRenderPass(frame, { 0.02f, 0.02f, 0.03f, 1.0f });
			vulkan_context.setViewportAndScissor(frame.commandBuffer);
			sample_pipeline.bind(frame.commandBuffer);
			scene_descriptor.bind(frame.commandBuffer, sample_layout.handle());
			sample_pipeline.pipeline().draw(frame.commandBuffer, 3);
			vulkan_context.endRenderPass(frame);

			vulkan_context.endFrame(frame);
		}
	} catch (const std::exception &error) {
		OXIDE_LOG_FATAL << "unhandled exception: " << error.what();
		return 1;
	}
	return 0;
}
