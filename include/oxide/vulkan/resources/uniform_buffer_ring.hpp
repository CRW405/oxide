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
// Owns one host-visible uniform buffer per frame in flight and one descriptor
// set per buffer. When constructed with a descriptor set layout, that layout is
// supplied by another owner, such as a reflected PipelineLayout; otherwise the
// ring creates its own.
//
// Index updates and binds with Frame::frameIndex so each slot is only rewritten
// after VulkanContext has waited on that slot's fence. This avoids the data race
// that occurs when a single UBO is updated while the previous frame is still
// reading it on the GPU.
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

	// Allocate every frame's set from a layout owned elsewhere. The reflected
	// PipelineLayout::descriptorSetLayout(set) accessor is the usual source.
	// The layout must already declare a uniform buffer at binding.
	UniformBufferRing(
	    VulkanDevice        &device,
	    std::uint32_t        frameCount,
	    VkDeviceSize         size,
	    const void          *initialData,
	    VkDescriptorSetLayout layout,
	    std::uint32_t        binding = 0);

	template <typename T>
	UniformBufferRing(
	    VulkanDevice        &device,
	    std::uint32_t        frameCount,
	    const T             &initialData,
	    VkDescriptorSetLayout layout,
	    std::uint32_t        binding = 0)
	    : UniformBufferRing(
	          device,
	          frameCount,
	          static_cast<VkDeviceSize>(sizeof(T)),
	          &initialData,
	          layout,
	          binding) {
		static_assert(std::is_trivially_copyable_v<T>, "uniform buffer data must be trivially copyable");
	}

	~UniformBufferRing();

	UniformBufferRing(const UniformBufferRing &)            = delete;
	UniformBufferRing &operator=(const UniformBufferRing &) = delete;
	UniformBufferRing(UniformBufferRing &&)                 = delete;
	UniformBufferRing &operator=(UniformBufferRing &&)      = delete;

	[[nodiscard]] VkDescriptorSetLayout layout() const noexcept { return layout_handle_; }
	[[nodiscard]] std::uint32_t         frameCount() const noexcept { return frame_count_; }
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
	void initialize(const void *initialData);
	void checkFrameIndex(std::uint32_t frameIndex) const;

	VulkanDevice                               &device_;
	std::unique_ptr<DescriptorSetLayout>        owned_layout_;
	VkDescriptorSetLayout                       layout_handle_ = VK_NULL_HANDLE;
	// Buffers are declared before the pool so the pool (and its descriptor
	// sets) is destroyed first, releasing references before the buffers go.
	std::vector<std::unique_ptr<UniformBuffer>> buffers_;
	DescriptorPool                              pool_;
	std::uint32_t                               binding_ = 0;
	VkDeviceSize                                size_    = 0;
	std::uint32_t                               frame_count_ = 0;
	std::vector<VkDescriptorSet>                sets_;
};

} // namespace oxide
