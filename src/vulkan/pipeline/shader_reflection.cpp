#include <oxide/vulkan/pipeline/shader_reflection.hpp>

#include <oxide/tools/log.hpp>

#include <spirv_reflect.h>

#include <algorithm>
#include <cstddef>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace oxide {

namespace {

std::vector<char> readSpirv(const std::string &path) {
	std::ifstream file(path, std::ios::ate | std::ios::binary);
	if (!file.is_open()) {
		throw runtimeError("failed to open shader file for reflection: " + path);
	}

	const std::streampos end = file.tellg();
	if (end == std::streampos(-1)) {
		throw runtimeError("failed to determine shader file size for reflection: " + path);
	}
	const auto size = static_cast<std::size_t>(end);
	if (size == 0) {
		throw runtimeError("shader file is empty: " + path);
	}
	if (size % sizeof(std::uint32_t) != 0) {
		throw runtimeError("shader file size is not a multiple of 4 bytes: " + path);
	}

	std::vector<char> bytes(size);
	file.seekg(0);
	if (!file.read(bytes.data(), static_cast<std::streamsize>(size))) {
		throw runtimeError("failed to read shader file for reflection: " + path);
	}
	return bytes;
}

std::string safeName(const char *name) {
	return name == nullptr ? std::string{} : std::string(name);
}

VkShaderStageFlags toVulkanStage(SpvReflectShaderStageFlagBits stage) {
	// Map explicitly instead of relying on the two enums sharing numeric
	// values, which is not guaranteed across SPIRV-Reflect versions.
	switch (stage) {
	case SPV_REFLECT_SHADER_STAGE_VERTEX_BIT:
		return VK_SHADER_STAGE_VERTEX_BIT;
	case SPV_REFLECT_SHADER_STAGE_TESSELLATION_CONTROL_BIT:
		return VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
	case SPV_REFLECT_SHADER_STAGE_TESSELLATION_EVALUATION_BIT:
		return VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
	case SPV_REFLECT_SHADER_STAGE_GEOMETRY_BIT:
		return VK_SHADER_STAGE_GEOMETRY_BIT;
	case SPV_REFLECT_SHADER_STAGE_FRAGMENT_BIT:
		return VK_SHADER_STAGE_FRAGMENT_BIT;
	case SPV_REFLECT_SHADER_STAGE_COMPUTE_BIT:
		return VK_SHADER_STAGE_COMPUTE_BIT;
	case SPV_REFLECT_SHADER_STAGE_TASK_BIT_EXT:
		return VK_SHADER_STAGE_TASK_BIT_EXT;
	case SPV_REFLECT_SHADER_STAGE_MESH_BIT_EXT:
		return VK_SHADER_STAGE_MESH_BIT_EXT;
	case SPV_REFLECT_SHADER_STAGE_RAYGEN_BIT_KHR:
		return VK_SHADER_STAGE_RAYGEN_BIT_KHR;
	case SPV_REFLECT_SHADER_STAGE_ANY_HIT_BIT_KHR:
		return VK_SHADER_STAGE_ANY_HIT_BIT_KHR;
	case SPV_REFLECT_SHADER_STAGE_CLOSEST_HIT_BIT_KHR:
		return VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
	case SPV_REFLECT_SHADER_STAGE_MISS_BIT_KHR:
		return VK_SHADER_STAGE_MISS_BIT_KHR;
	case SPV_REFLECT_SHADER_STAGE_INTERSECTION_BIT_KHR:
		return VK_SHADER_STAGE_INTERSECTION_BIT_KHR;
	case SPV_REFLECT_SHADER_STAGE_CALLABLE_BIT_KHR:
		return VK_SHADER_STAGE_CALLABLE_BIT_KHR;
	default:
		throw runtimeError("unsupported shader stage " +
		                   std::to_string(static_cast<int>(stage)));
	}
}

} // namespace

