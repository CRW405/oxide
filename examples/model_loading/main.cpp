#include <oxide/oxide.hpp>

#include <cmath>
#include <cstddef>
#include <exception>
#include <string>

#include <cglm/cglm.h>

struct CameraUniforms {
	mat4 model;
	mat4 view;
	mat4 projection;
};

int main() {
	try {
		oxide::Application    app(800, 600, "OBJ and glTF Loading");
		oxide::Window        &window = app.window();
		oxide::VulkanDevice  &device = app.device();
		oxide::VulkanContext &context = app.context();

		oxide::ShaderLibrary shaders(OXIDE_SHADER_DIR);
		const oxide::GraphicsShaderFiles shaderFiles = shaders.graphics("model");
		const oxide::ShaderReflection reflection = oxide::ShaderReflection::fromGraphics(shaderFiles);
		oxide::PipelineLayout layout(device, reflection);

		CameraUniforms uniforms{};
		glm_mat4_identity(uniforms.model);
		glm_mat4_identity(uniforms.view);
		glm_mat4_identity(uniforms.projection);
		oxide::UniformBufferRing objUniforms(
		    device, oxide::MAX_FRAMES_IN_FLIGHT, uniforms, layout.descriptorSetLayout(0));
		oxide::UniformBufferRing gltfUniforms(
		    device, oxide::MAX_FRAMES_IN_FLIGHT, uniforms, layout.descriptorSetLayout(0));

		oxide::VertexInputDescription vertexInput;
		vertexInput.addBinding(0, sizeof(oxide::MeshVertex))
		    .addAttribute(0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(oxide::MeshVertex, position))
		    .addAttribute(1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(oxide::MeshVertex, normal));

		const std::string assetDirectory = OXIDE_ASSET_DIR;
		const auto objMeshes = oxide::ObjReader::load(assetDirectory + "/models/tetrahedron.obj");
		const auto gltfMeshes = oxide::GltfReader::load(assetDirectory + "/models/tetrahedron.gltf");
		if (objMeshes.empty() || gltfMeshes.empty()) {
			throw oxide::runtimeError("the model-loading example requires non-empty OBJ and glTF meshes");
		}
		oxide::Mesh objMesh(device, objMeshes.front().vertices, objMeshes.front().indices);
		oxide::Mesh gltfMesh(device, gltfMeshes.front().vertices, gltfMeshes.front().indices);

		oxide::PipelineConfigInfo pipelineConfig = oxide::Pipeline::defaultPipelineConfigInfo();
		pipelineConfig.depthStencilInfo.depthTestEnable = VK_TRUE;
		pipelineConfig.depthStencilInfo.depthWriteEnable = VK_TRUE;
		pipelineConfig.setVertexInput(vertexInput);
		pipelineConfig.setPipelineLayout(oxide::BorrowedPipelineLayout(layout.handle()));
		oxide::SwapchainPipeline pipeline(context, device, shaderFiles, pipelineConfig);
		oxide::DeviceIdleGuard deviceIdleGuard(device);

		while (!window.shouldClose()) {
			window.pollEvents();
			if (window.closeRequested()) {
				break;
			}
			oxide::FrameScope frame = context.beginFrameScope({0.025f, 0.03f, 0.045f, 1.0f});
			if (!frame) {
				continue;
			}

			const float elapsed = static_cast<float>(window.time());
			const float aspect = static_cast<float>(context.extent().width) /
			                     static_cast<float>(context.extent().height);
			vec3 eye = {0.0f, 0.0f, 5.0f};
			vec3 target = {0.0f, 0.0f, 0.0f};
			vec3 up = {0.0f, 1.0f, 0.0f};

			glm_lookat(eye, target, up, uniforms.view);
			glm_perspective(glm_rad(45.0f), aspect, 0.1f, 100.0f, uniforms.projection);
			uniforms.projection[1][1] *= -1.0f;

			glm_mat4_identity(uniforms.model);
			vec3 leftPosition = {-1.25f, 0.0f, 0.0f};
			vec3 rotationAxis = {0.3f, 1.0f, 0.2f};
			vec3 objectScale = {0.8f, 0.8f, 0.8f};
			glm_translate(uniforms.model, leftPosition);
			glm_rotate(uniforms.model, elapsed * 0.45f, rotationAxis);
			glm_scale(uniforms.model, objectScale);
			objUniforms.update(frame.frameIndex(), uniforms);

			glm_mat4_identity(uniforms.model);
			vec3 rightPosition = {1.25f, 0.0f, 0.0f};
			glm_translate(uniforms.model, rightPosition);
			glm_rotate(uniforms.model, -elapsed * 0.45f, rotationAxis);
			glm_scale(uniforms.model, objectScale);
			gltfUniforms.update(frame.frameIndex(), uniforms);

			context.setViewportAndScissor(frame.commandBuffer());
			pipeline.bind(frame.commandBuffer());
			objUniforms.bind(
			    frame.commandBuffer(), layout.handle(), frame.frameIndex(), 0);
			objMesh.bind(frame.commandBuffer());
			objMesh.draw(frame.commandBuffer());
			gltfUniforms.bind(
			    frame.commandBuffer(), layout.handle(), frame.frameIndex(), 0);
			gltfMesh.bind(frame.commandBuffer());
			gltfMesh.draw(frame.commandBuffer());
		}
	} catch (const std::exception &error) {
		OXIDE_LOG_FATAL << error.what();
		return 1;
	}
}
