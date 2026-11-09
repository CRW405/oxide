#include <oxide/oxide.hpp>

#include <exception>
#include <vector>

// Scene vertex layout: location 0 is a 2D position and location 1 is an RGB
// color.
struct SceneVertex {
	float position[2];
	float color[3];
};

int main() {
	try {
		oxide::Application    app(900, 600, "Engine Workflow");
		oxide::Window        &window = app.window();
		oxide::VulkanDevice  &device = app.device();
		oxide::VulkanContext &vulkan_context = app.context();

		// One shader library serves every pipeline in this program. Each
		// pipeline reflects its own shaders, so every layout is derived from the
		// SPIR-V interface it will be used with.
		oxide::ShaderLibrary shader_library(OXIDE_SHADER_DIR);

		// Scene pipeline: draws the indexed triangle below.
		oxide::VertexInputDescription scene_vertex_input;
		scene_vertex_input.addBinding(0, sizeof(SceneVertex))
		    .addAttribute(0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(SceneVertex, position))
		    .addAttribute(1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(SceneVertex, color));

		const oxide::GraphicsShaderFiles scene_shaders =
		    shader_library.graphics("scene");
		const oxide::ShaderReflection scene_reflection =
		    oxide::ShaderReflection::fromGraphics(scene_shaders);
		oxide::PipelineLayout scene_layout(device, scene_reflection);

		oxide::PipelineConfigInfo scene_pipeline_config =
		    oxide::Pipeline::defaultPipelineConfigInfo();
		scene_pipeline_config.setVertexInput(scene_vertex_input);
		scene_pipeline_config.setPipelineLayout(
		    oxide::BorrowedPipelineLayout(scene_layout.handle()));
		oxide::SwapchainPipeline scene_pipeline(
		    vulkan_context, device, scene_shaders, scene_pipeline_config);

		// Overlay pipeline: generates its triangle from gl_VertexIndex, so it
		// does not need a vertex input description. The reflected vertex-stage
		// specialization constant shrinks the triangle slightly.
		const oxide::GraphicsShaderFiles overlay_shaders =
		    shader_library.graphics("overlay");
		const oxide::ShaderReflection overlay_reflection =
		    oxide::ShaderReflection::fromGraphics(overlay_shaders);
		oxide::PipelineLayout overlay_layout(device, overlay_reflection);

		oxide::PipelineConfigInfo overlay_pipeline_config =
		    oxide::Pipeline::defaultPipelineConfigInfo();
		overlay_pipeline_config.setPipelineLayout(
		    oxide::BorrowedPipelineLayout(overlay_layout.handle()));
		if (const auto *scale =
		        overlay_reflection.findSpecializationConstant("OVERLAY_SCALE")) {
			overlay_pipeline_config.vertexSpecialization.set(*scale, 0.9f);
		}
		oxide::SwapchainPipeline overlay_pipeline(
		    vulkan_context, device, overlay_shaders, overlay_pipeline_config);

		// This compute pass has no window dependency and could run between any
		// two graphics passes in a larger renderer. Its reflected layout happens
		// to be empty, which is still a valid explicit declaration.
		const oxide::ComputeShaderFile simulation_shader =
		    shader_library.compute("simulation");
		const oxide::ShaderReflection simulation_reflection =
		    oxide::ShaderReflection::fromCompute(simulation_shader);
		oxide::PipelineLayout simulation_layout(device, simulation_reflection);
		oxide::ComputePipeline simulation_pipeline(
		    device, simulation_shader,
		    oxide::BorrowedPipelineLayout(simulation_layout.handle()));

		const std::vector<SceneVertex> scene_vertices = {
			{ { -0.65f, -0.5f }, { 0.9f, 0.2f, 0.2f } },
			{ { 0.0f, 0.65f },   { 0.2f, 0.9f, 0.3f } },
			{ { 0.65f, -0.5f },  { 0.2f, 0.4f, 1.0f } }
		};
		oxide::VertexBuffer   scene_vertex_buffer(device, scene_vertices);
		oxide::DeviceIdleGuard device_idle_guard(device);

		while (!window.shouldClose()) {
			window.pollEvents();
			if (window.closeRequested())
				break;

			// Use a raw Frame here to show that compute work can happen before
			// the graphics render pass. Most simple apps can use FrameScope.
			const oxide::Frame current_frame = vulkan_context.beginFrame();
			if (current_frame.commandBuffer == VK_NULL_HANDLE) {
				continue;
			}

			// Dispatch compute work first, outside the render pass.
			simulation_pipeline.bind(current_frame.commandBuffer);
			simulation_pipeline.dispatch(current_frame.commandBuffer);

			// Then render the scene, followed by the overlay on top of it.
			vulkan_context.beginRenderPass(
			    current_frame, { 0.02f, 0.02f, 0.04f, 1.0f });
			vulkan_context.setViewportAndScissor(current_frame.commandBuffer);

			scene_pipeline.bind(current_frame.commandBuffer);
			scene_vertex_buffer.bind(current_frame.commandBuffer);
			scene_pipeline.pipeline().draw(current_frame.commandBuffer, 3);

			// The overlay uses gl_VertexIndex, so it needs no vertex buffer.
			overlay_pipeline.bind(current_frame.commandBuffer);
			overlay_pipeline.pipeline().draw(current_frame.commandBuffer, 3);

			vulkan_context.endRenderPass(current_frame);
			vulkan_context.endFrame(current_frame);
		}
	} catch (const std::exception &error) {
		OXIDE_LOG_FATAL << "unhandled exception: " << error.what();
		return 1;
	}
	return 0;
}
