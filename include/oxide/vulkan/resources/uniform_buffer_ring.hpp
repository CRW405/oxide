#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <memory>
#include <type_traits>
#include <vector>

#include <oxide/vulkan/resources/buffer.hpp>
#include <oxide/vulkan/resources/descriptor_set.hpp>
#include <oxide/vulkan/device/vulkan_device.hpp>

namespace oxide {

// ============================================================================
// Per-frame uniform buffer ring
//
// Owns one host-visible uniform buffer per frame in flight, a single descriptor
// set layout, and one descriptor set per buffer. Index updates and binds with
// Frame::frameIndex so each slot is only rewritten after VulkanContext has
// waited on that slot's fence.
//
// This avoids the data race that occurs when a single UBO is updated while the
// previous frame is still reading it on the GPU.
// ============================================================================
class UniformBufferRing {
  public:
	UniformBufferRing(
	    VulkanDevice       &device,
	    std::uint32_t       frameCount,
	    VkDeviceSize        size,
	    const void         *initialData,
	    std::uint32_t       binding    = 0,
	    VkShaderStageFlags  stageFlags = VK_SHADER_STAGE_VERTEX_BIT);

	template <typename T>
	UniformBufferRing(
	    VulkanDevice      &device,
	    std::uint32_t      frameCount,
	    const T           &initialData,
	    std::uint32_t       binding    = 0,
	    VkShaderStageFlags  stageFlags = VK_SHADER_STAGE_VERTEX_BIT)
	    : UniformBufferRing(
	          device,
	          frameCount,
	          static_cast<VkDeviceSize>(sizeof(T)),
	          &initialData,
	          binding,
	          stageFlags) {
		static_assert(std::is_trivially_copyable_v<T>, "uniform buffer data must be trivially copyable");
	}
	~UniformBufferRing();

	UniformBufferRing(const UniformBufferRing &)            = delete;
	UniformBufferRing &operator=(const UniformBufferRing &) = delete;
	UniformBufferRing(UniformBufferRing &&)                 = delete;
	UniformBufferRing &operator=(UniformBufferRing &&)      = delete;

	[[nodiscard]] VkDescriptorSetLayout layout() const noexcept { return layout_.handle(); }
	[[nodiscard]] std::uint32_t         frameCount() const noexcept {
		return static_cast<std::uint32_t>(buffers_.size());
	}
	[[nodiscard]] VkDescriptorSet descriptorSet(std::uint32_t frameIndex) const;

	void update(std::uint32_t frameIndex, const void *data, VkDeviceSize size);

	// Upload one typed value without repeating its size at the call site.
	template <typename T>
	void update(std::uint32_t frameIndex, const T &data) {
		static_assert(std::is_trivially_copyable_v<T>, "uniform buffer data must be trivially copyable");
		update(frameIndex, &data, static_cast<VkDeviceSize>(sizeof(T)));
	}

	// Bind the descriptor set that belongs to this frame slot.
	void bind(
	    VkCommandBuffer     commandBuffer,
	    VkPipelineLayout    pipelineLayout,
	    std::uint32_t       frameIndex,
	    std::uint32_t       firstSet  = 0,
	    VkPipelineBindPoint bindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS) const;

  private:
	void checkFrameIndex(std::uint32_t frameIndex) const;

	VulkanDevice                               &device_;
	DescriptorSetLayout                         layout_;
	DescriptorPool                              pool_;
	std::uint32_t                               binding_ = 0;
	VkDeviceSize                                size_    = 0;
	std::vector<std::unique_ptr<UniformBuffer>> buffers_;
	std::vector<VkDescriptorSet>                sets_;
};

} // namespace oxide
