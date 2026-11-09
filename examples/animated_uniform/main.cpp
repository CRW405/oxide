#include <oxide/oxide.hpp>

#include <cmath>
#include <cstddef>
#include <exception>
#include <vector>

// Animated triangle vertex: location 0 is a 2D position and location 1 is an
// RGB color.
struct AnimatedVertex {
	float position[2];
	float color[3];
};

// Per-frame transform values. The trailing padding keeps the block aligned to
// 16 bytes, matching the uniform layout declared in the vertex shader.
struct AnimationUniforms {
	float offset[2];
	float scale;
	float padding;
};

int main() {
	try {
		oxide::Application    app(640, 480, "Animated Uniform");
		oxide::Window        &window = app.window();
		oxide::VulkanDevice  &device = app.device();
		oxide::VulkanContext &vulkan_context = app.context();

		const AnimationUniforms initial_uniforms{ { 0.0f, 0.0f }, 1.0f, 0.0f };

		// Reflect the shader pair first. The reflected pipeline layout owns the
		// descriptor set layout used by the uniform buffer ring.
		oxide::ShaderLibrary shader_library(OXIDE_SHADER_DIR);
		const oxide::GraphicsShaderFiles animation_shaders =
		    shader_library.graphics("animated_uniform");
		const oxide::ShaderReflection animation_reflection =
		    oxide::ShaderReflection::fromGraphics(animation_shaders);
		oxide::PipelineLayout pipeline_layout(device, animation_reflection);

		// One uniform buffer and descriptor set per frame in flight, so the CPU
		// never overwrites data the GPU is still reading.
		oxide::UniformBufferRing animation_uniform_buffer(
		    device, oxide::MAX_FRAMES_IN_FLIGHT, initial_uniforms,
		    pipeline_layout.descriptorSetLayout(0));

		oxide::VertexInputDescription vertex_input_description;
		vertex_input_description.addBinding(0, sizeof(AnimatedVertex))
		    .addAttribute(0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(AnimatedVertex, position))
		    .addAttribute(
		        1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(AnimatedVertex, color));

		oxide::PipelineConfigInfo pipeline_config =
		    oxide::Pipeline::defaultPipelineConfigInfo();
		pipeline_config.setVertexInput(vertex_input_description);
		pipeline_config.setPipelineLayout(
		    oxide::BorrowedPipelineLayout(pipeline_layout.handle()));

		oxide::SwapchainPipeline pipeline(
		    vulkan_context, device, animation_shaders, pipeline_config);

		const std::vector<AnimatedVertex> triangle_vertices = {
			{ { -0.5f, -0.5f }, { 1.0f, 0.2f, 0.2f } },
			{ { 0.5f, -0.5f },  { 0.2f, 1.0f, 0.2f } },
			{ { 0.0f, 0.5f },   { 0.2f, 0.4f, 1.0f } },
		};
		oxide::VertexBuffer   triangle_vertex_buffer(device, triangle_vertices);
		oxide::DeviceIdleGuard device_idle_guard(device);

		while (!window.shouldClose()) {
			window.pollEvents();
			if (window.closeRequested())
				break;

			oxide::FrameScope frame_scope =
			    vulkan_context.beginFrameScope({ 0.02f, 0.02f, 0.05f, 1.0f });
			if (!frame_scope)
				continue;

			// Oscillate the triangle's offset and scale over time.
			const float elapsed_seconds = static_cast<float>(window.time());
			const AnimationUniforms frame_uniforms{
				{ std::sin(elapsed_seconds) * 0.35f,
			      std::cos(elapsed_seconds * 0.8f) * 0.25f },
				0.75f + std::sin(elapsed_seconds * 1.7f) * 0.2f,
				0.0f,
			};
			animation_uniform_buffer.update(frame_scope.frameIndex(), frame_uniforms);

			pipeline.bind(frame_scope.commandBuffer());
			vulkan_context.setViewportAndScissor(frame_scope.commandBuffer());
			animation_uniform_buffer.bind(
			    frame_scope.commandBuffer(), pipeline_layout.handle(),
			    frame_scope.frameIndex());
			triangle_vertex_buffer.bind(frame_scope.commandBuffer());
			pipeline.pipeline().draw(frame_scope.commandBuffer(), 3);
		}
	} catch (const std::exception &error) {
		OXIDE_LOG_FATAL << error.what();
		return 1;
	}
}
