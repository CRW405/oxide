#include <oxide/oxide.hpp>

#include <cstddef>
#include <cstdint>
#include <exception>
#include <vector>

// Quad vertex layout: location 0 is a 2D position and location 1 is an RGB
// color.
struct QuadVertex {
	float position[2];
	float color[3];
};

int main() {
	try {
		oxide::Application    app(640, 480, "Indexed Quad");
		oxide::Window        &window = app.window();
		oxide::VulkanDevice  &device = app.device();
		oxide::VulkanContext &vulkan_context = app.context();

		// Tell the pipeline how to read each QuadVertex from the vertex buffer.
		oxide::VertexInputDescription vertex_input_description;
		vertex_input_description.addBinding(0, sizeof(QuadVertex))
		    .addAttribute(0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(QuadVertex, position))
		    .addAttribute(1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(QuadVertex, color));

		oxide::PipelineConfigInfo pipeline_config =
		    oxide::Pipeline::defaultPipelineConfigInfo();
		pipeline_config.setVertexInput(vertex_input_description);

		oxide::ShaderLibrary shader_library(OXIDE_SHADER_DIR);
		const oxide::GraphicsShaderFiles quad_shaders =
		    shader_library.graphics("indexed_quad");
		const oxide::ShaderReflection quad_reflection =
		    oxide::ShaderReflection::fromGraphics(quad_shaders);
		oxide::PipelineLayout quad_layout(device, quad_reflection);
		pipeline_config.setPipelineLayout(
		    oxide::BorrowedPipelineLayout(quad_layout.handle()));

		oxide::SwapchainPipeline pipeline(
		    vulkan_context, device, quad_shaders, pipeline_config);

		// Four corners share two triangles through the index buffer below.
		const std::vector<QuadVertex> quad_vertices = {
			{ { -0.6f, -0.6f }, { 1.0f, 0.2f, 0.2f } },
			{ { 0.6f, -0.6f },  { 0.2f, 1.0f, 0.2f } },
			{ { 0.6f, 0.6f },   { 0.2f, 0.4f, 1.0f } },
			{ { -0.6f, 0.6f },  { 1.0f, 0.8f, 0.2f } },
		};
		const std::vector<std::uint16_t> quad_indices = { 0, 1, 2, 2, 3, 0 };
		oxide::Mesh           quad_mesh(device, quad_vertices, quad_indices);
		oxide::DeviceIdleGuard device_idle_guard(device);

		while (!window.shouldClose()) {
			window.pollEvents();
			if (window.closeRequested())
				break;

			oxide::FrameScope frame_scope =
			    vulkan_context.beginFrameScope({ 0.04f, 0.04f, 0.06f, 1.0f });
			if (!frame_scope)
				continue;

			pipeline.bind(frame_scope.commandBuffer());
			vulkan_context.setViewportAndScissor(frame_scope.commandBuffer());
			quad_mesh.bind(frame_scope.commandBuffer());
			quad_mesh.draw(frame_scope.commandBuffer());
		}
	} catch (const std::exception &error) {
		OXIDE_LOG_FATAL << error.what();
		return 1;
	}
}
