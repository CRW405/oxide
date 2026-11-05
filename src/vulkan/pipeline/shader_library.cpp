#include <oxide/vulkan/pipeline/shader_library.hpp>
#include <oxide/vulkan/core/vk_error.hpp>

#include <filesystem>

namespace oxide {

namespace {

std::string shaderPath(
    const std::string &directory,
    const std::string &name,
    const char        *suffix) {
	const std::filesystem::path path = std::filesystem::path(directory) / (name + suffix);
	if (!std::filesystem::is_regular_file(path)) {
		throw runtimeError("shader file does not exist: " + path.string());
	}
	return path.string();
}

} // namespace

GraphicsShaderFiles ShaderLibrary::graphics(const std::string &name) const {
	return { shaderPath(directory_, name, ".vert.spv"), shaderPath(directory_, name, ".frag.spv") };
}

ComputeShaderFile ShaderLibrary::compute(const std::string &name) const {
	return { shaderPath(directory_, name, ".comp.spv") };
}

} // namespace oxide
