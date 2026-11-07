#include <oxide/vulkan/resources/image_texture.hpp>
#include <oxide/vulkan/resources/buffer.hpp>
#include <oxide/tools/log.hpp>
#include <oxide/vulkan/core/vk_error.hpp>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include <cstring>
#include <stdexcept>
#include <cmath>
#include <algorithm>

namespace oxide {

void transitionImageLayout(VulkanDevice &device, VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout) {
	VkCommandBuffer commandBuffer = device.beginSingleTimeCommands();

	VkImageMemoryBarrier barrier{};
	barrier.sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	barrier.oldLayout                       = oldLayout;
	barrier.newLayout                       = newLayout;
	barrier.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
	barrier.image                           = image;
	barrier.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
	barrier.subresourceRange.baseMipLevel   = 0;
	barrier.subresourceRange.levelCount     = VK_REMAINING_MIP_LEVELS;
	barrier.subresourceRange.baseArrayLayer = 0;
	barrier.subresourceRange.layerCount     = 1;

	VkPipelineStageFlags sourceStage;
	VkPipelineStageFlags destinationStage;

	if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
		barrier.srcAccessMask = 0;
		barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

		sourceStage      = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
		destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
	} else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
		barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

		sourceStage      = VK_PIPELINE_STAGE_TRANSFER_BIT;
		destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
	} else {
		throw invalidArgument("unsupported image layout transition (" +
		                      std::to_string(static_cast<int>(oldLayout)) + " -> " +
		                      std::to_string(static_cast<int>(newLayout)) + ")");
	}

	vkCmdPipelineBarrier(commandBuffer,
	                     sourceStage,
	                     destinationStage,
	                     0,
	                     0,
	                     nullptr,
	                     0,
	                     nullptr,
	                     1,
	                     &barrier);

	device.endSingleTimeCommands(commandBuffer);
}

void copyBufferToImage(VulkanDevice &device, VkBuffer buffer, VkImage image, uint32_t width, uint32_t height) {
	VkCommandBuffer commandBuffer = device.beginSingleTimeCommands();

	VkBufferImageCopy region{};
	region.bufferOffset                    = 0;
	region.bufferRowLength                 = 0;
	region.bufferImageHeight               = 0;
	region.imageSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
	region.imageSubresource.mipLevel       = 0;
	region.imageSubresource.baseArrayLayer = 0;
	region.imageSubresource.layerCount     = 1;
	region.imageOffset                     = { 0, 0, 0 };
	region.imageExtent                     = { width, height, 1 };

	vkCmdCopyBufferToImage(commandBuffer,
	                       buffer,
	                       image,
	                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
	                       1,
	                       &region);

	device.endSingleTimeCommands(commandBuffer);
}