ShaderReflection ShaderReflection::fromModule(const std::string &path) {
	const std::vector<char> code = readSpirv(path);

	SpvReflectShaderModule module{};
	const SpvReflectResult result =
	    spvReflectCreateShaderModule(code.size(), code.data(), &module);
	if (result != SPV_REFLECT_RESULT_SUCCESS) {
		throw runtimeError("SPIRV-Reflect failed for " + path + " (result " +
		                   std::to_string(static_cast<int>(result)) + ")");
	}

	try {
		const VkShaderStageFlags stage = toVulkanStage(module.shader_stage);

		std::vector<ReflectedDescriptor> descriptors;
		descriptors.reserve(module.descriptor_binding_count);
		for (std::uint32_t index = 0; index < module.descriptor_binding_count; ++index) {
			const SpvReflectDescriptorBinding &binding = module.descriptor_bindings[index];
			descriptors.push_back(ReflectedDescriptor{
			    .set        = binding.set,
			    .binding    = binding.binding,
			    .type       = static_cast<VkDescriptorType>(binding.descriptor_type),
			    .count      = binding.count == 0 ? 1u : binding.count,
			    .stageFlags = stage,
			    .name       = safeName(binding.name),
			});
		}

		std::vector<ReflectedPushConstant> push_constants;
		push_constants.reserve(module.push_constant_block_count);
		for (std::uint32_t index = 0; index < module.push_constant_block_count; ++index) {
			const SpvReflectBlockVariable &block = module.push_constant_blocks[index];
			if (block.size == 0) {
				continue;
			}
			push_constants.push_back(ReflectedPushConstant{
			    .offset     = block.offset,
			    .size       = block.size,
			    .stageFlags = stage,
			});
		}

		std::vector<ReflectedSpecializationConstant> specialization_constants;
		specialization_constants.reserve(module.spec_constant_count);
		for (std::uint32_t index = 0; index < module.spec_constant_count; ++index) {
			const SpvReflectSpecializationConstant &constant = module.spec_constants[index];
			specialization_constants.push_back(ReflectedSpecializationConstant{
			    .constantId = constant.constant_id,
			    .size       = constant.default_value_size,
			    .stageFlags = stage,
			    .name       = safeName(constant.name),
			});
		}

		spvReflectDestroyShaderModule(&module);

		// Use the same merge path as fromFiles so a single module gets the same
		// normalization and descriptor-set construction.
		ShaderReflection reflection;
		reflection.descriptors_ = std::move(descriptors);
		reflection.push_constants_ = std::move(push_constants);
		reflection.specialization_constants_ = std::move(specialization_constants);
		reflection.rebuildDescriptorSets();
		return reflection;
	} catch (...) {
		spvReflectDestroyShaderModule(&module);
		throw;
	}
}

ShaderReflection ShaderReflection::fromFile(const std::string &path) {
	return fromModule(path);
}

ShaderReflection ShaderReflection::fromFiles(const std::vector<std::string> &paths) {
	if (paths.empty()) {
		throw invalidArgument("shader reflection requires at least one SPIR-V file");
	}

	ShaderReflection result;
	for (const std::string &path : paths) {
		result.merge(fromModule(path));
	}
	result.rebuildDescriptorSets();
	return result;
}

ShaderReflection ShaderReflection::fromGraphics(const GraphicsShaderFiles &shaders) {
	return fromFiles({ shaders.vertex, shaders.fragment });
}

ShaderReflection ShaderReflection::fromCompute(const ComputeShaderFile &shader) {
	return fromFile(shader.compute);
}

void ShaderReflection::merge(const ShaderReflection &other) {
	for (const ReflectedDescriptor &descriptor : other.descriptors_) {
		const auto existing = std::find_if(
		    descriptors_.begin(), descriptors_.end(), [&descriptor](const ReflectedDescriptor &candidate) {
			    return candidate.set == descriptor.set && candidate.binding == descriptor.binding;
		    });
		if (existing == descriptors_.end()) {
			descriptors_.push_back(descriptor);
			continue;
		}
		if (existing->type != descriptor.type || existing->count != descriptor.count) {
			throw runtimeError("shader descriptor set " + std::to_string(descriptor.set) +
			                   ", binding " + std::to_string(descriptor.binding) +
			                   " has conflicting types or counts across stages");
		}
		existing->stageFlags |= descriptor.stageFlags;
		if (existing->name.empty()) {
			existing->name = descriptor.name;
		}
	}

	for (const ReflectedPushConstant &constant : other.push_constants_) {
		const auto existing = std::find_if(
		    push_constants_.begin(), push_constants_.end(), [&constant](const ReflectedPushConstant &candidate) {
			    return candidate.offset == constant.offset && candidate.size == constant.size;
		    });
		if (existing == push_constants_.end()) {
			push_constants_.push_back(constant);
		} else {
			existing->stageFlags |= constant.stageFlags;
		}
	}

	for (const ReflectedSpecializationConstant &constant : other.specialization_constants_) {
		const auto existing = std::find_if(
		    specialization_constants_.begin(), specialization_constants_.end(),
		    [&constant](const ReflectedSpecializationConstant &candidate) {
			    return candidate.constantId == constant.constantId;
		    });
		if (existing == specialization_constants_.end()) {
			specialization_constants_.push_back(constant);
			continue;
		}
		if (existing->size != constant.size) {
			throw runtimeError("specialization constant " + std::to_string(constant.constantId) +
			                   " has conflicting sizes across stages");
		}
		existing->stageFlags |= constant.stageFlags;
		if (existing->name.empty()) {
			existing->name = constant.name;
		}
	}

	descriptor_set_count_ = std::max(descriptor_set_count_, other.descriptor_set_count_);
	for (const ReflectedDescriptor &descriptor : descriptors_) {
		descriptor_set_count_ = std::max(descriptor_set_count_, descriptor.set + 1);
	}
}

