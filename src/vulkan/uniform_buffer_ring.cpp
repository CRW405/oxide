#include <oxide/vulkan/uniform_buffer_ring.hpp>
#include <oxide/tools/log.hpp>

#include <string>

namespace oxide {

UniformBufferRing::UniformBufferRing(
    VulkanDevice       &device,
    std::uint32_t       frameCount,
    VkDeviceSize        size,
    const void         *initialData,
    std::uint32_t       binding,
    VkShaderStageFlags  stageFlags)
    : device_(device),
      layout_(device, DescriptorBinding{ binding, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, stageFlags }),
      pool_(device, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, frameCount, frameCount),
      binding_(binding),
      size_(size) {
	if (initialData == nullptr || size_ == 0) {
		throw invalidArgument("uniform buffer ring requires non-empty initial data");
	}

	buffers_.reserve(frameCount);
	sets_.reserve(frameCount);
	for (std::uint32_t index = 0; index < frameCount; ++index) {
		buffers_.push_back(std::make_unique<UniformBuffer>(device_, initialData, size_));
		sets_.push_back(pool_.allocate(layout_.handle()));
	}

	for (std::uint32_t index = 0; index < frameCount; ++index) {
		VkDescriptorBufferInfo bufferInfo{};
		bufferInfo.buffer = buffers_[index]->buffer();
		bufferInfo.offset = 0;
		bufferInfo.range  = size_;

		VkWriteDescriptorSet write{};
		write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		write.dstSet          = sets_[index];
		write.dstBinding      = binding_;
		write.dstArrayElement = 0;
		write.descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		write.descriptorCount = 1;
		write.pBufferInfo     = &bufferInfo;

		vkUpdateDescriptorSets(device_.device(), 1, &write, 0, nullptr);
	}
}

UniformBufferRing::~UniformBufferRing() = default;

VkDescriptorSet UniformBufferRing::descriptorSet(std::uint32_t frameIndex) const {
	checkFrameIndex(frameIndex);
	return sets_[frameIndex];
}

void UniformBufferRing::update(std::uint32_t frameIndex, const void *data, VkDeviceSize size) {
	checkFrameIndex(frameIndex);
	buffers_[frameIndex]->update(data, size);
}

void UniformBufferRing::bind(
    VkCommandBuffer     commandBuffer,
    VkPipelineLayout    pipelineLayout,
    std::uint32_t       frameIndex,
    std::uint32_t       firstSet,
    VkPipelineBindPoint bindPoint) const {
	const VkDescriptorSet set = descriptorSet(frameIndex);
	vkCmdBindDescriptorSets(
	    commandBuffer,
	    bindPoint,
	    pipelineLayout,
	    firstSet,
	    1,
	    &set,
	    0,
	    nullptr);
}

void UniformBufferRing::checkFrameIndex(std::uint32_t frameIndex) const {
	if (frameIndex >= buffers_.size()) {
		throw outOfRange("uniform buffer frame index " + std::to_string(frameIndex) +
		                 " is out of range (" + std::to_string(buffers_.size()) + " frames)");
	}
}

} // namespace oxide
