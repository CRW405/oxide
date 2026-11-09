#include <oxide/vulkan/resources/uniform_buffer_ring.hpp>
#include <oxide/tools/log.hpp>

#include <string>

namespace oxide {

namespace {

VkDescriptorSetLayout requireLayout(VkDescriptorSetLayout layout) {
	if (layout == VK_NULL_HANDLE) {
		throw invalidArgument("uniform buffer ring requires a valid descriptor set layout");
	}
	return layout;
}

} // namespace

UniformBufferRing::UniformBufferRing(
    VulkanDevice       &device,
    std::uint32_t       frameCount,
    VkDeviceSize        size,
    const void         *initialData,
    std::uint32_t       binding,
    VkShaderStageFlags  stageFlags)
    : device_(device),
      owned_layout_(std::make_unique<DescriptorSetLayout>(
          device, DescriptorBinding{ binding, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, stageFlags })),
      layout_handle_(owned_layout_->handle()),
      pool_(device, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, frameCount, frameCount),
      binding_(binding),
      size_(size),
      frame_count_(frameCount) {
	initialize(initialData);
}

UniformBufferRing::UniformBufferRing(
    VulkanDevice        &device,
    std::uint32_t        frameCount,
    VkDeviceSize         size,
    const void          *initialData,
    VkDescriptorSetLayout layout,
    std::uint32_t        binding)
    : device_(device),
      layout_handle_(requireLayout(layout)),
      pool_(device, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, frameCount, frameCount),
      binding_(binding),
      size_(size),
      frame_count_(frameCount) {
	initialize(initialData);
}

void UniformBufferRing::initialize(const void *initialData) {
	if (initialData == nullptr || size_ == 0) {
		throw invalidArgument("uniform buffer ring requires non-empty initial data");
	}
	if (pool_.handle() == VK_NULL_HANDLE || frame_count_ == 0) {
		throw invalidArgument("uniform buffer ring requires at least one frame in flight");
	}

	buffers_.reserve(frame_count_);
	sets_.reserve(frame_count_);
	for (std::uint32_t index = 0; index < frame_count_; ++index) {
		buffers_.push_back(std::make_unique<UniformBuffer>(device_, initialData, size_));
		sets_.push_back(pool_.allocate(layout_handle_));
	}

	for (std::uint32_t index = 0; index < frame_count_; ++index) {
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