void ShaderReflection::rebuildDescriptorSets() {
	// Keep reflection output deterministic and make descriptorBindings(set)
	// directly usable as a VkDescriptorSetLayoutCreateInfo binding list.
	std::sort(descriptors_.begin(), descriptors_.end(),
	          [](const ReflectedDescriptor &left, const ReflectedDescriptor &right) {
		          if (left.set != right.set) {
			          return left.set < right.set;
		          }
		          return left.binding < right.binding;
	          });

	// Merge overlapping push-constant ranges so the generated pipeline layout
	// never declares overlapping ranges. Ranges that only touch stay separate.
	std::sort(push_constants_.begin(), push_constants_.end(),
	          [](const ReflectedPushConstant &left, const ReflectedPushConstant &right) {
		          return left.offset < right.offset;
	          });
	std::vector<ReflectedPushConstant> merged;
	for (const ReflectedPushConstant &constant : push_constants_) {
		if (!merged.empty()) {
			ReflectedPushConstant &previous = merged.back();
			const std::uint64_t previous_end =
			    static_cast<std::uint64_t>(previous.offset) + previous.size;
			if (constant.offset < previous_end) {
				const std::uint64_t end =
				    std::max(previous_end,
				             static_cast<std::uint64_t>(constant.offset) + constant.size);
				previous.size       = static_cast<std::uint32_t>(end - previous.offset);
				previous.stageFlags |= constant.stageFlags;
				continue;
			}
		}
		merged.push_back(constant);
	}
	push_constants_ = std::move(merged);

	push_constant_ranges_.clear();
	push_constant_ranges_.reserve(push_constants_.size());
	for (const ReflectedPushConstant &constant : push_constants_) {
		push_constant_ranges_.push_back(VkPushConstantRange{
		    constant.stageFlags, constant.offset, constant.size});
	}

	descriptor_set_count_ = 0;
	for (const ReflectedDescriptor &descriptor : descriptors_) {
		descriptor_set_count_ = std::max(descriptor_set_count_, descriptor.set + 1);
	}

	descriptor_sets_.assign(descriptor_set_count_, {});
	for (const ReflectedDescriptor &descriptor : descriptors_) {
		VkDescriptorSetLayoutBinding binding{};
		binding.binding            = descriptor.binding;
		binding.descriptorType     = descriptor.type;
		binding.descriptorCount    = descriptor.count;
		binding.stageFlags         = descriptor.stageFlags;
		binding.pImmutableSamplers = nullptr;
		descriptor_sets_[descriptor.set].push_back(binding);
	}
}

const std::vector<VkDescriptorSetLayoutBinding> &
ShaderReflection::descriptorBindings(std::uint32_t set) const noexcept {
	static const std::vector<VkDescriptorSetLayoutBinding> empty;
	if (set >= descriptor_sets_.size()) {
		return empty;
	}
	return descriptor_sets_[set];
}

const ReflectedDescriptor *
ShaderReflection::findDescriptor(std::string_view name) const noexcept {
	const auto found = std::find_if(
	    descriptors_.begin(), descriptors_.end(),
	    [name](const ReflectedDescriptor &descriptor) { return descriptor.name == name; });
	return found == descriptors_.end() ? nullptr : &*found;
}

const ReflectedSpecializationConstant *
ShaderReflection::findSpecializationConstant(std::string_view name) const noexcept {
	const auto found = std::find_if(
	    specialization_constants_.begin(), specialization_constants_.end(),
	    [name](const ReflectedSpecializationConstant &constant) { return constant.name == name; });
	return found == specialization_constants_.end() ? nullptr : &*found;
}

} // namespace oxide
