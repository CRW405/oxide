#include <oxide/vulkan/resources/texture_descriptor.hpp>
#include <oxide/vulkan/resources/image_texture.hpp>

namespace oxide {

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
    : layout_(device, DescriptorBinding{ binding, DEFAULT_DESCRIPTOR_TYPE, stageFlags }),
      pool_(device, DEFAULT_DESCRIPTOR_TYPE),
      set_(pool_, layout_.handle()) {
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
