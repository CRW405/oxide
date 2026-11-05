#include <oxide/oxide.hpp>

#include <cmath>
#include <cstdio>
#include <exception>

struct Camera {
	int   recursion_depth;
	float zoom;
	float aspect_ratio;
};

int main() {
	// setup
	oxide::Application    app(500, 500, "Sierpinski Triangle");
	oxide::Window        &window = app.window();
	oxide::VulkanDevice  &device = app.device();
	oxide::VulkanContext &vulkan = app.context();

	// create pipeline
	oxide::PipelineConfigInfo pipelineConfig = oxide::Pipeline::defaultPipelineConfigInfo();

	const oxide::PushConstant cameraPush = oxide::pushConstant<Camera>(VK_SHADER_STAGE_FRAGMENT_BIT);
	oxide::PipelineLayout     pipelineLayout(device, { cameraPush.range() });
	pipelineConfig.setPipelineLayout(oxide::BorrowedPipelineLayout(pipelineLayout.handle()));
	oxide::ShaderLibrary     shaders(OXIDE_SHADER_DIR);
	oxide::SwapchainPipeline pipeline(vulkan, device, shaders.graphics("sierpinski_triangle"), pipelineConfig);

	// Declared after the GPU resources so it is destroyed first, including on
	// exception unwinding from the render loop.
	oxide::DeviceIdleGuard idle_guard(device);

	// "globals"
	Camera camera{ 0, 1.0f, 1.0f };

	// debug tools
	tools::FPSCounter fps(window.time());

	// main loop
	while (!window.shouldClose()) {
		window.pollEvents();

		// input
		if (window.closeRequested()) break;
		if (window.keyPressed(GLFW_KEY_RIGHT)) {
			camera.recursion_depth++;
			std::printf("Recursion depth: %d\n", camera.recursion_depth);
			std::printf("Number of triangles: %lld\n", (long long)pow(3, camera.recursion_depth));
		}
		if (window.keyPressed(GLFW_KEY_LEFT) && camera.recursion_depth > 0) {
			camera.recursion_depth--;
			std::printf("Recursion depth: %d\n", camera.recursion_depth);
			std::printf("Number of triangles: %lld\n", (long long)pow(3, camera.recursion_depth));
		}

		const oxide::MousePosition scroll = window.consumeScrollDelta();
		camera.zoom                       = camera.zoom * std::exp(static_cast<float>(scroll.y) * 0.1f);

		// create frame
		oxide::FrameScope frame = vulkan.beginFrameScope({ 1.0f, 1.0f, 1.0f, 1.0f });
		if (!frame) {
			continue;
		}
		camera.aspect_ratio = static_cast<float>(vulkan.extent().width) /
		                      static_cast<float>(vulkan.extent().height);

		// setup draw
		pipeline.bind(frame.commandBuffer());
		vulkan.setViewportAndScissor(frame.commandBuffer());

		// push constant data
		pipelineLayout.pushConstant(frame.commandBuffer(), cameraPush, camera);

		// draw
		pipeline.pipeline().draw(frame.commandBuffer(), 3);
		fps.update(window.time());
	}
	fps.finish();
	return 0;
}
