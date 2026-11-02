#include <oxide/glfw/glfw_oxide.hpp>
#include <oxide/tools/fps.hpp>
#include <oxide/tools/log.hpp>
#include <oxide/vulkan/pipeline_layout.hpp>
#include <oxide/vulkan/render_pipeline.hpp>
#include <oxide/vulkan/vulkan_context.hpp>
#include <oxide/vulkan/vulkan_device.hpp>

#include <cmath>
#include <cstdio>
#include <exception>

struct Camera {
	int   recursion_depth;
	float zoom;
	float aspect_ratio;
};

int main() {
	try {
		// setup
		oxide::GLFWContext   glfw;
		oxide::Window        window({ 500, 500, "Sierpinski Triangle", true });
		oxide::VulkanDevice  device(window.handle(), "Oxide");
		oxide::VulkanContext vulkan(device, &window);

		// create pipeline
		oxide::PipelineConfigInfo pipelineConfig = oxide::Pipeline::defaultPipelineConfigInfo();

		const oxide::PushConstant cameraPush = oxide::pushConstant<Camera>(VK_SHADER_STAGE_FRAGMENT_BIT);
		oxide::PipelineLayout     pipelineLayout(device, { cameraPush.range() });
		pipelineConfig.pipelineLayout = pipelineLayout.handle();
		pipelineConfig.renderPass     = vulkan.renderPass();
		oxide::Pipeline pipeline(device,
		                          OXIDE_SHADER_DIR "/sierpinski_triangle.vert.spv",
		                          OXIDE_SHADER_DIR "/sierpinski_triangle.frag.spv",
		                          pipelineConfig);

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
			if (window.keyPressed(GLFW_KEY_ESCAPE) || window.keyPressed(GLFW_KEY_Q)) {
				glfwSetWindowShouldClose(window.handle(), GLFW_TRUE);
			}
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
			camera.zoom                        = camera.zoom * std::exp(static_cast<float>(scroll.y) * 0.1f);

			// create frame
			const oxide::Frame frame = vulkan.beginFrame();
			if (frame.commandBuffer == VK_NULL_HANDLE) {
				continue;
			}
			camera.aspect_ratio = static_cast<float>(vulkan.extent().width) /
			                      static_cast<float>(vulkan.extent().height);
			vulkan.beginRenderPass(frame, { 1.0f, 1.0f, 1.0f, 1.0f });

			// setup draw
			pipeline.bind(frame.commandBuffer);
			vulkan.setViewportAndScissor(frame.commandBuffer);

			// push constant data
			pipelineLayout.pushConstant(frame.commandBuffer, cameraPush, camera);

			// draw
			pipeline.draw(frame.commandBuffer, 3);

			// end frame
			vulkan.endRenderPass(frame);
			vulkan.endFrame(frame);
			fps.update(window.time());
		}
		fps.finish();
	} catch (const std::exception &error) {
		OXIDE_LOG_FATAL << "unhandled exception: " << error.what();
		return 1;
	}
	return 0;
}
