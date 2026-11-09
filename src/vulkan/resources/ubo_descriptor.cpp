#include <oxide/vulkan/resources/ubo_descriptor.hpp>
#include <oxide/vulkan/resources/buffer.hpp>
#include <oxide/tools/log.hpp>

namespace oxide {

namespace {

VkDescriptorSetLayout requireLayout(VkDescriptorSetLayout layout) {
	if (layout == VK_NULL_HANDLE) {
		throw invalidArgument("uniform buffer descriptor set requires a valid descriptor set layout");
	}
	return layout;
}

} // namespace

UBODescriptorSet::UBODescriptorSet(
    VulkanDevice       &device,
    const Buffer       &buffer,
    std::uint32_t       binding,
    VkShaderStageFlags  stageFlags)
    : owned_layout_(std::make_unique<DescriptorSetLayout>(
          device, DescriptorBinding{ binding, DEFAULT_DESCRIPTOR_TYPE, stageFlags })),
      layout_handle_(owned_layout_->handle()),
      pool_(device, DEFAULT_DESCRIPTOR_TYPE),
      set_(pool_, layout_handle_) {
	writeDescriptor(device, buffer, binding);
}

UBODescriptorSet::UBODescriptorSet(
    VulkanDevice        &device,
    const Buffer        &buffer,
    VkDescriptorSetLayout layout,
    std::uint32_t        binding)
    : layout_handle_(requireLayout(layout)),
      pool_(device, DEFAULT_DESCRIPTOR_TYPE),
      set_(pool_, layout_handle_) {
	writeDescriptor(device, buffer, binding);
}

void UBODescriptorSet::writeDescriptor(
    VulkanDevice &device,
    const Buffer &buffer,
    std::uint32_t binding) {
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