ImageTexture::ImageTexture(VulkanDevice &device, const std::string &imagePath, TextureConfig config) : device_(device) {
	VkImage image = VK_NULL_HANDLE;
	VkDeviceMemory image_memory = VK_NULL_HANDLE;
	VkDeviceSize image_memory_offset = 0;
	VkImageView image_view = VK_NULL_HANDLE;
	VkSampler sampler = VK_NULL_HANDLE;
	int width = 0;
	int height = 0;
	int channels = 0;
	stbi_uc *pixels = stbi_load(imagePath.c_str(), &width, &height, &channels, STBI_rgb_alpha);
	if (pixels == nullptr) {
		const char *reason = stbi_failure_reason();
		throw runtimeError("failed to load texture image '" + imagePath + "'" +
		                   (reason != nullptr ? ": " + std::string(reason) : ""));
	}
	if (width <= 0 || height <= 0) {
		stbi_image_free(pixels);
		throw runtimeError("texture image has invalid dimensions '" + imagePath + "' (" +
		                   std::to_string(width) + "x" + std::to_string(height) + ")");
	}

	const VkDeviceSize imageSize = static_cast<VkDeviceSize>(width) *
	                               static_cast<VkDeviceSize>(height) * 4;

	try {
		Buffer stagingBuffer(device_, pixels, imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
		stbi_image_free(pixels);
		pixels = nullptr;

		VkImageCreateInfo imageInfo{};
		imageInfo.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		imageInfo.imageType     = VK_IMAGE_TYPE_2D;
		imageInfo.extent.width  = static_cast<uint32_t>(width);
		imageInfo.extent.height = static_cast<uint32_t>(height);
		imageInfo.extent.depth  = 1;
		VkFormatProperties format_properties{};
		vkGetPhysicalDeviceFormatProperties(device_.physicalDevice(), config.format, &format_properties);
		const bool can_blit = (format_properties.optimalTilingFeatures &
		                       (VK_FORMAT_FEATURE_BLIT_SRC_BIT | VK_FORMAT_FEATURE_BLIT_DST_BIT)) ==
		                      (VK_FORMAT_FEATURE_BLIT_SRC_BIT | VK_FORMAT_FEATURE_BLIT_DST_BIT);
		mip_levels_ = can_blit ? static_cast<std::uint32_t>(std::floor(std::log2(std::max(width, height)))) + 1 : 1;
		imageInfo.mipLevels     = mip_levels_;
		imageInfo.arrayLayers   = 1;
		imageInfo.format        = config.format;
		imageInfo.tiling        = VK_IMAGE_TILING_OPTIMAL;
		imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		imageInfo.usage         = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
		                          (mip_levels_ > 1 ? VK_IMAGE_USAGE_TRANSFER_SRC_BIT : 0);
		imageInfo.samples       = VK_SAMPLE_COUNT_1_BIT;
		imageInfo.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
		checkVk(vkCreateImage(device_.device(), &imageInfo, nullptr, &image), "create texture image");

		VkMemoryRequirements memRequirements;
		vkGetImageMemoryRequirements(device_.device(), image, &memRequirements);
		const DeviceMemoryAllocation allocation =
		    device_.allocateMemory(memRequirements, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		image_memory = allocation.memory;
		image_memory_offset = allocation.offset;
		image_memory = allocation.memory;
		handle_ = device_.resources().registerImage(
		    { image, image_memory, image_memory_offset, memRequirements.size, VK_NULL_HANDLE, VK_NULL_HANDLE });
		checkVk(vkBindImageMemory(device_.device(), image, image_memory, image_memory_offset), "bind texture image memory");

		transitionImageLayout(device_, image, VK_IMAGE_LAYOUT_UNDEFINED,
		                      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
		copyBufferToImage(device_,
		                  stagingBuffer.buffer(),
		                  image,
		                  static_cast<uint32_t>(width),
		                  static_cast<uint32_t>(height));
		// Generate the complete mip chain with linear blits when supported.
		VkCommandBuffer command = device_.beginSingleTimeCommands();
		VkImageMemoryBarrier barrier{};
		barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		barrier.image = image;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		barrier.subresourceRange.baseArrayLayer = 0;
		barrier.subresourceRange.layerCount = 1;
		int mip_width = width, mip_height = height;
		for (std::uint32_t level = 1; level < mip_levels_; ++level) {
			barrier.subresourceRange.baseMipLevel = level - 1;
			barrier.subresourceRange.levelCount = 1;
			barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
			vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
			                     0, nullptr, 0, nullptr, 1, &barrier);
			VkImageBlit blit{};
			blit.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, level - 1, 0, 1 };
			blit.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, level, 0, 1 };
			blit.srcOffsets[1] = { mip_width, mip_height, 1 };
			mip_width = std::max(1, mip_width / 2);
			mip_height = std::max(1, mip_height / 2);
			blit.dstOffsets[1] = { mip_width, mip_height, 1 };
			vkCmdBlitImage(command, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			               image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit,
			               VK_FILTER_LINEAR);
		}
		barrier.subresourceRange.baseMipLevel = mip_levels_ - 1;
		barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
		                     0, nullptr, 0, nullptr, 1, &barrier);
		for (std::uint32_t level = 0; level + 1 < mip_levels_; ++level) {
			barrier.subresourceRange.baseMipLevel = level;
			barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
			barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
			vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
			                     0, nullptr, 0, nullptr, 1, &barrier);
		}
		device_.endSingleTimeCommands(command);

		// create image view
		VkImageViewCreateInfo viewInfo{};
		viewInfo.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		viewInfo.image                           = image;
		viewInfo.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
		viewInfo.format                          = config.format;
		viewInfo.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
		viewInfo.subresourceRange.baseMipLevel   = 0;
		viewInfo.subresourceRange.levelCount     = mip_levels_;
		viewInfo.subresourceRange.baseArrayLayer = 0;
		viewInfo.subresourceRange.layerCount     = 1;
		checkVk(vkCreateImageView(device_.device(), &viewInfo, nullptr, &image_view),
		        "create texture image view");
		device_.resources().image(handle_)->view = image_view;

		// create sampler so that the shader can sample from the texture
		VkSamplerCreateInfo samplerInfo{};
		samplerInfo.sType                   = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
		samplerInfo.magFilter               = config.magFilter;
		samplerInfo.minFilter               = config.minFilter;
		samplerInfo.addressModeU            = config.addressMode;
		samplerInfo.addressModeV            = config.addressMode;
		samplerInfo.addressModeW            = config.addressMode;
		samplerInfo.anisotropyEnable        = config.anisotropy && device_.features().samplerAnisotropy;
		samplerInfo.maxAnisotropy           = samplerInfo.anisotropyEnable ? 16.0f : 1.0f;
		samplerInfo.borderColor             = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
		samplerInfo.unnormalizedCoordinates = VK_FALSE;
		samplerInfo.compareEnable           = VK_FALSE;
		samplerInfo.compareOp               = VK_COMPARE_OP_ALWAYS;
		samplerInfo.mipmapMode              = VK_SAMPLER_MIPMAP_MODE_LINEAR;
		samplerInfo.minLod                  = 0.0f;
		samplerInfo.maxLod                  = static_cast<float>(mip_levels_);
		checkVk(vkCreateSampler(device_.device(), &samplerInfo, nullptr, &sampler),
		        "create texture sampler");
		device_.resources().image(handle_)->sampler = sampler;
	} catch (...) {
		if (pixels != nullptr) {
			stbi_image_free(pixels);
		}
		destroyResources();
		throw;
	}
}

ImageTexture::~ImageTexture() {
	destroyResources();
}

void ImageTexture::destroyResources() noexcept {
	if (device_.device() == VK_NULL_HANDLE) {
		return;
	}
	const auto *resource = device_.resources().image(handle_);
	const DeviceMemoryAllocation allocation =
	    resource == nullptr ? DeviceMemoryAllocation{} :
	                          DeviceMemoryAllocation{resource->memory, resource->offset, resource->size, 0, nullptr};
	device_.resources().unregisterImage(handle_);
	handle_ = {};
	if (allocation.memory != VK_NULL_HANDLE) {
		device_.freeMemory(allocation);
	}
}

} // namespace oxide
