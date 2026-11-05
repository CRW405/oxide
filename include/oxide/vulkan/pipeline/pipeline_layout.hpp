#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <initializer_list>
#include <type_traits>

#include <oxide/vulkan/device/vulkan_device.hpp>

namespace oxide {

// ============================================================================
// Push-constant range defaults
//
// Creates a range whose size matches a C++ type. Pair it with the matching
// PipelineLayout::pushConstant call so the layout and recorded data agree.
// ============================================================================
template <typename T>
[[nodiscard]] constexpr VkPushConstantRange pushConstantRange(
    VkShaderStageFlags stageFlags,
    std::uint32_t      offset = 0) noexcept {
	static_assert(std::is_trivially_copyable_v<T>, "push constants must be trivially copyable");
	return { stageFlags, offset, static_cast<std::uint32_t>(sizeof(T)) };
}

// ============================================================================
// Typed push constant
//
// Keeps a range and its shader stages together so the layout declaration and
// the recorded value cannot drift apart. Create one with pushConstant<T>().
// The raw Vulkan call remains available for arrays or data that does not map to
// a single C++ type.
// ============================================================================
class PushConstant {
  public:
	constexpr PushConstant(
	    VkShaderStageFlags stageFlags,
	    std::uint32_t      size,
	    std::uint32_t      offset = 0) noexcept
	    : range_{ stageFlags, offset, size } {}

	[[nodiscard]] constexpr const VkPushConstantRange &range() const noexcept { return range_; }
	[[nodiscard]] constexpr VkShaderStageFlags        stageFlags() const noexcept {
		return range_.stageFlags;
	}
	[[nodiscard]] constexpr std::uint32_t offset() const noexcept { return range_.offset; }
	[[nodiscard]] constexpr std::uint32_t size() const noexcept { return range_.size; }

	template <typename T>
	void push(
	    VkCommandBuffer   commandBuffer,
	    VkPipelineLayout  pipelineLayout,
	    const T          &data) const {
		static_assert(std::is_trivially_copyable_v<T>, "push constants must be trivially copyable");
		static_assert(sizeof(T) <= sizeof(std::uint32_t) * 32, "push constant exceeds the minimum Vulkan limit");
		vkCmdPushConstants(commandBuffer,
		                   pipelineLayout,
		                   range_.stageFlags,
		                   range_.offset,
		                   static_cast<std::uint32_t>(sizeof(T)),
		                   &data);
	}

  private:
	VkPushConstantRange range_;
};

template <typename T>
[[nodiscard]] constexpr PushConstant pushConstant(
    VkShaderStageFlags stageFlags,
    std::uint32_t      offset = 0) noexcept {
	static_assert(std::is_trivially_copyable_v<T>, "push constants must be trivially copyable");
	return PushConstant(stageFlags, static_cast<std::uint32_t>(sizeof(T)), offset);
}

// ============================================================================
// Descriptor and push-constant layout
// ============================================================================
class PipelineLayout {
  public:
	PipelineLayout(
	    VulkanDevice &device,
	    const VkDescriptorSetLayout *descriptorSetLayouts = nullptr,
	    std::uint32_t descriptorSetLayoutCount = 0,
	    const VkPushConstantRange *pushConstantRanges = nullptr,
	    std::uint32_t pushConstantRangeCount = 0);
	PipelineLayout(
	    VulkanDevice &device,
	    std::initializer_list<VkDescriptorSetLayout> descriptorSetLayouts,
	    std::initializer_list<VkPushConstantRange> pushConstantRanges);
	PipelineLayout(
	    VulkanDevice &device,
	    std::initializer_list<VkPushConstantRange> pushConstantRanges);
	~PipelineLayout();

	PipelineLayout(const PipelineLayout &)            = delete;
	PipelineLayout &operator=(const PipelineLayout &) = delete;
	PipelineLayout(PipelineLayout &&)                 = delete;
	PipelineLayout &operator=(PipelineLayout &&)      = delete;

	// This layout is owned by PipelineLayout and remains valid until it is
	// destroyed. Pass it to a pipeline as BorrowedPipelineLayout when sharing.
	[[nodiscard]] VkPipelineLayout handle() const noexcept { return layout_; }
	[[nodiscard]] PipelineLayoutHandle resourceHandle() const noexcept { return resource_handle_; }

	// Bind one descriptor set for the common no-dynamic-offset case.
	void bindDescriptorSet(
	    VkCommandBuffer commandBuffer,
	    VkDescriptorSet descriptorSet,
	    std::uint32_t   firstSet = 0) const;

	// Upload one typed value through a push-constant range declared with
	// pushConstantRange<T>(). The raw Vulkan call remains available for arrays
	// or data that does not map to a single C++ type.
	template <typename T>
	void pushConstant(
	    VkCommandBuffer    commandBuffer,
	    VkShaderStageFlags stageFlags,
	    const T           &data,
	    std::uint32_t      offset = 0) const {
		static_assert(std::is_trivially_copyable_v<T>, "push constants must be trivially copyable");
		vkCmdPushConstants(commandBuffer,
		                   layout_,
		                   stageFlags,
		                   offset,
		                   static_cast<std::uint32_t>(sizeof(T)),
		                   &data);
	}

	// Upload through a PushConstant that already carries its stage flags and
	// offset. Arrays and custom data can still use the overload above.
	template <typename T>
	void pushConstant(
	    VkCommandBuffer    commandBuffer,
	    const PushConstant &constant,
	    const T            &data) const {
		constant.push(commandBuffer, layout_, data);
	}

  private:
	VulkanDevice    &device_;
	VkPipelineLayout layout_ = VK_NULL_HANDLE;
	PipelineLayoutHandle resource_handle_{};
};

}
