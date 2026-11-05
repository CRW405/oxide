#include <oxide/oxide.hpp>

#include <cmath>
#include <cstdio>
#include <exception>

// Values sent to the fragment shader as push constants each frame.
struct FractalSettings {
	int   recursion_depth;
	float zoom;
	float aspect_ratio;
};

int main() {
	// High-level bootstrap.
	oxide::Application    app(500, 500, "Sierpinski Triangle");
	oxide::Window        &window = app.window();
	oxide::VulkanDevice  &device = app.device();
	oxide::VulkanContext &vulkan_context = app.context();

	// The fragment shader draws the entire fractal, so no vertex buffer is
	// required. The vertex shader generates one oversized screen triangle.
	oxide::PipelineConfigInfo pipeline_config =
	    oxide::Pipeline::defaultPipelineConfigInfo();

	const oxide::PushConstant fractal_push_constant =
	    oxide::pushConstant<FractalSettings>(VK_SHADER_STAGE_FRAGMENT_BIT);
	oxide::PipelineLayout pipeline_layout(device, { fractal_push_constant.range() });
	pipeline_config.setPipelineLayout(
	    oxide::BorrowedPipelineLayout(pipeline_layout.handle()));

	oxide::ShaderLibrary     shader_library(OXIDE_SHADER_DIR);
	oxide::SwapchainPipeline sierpinski_pipeline(
	    vulkan_context, device, shader_library.graphics("sierpinski_triangle"),
	    pipeline_config);

	// Declared after the GPU resources so it is destroyed first, including on
	// exception unwinding from the render loop.
	oxide::DeviceIdleGuard device_idle_guard(device);

	// Mutable state driven by keyboard and scroll input.
	FractalSettings fractal_settings{ 0, 1.0f, 1.0f };

	// Debug tools.
	tools::FPSCounter fps_counter(window.time());

	// Main loop.
	while (!window.shouldClose()) {
		window.pollEvents();

		// Input: left/right change recursion depth, scroll zooms.
		if (window.closeRequested())
			break;
		if (window.keyPressed(GLFW_KEY_RIGHT)) {
			fractal_settings.recursion_depth++;
			std::printf("Recursion depth: %d\n", fractal_settings.recursion_depth);
			std::printf(
			    "Number of triangles: %lld\n",
			    static_cast<long long>(std::pow(3, fractal_settings.recursion_depth)));
		}
		if (window.keyPressed(GLFW_KEY_LEFT) && fractal_settings.recursion_depth > 0) {
			fractal_settings.recursion_depth--;
			std::printf("Recursion depth: %d\n", fractal_settings.recursion_depth);
			std::printf(
			    "Number of triangles: %lld\n",
			    static_cast<long long>(std::pow(3, fractal_settings.recursion_depth)));
		}

		const oxide::MousePosition scroll_delta = window.consumeScrollDelta();
		fractal_settings.zoom *= std::exp(static_cast<float>(scroll_delta.y) * 0.1f);

		// Begin the frame and its render pass.
		oxide::FrameScope frame_scope =
		    vulkan_context.beginFrameScope({ 1.0f, 1.0f, 1.0f, 1.0f });
		if (!frame_scope) {
			continue; // The swapchain is being recreated.
		}
		fractal_settings.aspect_ratio = static_cast<float>(vulkan_context.extent().width) /
		                               static_cast<float>(vulkan_context.extent().height);

		// Bind the pipeline and update the fragment shader's parameters.
		sierpinski_pipeline.bind(frame_scope.commandBuffer());
		vulkan_context.setViewportAndScissor(frame_scope.commandBuffer());
		pipeline_layout.pushConstant(
		    frame_scope.commandBuffer(), fractal_push_constant, fractal_settings);

		// Draw the full-screen triangle; the fragment shader does the rest.
		sierpinski_pipeline.pipeline().draw(frame_scope.commandBuffer(), 3);
		fps_counter.update(window.time());
	}
	fps_counter.finish();
	return 0;
}
