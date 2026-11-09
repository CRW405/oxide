#include <oxide/vulkan/resources/texture_descriptor.hpp>
#include <oxide/vulkan/resources/image_texture.hpp>
#include <oxide/tools/log.hpp>

namespace oxide {

namespace {

VkDescriptorSetLayout requireLayout(VkDescriptorSetLayout layout) {
	if (layout == VK_NULL_HANDLE) {
		throw invalidArgument("texture descriptor set requires a valid descriptor set layout");
	}
	return layout;
}

} // namespace

TextureDescriptorSet::TextureDescriptorSet(
    VulkanDevice       &device,
    const ImageTexture &texture,
    std::uint32_t       binding,
    VkShaderStageFlags  stageFlags)
    : TextureDescriptorSet(device, texture.imageView(), texture.sampler(), binding, stageFlags) {}

TextureDescriptorSet::TextureDescriptorSet(
    VulkanDevice       &device,
    VkImageView         imageView,
    VkSampler           sampler,
    std::uint32_t       binding,
    VkShaderStageFlags  stageFlags)
    : owned_layout_(std::make_unique<DescriptorSetLayout>(
          device, DescriptorBinding{ binding, DEFAULT_DESCRIPTOR_TYPE, stageFlags })),
      layout_handle_(owned_layout_->handle()),
      pool_(device, DEFAULT_DESCRIPTOR_TYPE),
      set_(pool_, layout_handle_) {
	writeDescriptor(device, imageView, sampler, binding);
}

TextureDescriptorSet::TextureDescriptorSet(
    VulkanDevice        &device,
    const ImageTexture  &texture,
    VkDescriptorSetLayout layout,
    std::uint32_t        binding)
    : TextureDescriptorSet(device, texture.imageView(), texture.sampler(), layout, binding) {}

TextureDescriptorSet::TextureDescriptorSet(
    VulkanDevice        &device,
    VkImageView          imageView,
    VkSampler            sampler,
    VkDescriptorSetLayout layout,
    std::uint32_t        binding)
    : layout_handle_(requireLayout(layout)),
      pool_(device, DEFAULT_DESCRIPTOR_TYPE),
      set_(pool_, layout_handle_) {
	writeDescriptor(device, imageView, sampler, binding);
}

void TextureDescriptorSet::writeDescriptor(
    VulkanDevice &device,
    VkImageView   imageView,
    VkSampler     sampler,
    std::uint32_t binding) {
	VkDescriptorImageInfo imageInfo{};
	imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	imageInfo.imageView   = imageView;
	imageInfo.sampler     = sampler;

	VkWriteDescriptorSet descriptorWrite{};
	descriptorWrite.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	descriptorWrite.dstSet          = set_.handle();
	descriptorWrite.dstBinding      = binding;
	descriptorWrite.dstArrayElement = 0;
	descriptorWrite.descriptorType  = DEFAULT_DESCRIPTOR_TYPE;
	descriptorWrite.descriptorCount = 1;
	descriptorWrite.pImageInfo      = &imageInfo;

	vkUpdateDescriptorSets(device.device(), 1, &descriptorWrite, 0, nullptr);
}

} // namespace oxide
