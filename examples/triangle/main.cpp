#include <oxide/glfw/glfw_oxide.hpp>
#include <oxide/tools/fps.hpp>
#include <oxide/tools/log.hpp>
#include <oxide/vulkan/buffer.hpp>
#include <oxide/vulkan/pipeline_layout.hpp>
#include <oxide/vulkan/render_pipeline.hpp>
#include <oxide/vulkan/vulkan_context.hpp>
#include <oxide/vulkan/vulkan_device.hpp>

#include <cglm/cglm.h>

#include <cstdint>
#include <cstdio>
#include <exception>
#include <vector>

// -----------------------------------------------------------------------------
// Triangle application
//
// Create Oxide, record a rotating indexed triangle, and present frames
// until the window is closed.
// -----------------------------------------------------------------------------
struct simple_vertex {
	float position[2];
	float color[3];
};

int main() {
	std::printf("Running Vulkan Triangle\n");
	try {
		// -------------------------------------------------------------------------
		// Step 1: create the process, window, Vulkan device, and presentation
		// context. These objects are declared in dependency order so teardown
		// happens in the reverse order automatically.
		// -------------------------------------------------------------------------
		oxide::GLFWContext   glfw;
		oxide::Window        window({ 500, 500, "Oxide", true, true, false });
		oxide::VulkanDevice  device(window.handle(), "Oxide");
		oxide::VulkanContext vulkan(device, &window);

		// -------------------------------------------------------------------------
		// Step 2: describe how simple_vertex is laid out in the vertex buffer.
		// The shader locations must match these attribute descriptions.
		// -------------------------------------------------------------------------
		oxide::PipelineConfigInfo pipelineConfig = oxide::Pipeline::defaultPipelineConfigInfo();

		oxide::VertexInputDescription vertexInput;
		vertexInput.addBinding(0, sizeof(simple_vertex))
		    .addAttribute(0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(simple_vertex, position))
		    .addAttribute(1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(simple_vertex, color));
		pipelineConfig.setVertexInput(vertexInput);
		pipelineConfig.renderPass = vulkan.renderPass();

		// Push constant setup
		const oxide::PushConstant transformPush = oxide::pushConstant<mat4>(VK_SHADER_STAGE_VERTEX_BIT);
		oxide::PipelineLayout     pipelineLayout(device, { transformPush.range() });
		pipelineConfig.pipelineLayout = pipelineLayout.handle();

		// -------------------------------------------------------------------------
		// Step 3: create the pipeline and upload the triangle data. The pipeline
		// owns its shader modules and default layout after construction.
		// -------------------------------------------------------------------------
		oxide::Pipeline pipeline(device,
		                          OXIDE_SHADER_DIR "/vulkan_triangle.vert.spv",
		                          OXIDE_SHADER_DIR "/vulkan_triangle.frag.spv",
		                          pipelineConfig);

		std::vector<simple_vertex> vulkan_triangle = {
			{ { -0.5f, -0.5f }, { 1.0f, 0.0f, 0.0f } },
			{ { 0.5f, -0.5f },  { 0.0f, 1.0f, 0.0f } },
			{ { 0.0f, 0.5f },   { 0.0f, 0.0f, 1.0f } }
		};
		const std::vector<std::uint16_t> triangle_indices = { 0, 1, 2 };
		oxide::Mesh                     triangle_mesh(device, vulkan_triangle, triangle_indices);
		std::printf("Vertex buffer created with size: %zu bytes\n", sizeof(simple_vertex) * vulkan_triangle.size());

		// Declared after the GPU resources so it is destroyed first. On exception
		// unwinding this waits for the device before those resources are released.
		oxide::DeviceIdleGuard idle_guard(device);

		// -------------------------------------------------------------------------
		// Step 4: acquire, record, submit, and present one frame at a time.
		// beginFrame returns an empty frame when the swapchain was recreated.
		// -------------------------------------------------------------------------
		tools::FPSCounter fps(window.time());

		while (!window.shouldClose()) {
			window.pollEvents();
			if (window.keyPressed(GLFW_KEY_ESCAPE) || window.keyPressed(GLFW_KEY_Q)) {
				glfwSetWindowShouldClose(window.handle(), GLFW_TRUE);
			}

			const oxide::Frame frame = vulkan.beginFrame();
			if (frame.commandBuffer == VK_NULL_HANDLE) {
				continue;
			}

			// -------------------------------------------------------------------------
			// Begin the render pass against the image acquired for this frame.
			// The swapchain extent is the framebuffer size Vulkan is actually using.
			// -------------------------------------------------------------------------
			vulkan.beginRenderPass(frame, { 0.0f, 0.0f, 0.0f, 0.0f });

			// Bind state first, then issue the draw commands that use it.
			pipeline.bind(frame.commandBuffer);

			vulkan.setViewportAndScissor(frame.commandBuffer);
			triangle_mesh.bind(frame.commandBuffer);

			// Push constant data
			mat4  Transform    = GLM_MAT4_IDENTITY_INIT;
			float angle        = static_cast<float>(window.time() * glm_rad(90.0f)); // Rotate 90 degrees per second
			vec3  rotationAxis = { 0.0f, 0.0f, 1.0f };
			glm_rotate(Transform, angle, rotationAxis);
			pipelineLayout.pushConstant(frame.commandBuffer, transformPush, Transform);

			triangle_mesh.draw(frame.commandBuffer);
			vulkan.endRenderPass(frame);

			vulkan.endFrame(frame);

			fps.update(window.time());
		}

		// Leave the cursor on a fresh line once the loop exits. The idle guard
		// waits for the device before the resources above are destroyed.
		fps.finish();
	} catch (const std::exception &error) {
		OXIDE_LOG_FATAL << "unhandled exception: " << error.what();
		return 1;
	}
	return 0;
}
