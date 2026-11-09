#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <oxide/vulkan/pipeline/shader_library.hpp>

namespace oxide {

// ============================================================================
// Reflected shader interface
//
// One descriptor binding discovered in SPIR-V. A binding used by several
// stages is merged into one entry with the union of their stage flags.
// ============================================================================
struct ReflectedDescriptor {
	std::uint32_t      set        = 0;
	std::uint32_t      binding    = 0;
	VkDescriptorType   type       = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	std::uint32_t      count      = 1;
	VkShaderStageFlags stageFlags = 0;
	std::string        name;
};

// One push-constant range discovered in SPIR-V. Identical ranges used by
// several stages are merged with the union of their stage flags.
struct ReflectedPushConstant {
	std::uint32_t      offset     = 0;
	std::uint32_t      size       = 0;
	VkShaderStageFlags stageFlags = 0;
};

// One specialization constant discovered in SPIR-V. The shader's default value
// remains active unless the pipeline config overrides it.
struct ReflectedSpecializationConstant {
	std::uint32_t      constantId = 0;
	std::uint32_t      size       = 0;
	VkShaderStageFlags stageFlags = 0;
	std::string        name;
};

// ============================================================================
// Shader reflection
//
// Reads SPIR-V and exposes the descriptor sets, push constants, and
// specialization constants that a pipeline layout and pipeline creation need.
// Reflection of a vertex and fragment pair is merged, so a descriptor used by
// both stages is declared once with both stage bits.
//
// PipelineLayout has a constructor that consumes this object directly and
// creates the matching descriptor set layouts.
// ============================================================================
class ShaderReflection {
  public:
	ShaderReflection() = default;

	// Reflect one or more SPIR-V files. Graphics programs should pass the
	// vertex module first and the fragment module second.
	[[nodiscard]] static ShaderReflection fromFile(const std::string &path);
	[[nodiscard]] static ShaderReflection fromFiles(const std::vector<std::string> &paths);
	[[nodiscard]] static ShaderReflection fromGraphics(const GraphicsShaderFiles &shaders);
	[[nodiscard]] static ShaderReflection fromCompute(const ComputeShaderFile &shader);

	[[nodiscard]] const std::vector<ReflectedDescriptor> &descriptors() const noexcept {
		return descriptors_;
	}
	[[nodiscard]] const std::vector<ReflectedPushConstant> &pushConstants() const noexcept {
		return push_constants_;
	}
	[[nodiscard]] const std::vector<VkPushConstantRange> &pushConstantRanges() const noexcept {
		return push_constant_ranges_;
	}
	[[nodiscard]] const std::vector<ReflectedSpecializationConstant> &
	specializationConstants() const noexcept {
		return specialization_constants_;
	}

	// Highest set index plus one. Gaps between used sets are represented by
	// empty layouts because Vulkan requires a layout for every set index.
	[[nodiscard]] std::uint32_t descriptorSetCount() const noexcept {
		return descriptor_set_count_;
	}

	// Bindings that belong to one set, ready for VkDescriptorSetLayoutCreateInfo.
	[[nodiscard]] const std::vector<VkDescriptorSetLayoutBinding> &
	descriptorBindings(std::uint32_t set) const noexcept;

	// Name lookup is available because SPIR-V preserves GLSL declaration names.
	// Returns nullptr when no entry matches.
	[[nodiscard]] const ReflectedDescriptor *
	findDescriptor(std::string_view name) const noexcept;
	[[nodiscard]] const ReflectedSpecializationConstant *
	findSpecializationConstant(std::string_view name) const noexcept;

	[[nodiscard]] bool empty() const noexcept {
		return descriptors_.empty() && push_constants_.empty() &&
		       specialization_constants_.empty();
	}

  private:
	[[nodiscard]] static ShaderReflection fromModule(const std::string &path);
	void merge(const ShaderReflection &other);
	void rebuildDescriptorSets();

	std::vector<ReflectedDescriptor>             descriptors_;
	std::vector<ReflectedPushConstant>           push_constants_;
	std::vector<VkPushConstantRange>             push_constant_ranges_;
	std::vector<ReflectedSpecializationConstant> specialization_constants_;
	std::vector<std::vector<VkDescriptorSetLayoutBinding>> descriptor_sets_;
	std::uint32_t                                descriptor_set_count_ = 0;
};

} // namespace oxide
