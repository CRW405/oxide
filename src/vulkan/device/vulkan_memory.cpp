#include <oxide/vulkan/device/vulkan_device.hpp>
#include <oxide/vulkan/core/vk_error.hpp>

#include <algorithm>
#include <utility>

namespace oxide {

DeviceMemoryAllocation VulkanDevice::allocateMemory(
    const VkMemoryRequirements &requirements,
    VkMemoryPropertyFlags      properties) {
	const std::uint32_t memory_type = findMemoryType(requirements.memoryTypeBits, properties);
	const VkDeviceSize alignment = requirements.alignment == 0 ? 1 : requirements.alignment;

	for (auto &block : memory_blocks_) {
		if (block.type != memory_type) {
			continue;
		}

		for (auto range = block.free_ranges.begin(); range != block.free_ranges.end(); ++range) {
			const VkDeviceSize aligned_offset =
			    (range->offset + alignment - 1) & ~(alignment - 1);
			const VkDeviceSize padding = aligned_offset - range->offset;
			if (padding + requirements.size > range->size) {
				continue;
			}

			const VkDeviceSize remaining_size = range->size - padding - requirements.size;
			if (remaining_size > 0) {
				range->offset = aligned_offset + requirements.size;
				range->size = remaining_size;
				if (padding > 0) {
					block.free_ranges.insert(range, { aligned_offset - padding, padding });
				}
			} else if (padding > 0) {
				range->size = padding;
			} else {
				block.free_ranges.erase(range);
			}

			return { block.memory, aligned_offset, requirements.size, memory_type, block.mapped };
		}

		const VkDeviceSize bump_offset = (block.used + alignment - 1) & ~(alignment - 1);
		if (bump_offset + requirements.size <= block.size) {
			block.used = bump_offset + requirements.size;
			return { block.memory, bump_offset, requirements.size, memory_type, block.mapped };
		}
	}

	const VkDeviceSize block_size = std::max<VkDeviceSize>(
	    64ull * 1024ull * 1024ull,
	    (requirements.size + alignment - 1) & ~(alignment - 1));

	VkMemoryAllocateInfo allocate_info{};
	allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	allocate_info.allocationSize = block_size;
	allocate_info.memoryTypeIndex = memory_type;

	MemoryBlock block{};
	block.size = block_size;
	block.type = memory_type;
	checkVk(vkAllocateMemory(device_, &allocate_info, nullptr, &block.memory),
	        "allocate Vulkan memory pool");

	VkPhysicalDeviceMemoryProperties memory_properties{};
	vkGetPhysicalDeviceMemoryProperties(physical_device_, &memory_properties);
	const VkMemoryPropertyFlags actual_properties =
	    memory_properties.memoryTypes[memory_type].propertyFlags;
	if ((actual_properties & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0) {
		checkVk(vkMapMemory(device_, block.memory, 0, VK_WHOLE_SIZE, 0, &block.mapped),
		        "persistently map Vulkan host memory pool");
	}

	block.used = requirements.size;
	memory_blocks_.push_back(block);
	return { block.memory, 0, requirements.size, memory_type, block.mapped };
}

void VulkanDevice::freeMemory(const DeviceMemoryAllocation &allocation) noexcept {
	if (allocation.memory == VK_NULL_HANDLE || allocation.size == 0) return;
	for (auto &block : memory_blocks_) {
		if (block.memory != allocation.memory) continue;
		block.free_ranges.push_back({allocation.offset, allocation.size});
		std::sort(block.free_ranges.begin(), block.free_ranges.end(),
		          [](const MemoryBlock::FreeRange &left, const MemoryBlock::FreeRange &right) {
			          return left.offset < right.offset;
		          });
		std::vector<MemoryBlock::FreeRange> merged;
		for (const auto &range : block.free_ranges) {
			if (!merged.empty() && merged.back().offset + merged.back().size >= range.offset) {
				const VkDeviceSize end = std::max(merged.back().offset + merged.back().size,
				                                  range.offset + range.size);
				merged.back().size = end - merged.back().offset;
			} else {
				merged.push_back(range);
			}
		}
		block.free_ranges = std::move(merged);
		while (!block.free_ranges.empty()) {
			const auto &tail = block.free_ranges.back();
			if (tail.offset + tail.size != block.used) break;
			block.used = tail.offset;
			block.free_ranges.pop_back();
		}
		return;
	}
}

void *VulkanDevice::mapMemory(const DeviceMemoryAllocation &allocation) const {
	for (const auto &block : memory_blocks_) {
		if (block.memory == allocation.memory && block.mapped != nullptr) {
			return static_cast<char *>(block.mapped) + allocation.offset;
		}
	}

	void *mapped = nullptr;
	if (vkMapMemory(device_, allocation.memory, allocation.offset, allocation.size, 0, &mapped) != VK_SUCCESS) {
		return nullptr;
	}
	return mapped;
}


} // namespace oxide
