#include <oxide/oxide.hpp>

#include <cstddef>
#include <cstdint>
#include <exception>
#include <string>
#include <vector>

// Textured quad vertex: location 0 is a 2D position and location 1 is a
// texture coordinate.
struct TexturedVertex {
	float position[2];
	float uv[2];
};

int main() {
	try {
		oxide::Application    app(640, 480, "Textured Quad");
		oxide::Window        &window = app.window();
		oxide::VulkanDevice  &device = app.device();
		oxide::VulkanContext &vulkan_context = app.context();

		// Reflect the shader first so the reflected pipeline layout owns the
		// descriptor set layout that the texture set allocates from.
		oxide::ShaderLibrary shader_library(OXIDE_SHADER_DIR);
		const oxide::GraphicsShaderFiles quad_shaders =
		    shader_library.graphics("textured_quad");
		const oxide::ShaderReflection quad_reflection =
		    oxide::ShaderReflection::fromGraphics(quad_shaders);
		oxide::PipelineLayout pipeline_layout(device, quad_reflection);

		// Load the image once and allocate a set from the reflected layout.
		oxide::ImageTexture quad_texture(
		    device, std::string(OXIDE_ASSET_DIR) + "/smiley_0.png");
		oxide::TextureDescriptorSet texture_descriptor_set(
		    device, quad_texture, pipeline_layout.descriptorSetLayout(0));

		oxide::VertexInputDescription vertex_input_description;
		vertex_input_description.addBinding(0, sizeof(TexturedVertex))
		    .addAttribute(0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(TexturedVertex, position))
		    .addAttribute(1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(TexturedVertex, uv));

		oxide::PipelineConfigInfo pipeline_config =
		    oxide::Pipeline::defaultPipelineConfigInfo();
		pipeline_config.setVertexInput(vertex_input_description);
		pipeline_config.setPipelineLayout(
		    oxide::BorrowedPipelineLayout(pipeline_layout.handle()));

		oxide::SwapchainPipeline pipeline(
		    vulkan_context, device, quad_shaders, pipeline_config);

		// UVs run from (0,0) at the top-left to (1,1) at the bottom-right.
		const std::vector<TexturedVertex> quad_vertices = {
			{ { -0.7f, -0.7f }, { 0.0f, 1.0f } }, // bottom-left
			{ { 0.7f, -0.7f },  { 1.0f, 1.0f } }, // bottom-right
			{ { 0.7f, 0.7f },   { 1.0f, 0.0f } }, // top-right
			{ { -0.7f, 0.7f },  { 0.0f, 0.0f } }, // top-left
		};
		const std::vector<std::uint16_t> quad_indices = { 0, 1, 2, 2, 3, 0 };
		oxide::Mesh           quad_mesh(device, quad_vertices, quad_indices);
		oxide::DeviceIdleGuard device_idle_guard(device);

		while (!window.shouldClose()) {
			window.pollEvents();
			if (window.closeRequested())
				break;

			oxide::FrameScope frame_scope =
			    vulkan_context.beginFrameScope({ 0.08f, 0.08f, 0.08f, 1.0f });
			if (!frame_scope)
				continue;

			pipeline.bind(frame_scope.commandBuffer());
			vulkan_context.setViewportAndScissor(frame_scope.commandBuffer());
			texture_descriptor_set.bind(frame_scope.commandBuffer(), pipeline_layout.handle());
			quad_mesh.bind(frame_scope.commandBuffer());
			quad_mesh.draw(frame_scope.commandBuffer());
		}
	} catch (const std::exception &error) {
		OXIDE_LOG_FATAL << error.what();
		return 1;
	}
}
