#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <initializer_list>

#include <oxide/vulkan/vulkan_device.hpp>

namespace oxide {

// ============================================================================
// Descriptor binding defaults
//
// Describes one binding in a descriptor set layout. The small factory helpers
// cover the common single-buffer and single-sampler cases while the public
// fields remain available for custom layouts.
// ============================================================================
struct DescriptorBinding {
	std::uint32_t      binding           = 0;
	VkDescriptorType   type              = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	VkShaderStageFlags stageFlags        = VK_SHADER_STAGE_VERTEX_BIT;
	std::uint32_t      descriptorCount   = 1;
	const VkSampler   *immutableSamplers = nullptr;

	[[nodiscard]] VkDescriptorSetLayoutBinding build() const noexcept {
		VkDescriptorSetLayoutBinding result{};
		result.binding            = binding;
		result.descriptorType     = type;
		result.descriptorCount    = descriptorCount;
		result.stageFlags         = stageFlags;
		result.pImmutableSamplers = immutableSamplers;
		return result;
	}
};

[[nodiscard]] constexpr DescriptorBinding combinedImageSampler(
    std::uint32_t      binding    = 0,
    VkShaderStageFlags stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT) noexcept {
	return { binding, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, stageFlags };
}

[[nodiscard]] constexpr DescriptorBinding uniformBuffer(
    std::uint32_t      binding    = 0,
    VkShaderStageFlags stageFlags = VK_SHADER_STAGE_VERTEX_BIT) noexcept {
	return { binding, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, stageFlags };
}

[[nodiscard]] constexpr DescriptorBinding storageBuffer(
    std::uint32_t      binding    = 0,
    VkShaderStageFlags stageFlags = VK_SHADER_STAGE_COMPUTE_BIT) noexcept {
	return { binding, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, stageFlags };
}

// ============================================================================
// Descriptor set layout
//
// Owns only the layout. It can be built from default bindings or from explicit
// DescriptorBinding values when a shader needs a custom interface.
// ============================================================================
class DescriptorSetLayout {
  public:
	DescriptorSetLayout(
	    VulkanDevice &device,
	    std::initializer_list<DescriptorBinding> bindings);
	explicit DescriptorSetLayout(VulkanDevice &device, const DescriptorBinding &binding);
	~DescriptorSetLayout();

	DescriptorSetLayout(const DescriptorSetLayout &)            = delete;
	DescriptorSetLayout &operator=(const DescriptorSetLayout &) = delete;
	DescriptorSetLayout(DescriptorSetLayout &&)                 = delete;
	DescriptorSetLayout &operator=(DescriptorSetLayout &&)      = delete;

	[[nodiscard]] VkDescriptorSetLayout handle() const noexcept { return layout_; }

  private:
	VulkanDevice          &device_;
	VkDescriptorSetLayout layout_ = VK_NULL_HANDLE;
};

// ============================================================================
// Descriptor pool
//
// Owns the pool that backs one or more descriptor sets. The default maxSets of
// one matches the single-set wrappers in Oxide.
// ============================================================================
class DescriptorPool {
  public:
	DescriptorPool(
	    VulkanDevice                                &device,
	    std::initializer_list<VkDescriptorPoolSize> sizes,
	    std::uint32_t                                maxSets = 1);
	DescriptorPool(
	    VulkanDevice  &device,
	    VkDescriptorType type,
	    std::uint32_t descriptorCount = 1,
	    std::uint32_t maxSets         = 1);
	~DescriptorPool();

	DescriptorPool(const DescriptorPool &)            = delete;
	DescriptorPool &operator=(const DescriptorPool &) = delete;
	DescriptorPool(DescriptorPool &&)                 = delete;
	DescriptorPool &operator=(DescriptorPool &&)      = delete;

	[[nodiscard]] VkDescriptorPool handle() const noexcept { return pool_; }
	[[nodiscard]] VkDescriptorSet  allocate(VkDescriptorSetLayout layout);

  private:
	VulkanDevice    &device_;
	VkDescriptorPool pool_ = VK_NULL_HANDLE;
};

// ============================================================================
// Descriptor set
//
// A lightweight view of one set allocated from a DescriptorPool. The pool owns
// the set and releases it on destruction.
// ============================================================================
class DescriptorSet {
  public:
	DescriptorSet(DescriptorPool &pool, VkDescriptorSetLayout layout);

	DescriptorSet(const DescriptorSet &)            = delete;
	DescriptorSet &operator=(const DescriptorSet &) = delete;
	DescriptorSet(DescriptorSet &&)                 = delete;
	DescriptorSet &operator=(DescriptorSet &&)      = delete;

	[[nodiscard]] VkDescriptorSet       handle() const noexcept { return set_; }
	[[nodiscard]] VkDescriptorSetLayout layout() const noexcept { return layout_; }

	void bind(
	    VkCommandBuffer     commandBuffer,
	    VkPipelineLayout    pipelineLayout,
	    std::uint32_t       firstSet  = 0,
	    VkPipelineBindPoint bindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS) const;

  private:
	VkDescriptorSetLayout layout_ = VK_NULL_HANDLE;
	VkDescriptorSet       set_    = VK_NULL_HANDLE;
};

} // namespace oxide
