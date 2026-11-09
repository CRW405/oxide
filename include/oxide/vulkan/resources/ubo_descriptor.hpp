#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <memory>

#include <oxide/vulkan/resources/descriptor_set.hpp>

namespace oxide {

class Buffer;

// ============================================================================
// Uniform buffer descriptor set
//
// Owns the descriptor pool and set needed to expose one Buffer as a uniform
// buffer. When constructed with a descriptor set layout, that layout is
// supplied by another owner, such as a reflected PipelineLayout; otherwise the
// wrapper creates its own.
//
// The defaults describe the common case: one uniform buffer exposed at binding
// 0 to the vertex stage. Override the binding or stage in the constructor when
// a shader uses a different interface. More complex layouts should use
// DescriptorSetLayout with explicit bindings.
//
// This wrapper is best for static data or data the application synchronizes
// itself. For per-frame data, use UniformBufferRing so every frame in flight
// has its own buffer and descriptor set.
// ============================================================================
class UBODescriptorSet {
  public:
	// Defaults used by the model-view-projection shader.
	static constexpr std::uint32_t      DEFAULT_BINDING = 0;
	static constexpr VkShaderStageFlags DEFAULT_STAGE_FLAGS = VK_SHADER_STAGE_VERTEX_BIT;
	static constexpr VkDescriptorType   DEFAULT_DESCRIPTOR_TYPE = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;

	UBODescriptorSet(
	    VulkanDevice       &device,
	    const Buffer       &buffer,
	    std::uint32_t       binding    = DEFAULT_BINDING,
	    VkShaderStageFlags  stageFlags = DEFAULT_STAGE_FLAGS);

	// Allocate the set from a layout owned elsewhere. The reflected
	// PipelineLayout::descriptorSetLayout(set) accessor is the usual source.
	// The layout must already declare a uniform buffer at binding.
	UBODescriptorSet(
	    VulkanDevice        &device,
	    const Buffer        &buffer,
	    VkDescriptorSetLayout layout,
	    std::uint32_t        binding = DEFAULT_BINDING);

	UBODescriptorSet(const UBODescriptorSet &)            = delete;
	UBODescriptorSet &operator=(const UBODescriptorSet &) = delete;

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
	void writeDescriptor(VulkanDevice &device, const Buffer &buffer, std::uint32_t binding);

	std::unique_ptr<DescriptorSetLayout> owned_layout_;
	VkDescriptorSetLayout                layout_handle_ = VK_NULL_HANDLE;
	DescriptorPool                       pool_;
	DescriptorSet                        set_;
};

} // namespace oxide
