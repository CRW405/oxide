#include <oxide/oxide.hpp>

#include <cstddef>
#include <exception>
#include <vector>

// Vertex layout for the single triangle. Location 0 is a 2D position and
// location 1 is an RGB color; the shader and pipeline must agree on this.
struct TriangleVertex {
	float position[2];
	float color[3];
};

int main() {
	// High-level bootstrap: the application owns the window, device, and
	// presentation context.
	oxide::Application    app(500, 500, "Classic Vulkan Triangle");
	oxide::Window        &window = app.window();
	oxide::VulkanDevice  &device = app.device();
	oxide::VulkanContext &vulkan_context = app.context();

	// Describe how a TriangleVertex is laid out in memory so the pipeline can
	// feed the vertex shader's input locations.
	oxide::VertexInputDescription vertex_input_description;
	vertex_input_description.addBinding(0, sizeof(TriangleVertex))
	    .addAttribute(0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(TriangleVertex, position))
	    .addAttribute(1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(TriangleVertex, color));

	// Start from the library defaults, then attach the vertex layout.
	oxide::PipelineConfigInfo pipeline_config =
	    oxide::Pipeline::defaultPipelineConfigInfo();
	pipeline_config.setVertexInput(vertex_input_description);

	// "vulkan_triangle" resolves the compiled vulkan_triangle.vert.spv and
	// vulkan_triangle.frag.spv pair from OXIDE_SHADER_DIR.
	oxide::ShaderLibrary     shader_library(OXIDE_SHADER_DIR);
	oxide::SwapchainPipeline pipeline(
	    vulkan_context, device, shader_library.graphics("vulkan_triangle"), pipeline_config);

	// Counter-clockwise triangle in normalized device coordinates. Each vertex
	// carries its own color, which the fragment shader interpolates.
	const std::vector<TriangleVertex> triangle_vertices = {
		{ { -0.5f, -0.5f }, { 1.0f, 0.0f, 0.0f } },
		{ { 0.5f, -0.5f },  { 0.0f, 1.0f, 0.0f } },
		{ { 0.0f, 0.5f },   { 0.0f, 0.0f, 1.0f } }
	};
	oxide::VertexBuffer triangle_vertex_buffer(device, triangle_vertices);

	// Declared after the GPU resources so it is destroyed first. This waits for
	// the device to become idle before those buffers are released.
	oxide::DeviceIdleGuard device_idle_guard(device);

	// Render one triangle per frame until the window closes.
	while (!window.shouldClose()) {
		window.pollEvents();
		if (window.closeRequested())
			break;

		// FrameScope owns the render pass and submits the frame when it ends.
		oxide::FrameScope frame_scope =
		    vulkan_context.beginFrameScope({ 0.0f, 0.0f, 0.0f, 1.0f });
		if (!frame_scope) {
			continue; // The swapchain was recreated; skip this iteration.
		}

		pipeline.bind(frame_scope.commandBuffer());
		vulkan_context.setViewportAndScissor(frame_scope.commandBuffer());
		triangle_vertex_buffer.bind(frame_scope.commandBuffer());
		pipeline.pipeline().draw(frame_scope.commandBuffer(), 3);
	}
	return 0;
}
