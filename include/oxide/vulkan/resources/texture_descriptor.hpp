#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <memory>

#include <oxide/vulkan/resources/descriptor_set.hpp>

namespace oxide {

class ImageTexture;

// ============================================================================
// Sampled image descriptor set
//
// Owns the descriptor pool and set needed to sample one image, either an
// ImageTexture or an externally owned image view and sampler. When constructed
// with a descriptor set layout, that layout is supplied by another owner, such
// as a reflected PipelineLayout; otherwise the wrapper creates its own.
//
// The defaults describe the common case: one combined image sampler exposed at
// binding 0. Override the binding in the constructor when a shader uses a
// different interface. More complex layouts should use DescriptorSetLayout with
// explicit bindings.
// ============================================================================
class TextureDescriptorSet {
  public:
	// Defaults used by the simple texture shader.
	static constexpr std::uint32_t      DEFAULT_BINDING = 0;
	static constexpr VkShaderStageFlags DEFAULT_STAGE_FLAGS = VK_SHADER_STAGE_FRAGMENT_BIT;
	static constexpr VkDescriptorType   DEFAULT_DESCRIPTOR_TYPE =
	    VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;

	TextureDescriptorSet(
	    VulkanDevice       &device,
	    const ImageTexture &texture,
	    std::uint32_t       binding    = DEFAULT_BINDING,
	    VkShaderStageFlags  stageFlags = DEFAULT_STAGE_FLAGS);

	// Bind an externally owned image view and sampler, such as an
	// OffscreenTarget. The caller keeps both objects alive for as long as this
	// descriptor set is used.
	TextureDescriptorSet(
	    VulkanDevice       &device,
	    VkImageView         imageView,
	    VkSampler           sampler,
	    std::uint32_t       binding    = DEFAULT_BINDING,
	    VkShaderStageFlags  stageFlags = DEFAULT_STAGE_FLAGS);

	// Allocate the set from a layout owned elsewhere. The reflected
	// PipelineLayout::descriptorSetLayout(set) accessor is the usual source.
	// The layout must already declare a combined image sampler at binding.
	TextureDescriptorSet(
	    VulkanDevice        &device,
	    const ImageTexture  &texture,
	    VkDescriptorSetLayout layout,
	    std::uint32_t        binding = DEFAULT_BINDING);

	TextureDescriptorSet(
	    VulkanDevice        &device,
	    VkImageView          imageView,
	    VkSampler            sampler,
	    VkDescriptorSetLayout layout,
	    std::uint32_t        binding = DEFAULT_BINDING);

	TextureDescriptorSet(const TextureDescriptorSet &)            = delete;
	TextureDescriptorSet &operator=(const TextureDescriptorSet &) = delete;

	[[nodiscard]] VkDescriptorSetLayout layout() const noexcept { return layout_handle_; }
	[[nodiscard]] VkDescriptorSet       descriptorSet() const noexcept { return set_.handle(); }

	// Bind this set through a pipeline layout that uses layout().
	void bind(
	    VkCommandBuffer     commandBuffer,
	    VkPipelineLayout    pipelineLayout,
	    std::uint32_t       firstSet  = 0,
	    VkPipelineBindPoint bindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS) const {
		set_.bind(commandBuffer, pipelineLayout, firstSet, bindPoint);
	}

  private:
	void writeDescriptor(
	    VulkanDevice &device,
	    VkImageView   imageView,
	    VkSampler     sampler,
	    std::uint32_t binding);

	std::unique_ptr<DescriptorSetLayout> owned_layout_;
	VkDescriptorSetLayout                layout_handle_ = VK_NULL_HANDLE;
	DescriptorPool                       pool_;
	DescriptorSet                        set_;
};

} // namespace oxide
