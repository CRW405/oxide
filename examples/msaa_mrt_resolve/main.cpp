#include <oxide/oxide.hpp>

#include <cstddef>
#include <exception>
#include <vector>

// Scene vertex: location 0 is a 2D position and location 1 is an RGB color.
struct SceneVertex {
	float position[2];
	float color[3];
};

// Value sent to both scene shader stages each frame.
struct ScenePush {
	float time;
};

// Renders an animated triangle into two multisampled color attachments, lets
// the target resolve them, then composites both resolved images on the
// swapchain. The second attachment carries a glow mask that the composite pass
// adds as rim light.
int main() {
	try {
		oxide::Application    app(1100, 700, "MSAA + MRT + Resolve");
		oxide::Window        &window = app.window();
		oxide::VulkanDevice  &device = app.device();
		oxide::VulkanContext &vulkan_context = app.context();

		// Two color attachments at 4x MSAA. Each attachment is resolved into a
		// single-sample image that colorView()/sampler() expose.
		oxide::OffscreenTarget target(device, {
		    .width = 1100,
		    .height = 700,
		    .colorFormats = { VK_FORMAT_R8G8B8A8_SRGB, VK_FORMAT_R8G8B8A8_UNORM },
		    .depth = true,
		    .sampleCount = VK_SAMPLE_COUNT_4_BIT,
		});
		OXIDE_LOG_INFO << "rendering " << target.colorCount() << " MRT attachments at "
		               << static_cast<int>(target.sampleCount()) << "x MSAA with resolve";

		oxide::ShaderLibrary shader_library(OXIDE_SHADER_DIR);

		// Scene pass: two fragment outputs, matching the target's two color
		// attachments. The pipeline sample count must match the target.
		// Reflection supplies the push-constant range declared by the shaders.
		const oxide::GraphicsShaderFiles scene_shaders =
		    shader_library.graphics("scene");
		const oxide::ShaderReflection scene_reflection =
		    oxide::ShaderReflection::fromGraphics(scene_shaders);
		oxide::PipelineLayout scene_layout(device, scene_reflection);
		const VkPushConstantRange scene_push_range =
		    scene_reflection.pushConstantRanges().front();

		oxide::VertexInputDescription scene_vertex_input;
		scene_vertex_input.addBinding(0, sizeof(SceneVertex))
		    .addAttribute(0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(SceneVertex, position))
		    .addAttribute(1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(SceneVertex, color));

		oxide::PipelineConfigInfo scene_config =
		    oxide::Pipeline::defaultPipelineConfigInfo();
		scene_config.setVertexInput(scene_vertex_input);
		scene_config.setPipelineLayout(
		    oxide::BorrowedPipelineLayout(scene_layout.handle()));
		scene_config.setRenderPass(target.renderPass());
		scene_config.setColorAttachmentCount(target.colorCount());
		scene_config.setMultisampleCount(target.sampleCount());
		scene_config.depthStencilInfo.depthTestEnable  = VK_TRUE;
		scene_config.depthStencilInfo.depthWriteEnable = VK_TRUE;
		scene_config.depthStencilInfo.depthCompareOp   = VK_COMPARE_OP_LESS;

		oxide::Pipeline scene_pipeline(device, scene_shaders, scene_config);

		// Composite pass: sample both resolved color images on the swapchain.
		// Reflection discovers the two sampled-image sets, so attachment 0 is
		// set 0 and attachment 1 is set 1 without hand-written bindings.
		const oxide::GraphicsShaderFiles composite_shaders =
		    shader_library.graphics("composite");
		const oxide::ShaderReflection composite_reflection =
		    oxide::ShaderReflection::fromGraphics(composite_shaders);
		oxide::PipelineLayout composite_layout(device, composite_reflection);
		oxide::TextureDescriptorSet color_set(
		    device, target.colorView(0), target.sampler(0),
		    composite_layout.descriptorSetLayout(0));
		oxide::TextureDescriptorSet glow_set(
		    device, target.colorView(1), target.sampler(1),
		    composite_layout.descriptorSetLayout(1));

		oxide::PipelineConfigInfo composite_config =
		    oxide::Pipeline::defaultPipelineConfigInfo();
		composite_config.setPipelineLayout(
		    oxide::BorrowedPipelineLayout(composite_layout.handle()));
		if (const auto *glow_strength =
		        composite_reflection.findSpecializationConstant("GLOW_STRENGTH")) {
			composite_config.fragmentSpecialization.set(*glow_strength, 0.85f);
		}
		oxide::SwapchainPipeline composite_pipeline(
		    vulkan_context, device, composite_shaders, composite_config);

		const std::vector<SceneVertex> vertices = {
			{ { 0.0f, 0.85f },   { 1.00f, 0.30f, 0.20f } },
			{ { 0.95f, -0.70f }, { 0.20f, 0.95f, 0.35f } },
			{ { -0.95f, -0.70f }, { 0.25f, 0.45f, 1.00f } },
		};
		oxide::VertexBuffer    vertex_buffer(device, vertices);
		oxide::DeviceIdleGuard device_idle_guard(device);

		const double start_time = window.time();
		while (!window.shouldClose()) {
			window.pollEvents();
			if (window.closeRequested())
				break;

			const oxide::Frame frame = vulkan_context.beginFrame();
			if (frame.commandBuffer == VK_NULL_HANDLE)
				continue;

			// Offscreen scene pass: MRT writes location 0 to the color
			// attachment and location 1 to the glow attachment. The render pass
			// resolves both multisampled images into their sampleable versions.
			target.beginRenderPass(frame.commandBuffer,
			                       { { 0.0f, 0.0f, 0.0f, 1.0f },
			                         { 0.0f, 0.0f, 0.0f, 1.0f } });
			target.setViewportAndScissor(frame.commandBuffer);
			scene_pipeline.bind(frame.commandBuffer);
			scene_layout.pushConstant(
			    frame.commandBuffer, scene_push_range.stageFlags,
			    ScenePush{ static_cast<float>(window.time() - start_time) },
			    scene_push_range.offset);
			vertex_buffer.bind(frame.commandBuffer);
			scene_pipeline.draw(frame.commandBuffer, static_cast<std::uint32_t>(vertices.size()));
			target.endRenderPass(frame.commandBuffer);

			// Screen pass: sample the resolved images and add a background.
			vulkan_context.beginRenderPass(frame, { 0.0f, 0.0f, 0.0f, 1.0f });
			vulkan_context.setViewportAndScissor(frame.commandBuffer);
			composite_pipeline.bind(frame.commandBuffer);
			color_set.bind(frame.commandBuffer, composite_layout.handle(), 0);
			glow_set.bind(frame.commandBuffer, composite_layout.handle(), 1);
			composite_pipeline.pipeline().draw(frame.commandBuffer, 3);
			vulkan_context.endRenderPass(frame);

			vulkan_context.endFrame(frame);
		}
	} catch (const std::exception &error) {
		OXIDE_LOG_FATAL << "unhandled exception: " << error.what();
		return 1;
	}
	return 0;
}
