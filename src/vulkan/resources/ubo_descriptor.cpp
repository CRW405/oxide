#include <oxide/vulkan/resources/ubo_descriptor.hpp>
#include <oxide/vulkan/resources/buffer.hpp>

namespace oxide {

UBODescriptorSet::UBODescriptorSet(
    VulkanDevice       &device,
    const Buffer       &buffer,
    std::uint32_t       binding,
    VkShaderStageFlags  stageFlags)
    : layout_(device, DescriptorBinding{ binding, DEFAULT_DESCRIPTOR_TYPE, stageFlags }),
      pool_(device, DEFAULT_DESCRIPTOR_TYPE),
      set_(pool_, layout_.handle()) {
	VkDescriptorBufferInfo bufferInfo{};
	bufferInfo.buffer = buffer.buffer();
	bufferInfo.offset = 0;
	bufferInfo.range  = buffer.size();

	VkWriteDescriptorSet descriptorWrite{};
	descriptorWrite.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	descriptorWrite.dstSet          = set_.handle();
	descriptorWrite.dstBinding      = binding;
	descriptorWrite.dstArrayElement = 0;
	descriptorWrite.descriptorType  = DEFAULT_DESCRIPTOR_TYPE;
	descriptorWrite.descriptorCount = 1;
	descriptorWrite.pBufferInfo     = &bufferInfo;

	vkUpdateDescriptorSets(device.device(), 1, &descriptorWrite, 0, nullptr);
}

} // namespace oxide
