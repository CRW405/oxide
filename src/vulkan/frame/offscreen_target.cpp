#include <oxide/tools/log.hpp>
#include <oxide/vulkan/core/vk_error.hpp>
#include <oxide/vulkan/frame/offscreen_target.hpp>
#include <oxide/vulkan/resources/buffer.hpp>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_WRITE_STATIC
#include <stb_image_write.h>

#include <algorithm>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace oxide {

namespace {

bool hasStencilComponent(VkFormat format) noexcept {
	return format == VK_FORMAT_D32_SFLOAT_S8_UINT || format == VK_FORMAT_D24_UNORM_S8_UINT;
}

bool isBgraFormat(VkFormat format) noexcept {
	return format == VK_FORMAT_B8G8R8A8_UNORM || format == VK_FORMAT_B8G8R8A8_SRGB;
}

bool isSupportedReadbackFormat(VkFormat format) noexcept {
	return format == VK_FORMAT_R8G8B8A8_UNORM || format == VK_FORMAT_R8G8B8A8_SRGB ||
	       format == VK_FORMAT_B8G8R8A8_UNORM || format == VK_FORMAT_B8G8R8A8_SRGB;
}

void imageBarrier(
    VkCommandBuffer      commandBuffer,
    VkImage              image,
    VkImageLayout        oldLayout,
    VkImageLayout        newLayout,
    VkAccessFlags        sourceAccess,
    VkAccessFlags        destinationAccess,
    VkPipelineStageFlags sourceStage,
    VkPipelineStageFlags destinationStage) {
	VkImageMemoryBarrier barrier{};
	barrier.sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	barrier.oldLayout                       = oldLayout;
	barrier.newLayout                       = newLayout;
	barrier.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
	barrier.image                           = image;
	barrier.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
	barrier.subresourceRange.baseMipLevel   = 0;
	barrier.subresourceRange.levelCount     = 1;
	barrier.subresourceRange.baseArrayLayer = 0;
	barrier.subresourceRange.layerCount     = 1;
	barrier.srcAccessMask                   = sourceAccess;
	barrier.dstAccessMask                   = destinationAccess;

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
}

} // namespace

OffscreenTarget::OffscreenTarget(VulkanDevice &device, const OffscreenConfig &config)
    : device_(device), config_(config) {
	if (config_.width == 0 || config_.height == 0) {
		throw invalidArgument("offscreen target requires non-zero width and height");
	}
	if (config_.sampleCount == 0) {
		throw invalidArgument("offscreen target requires a valid sample count");
	}

	color_formats_ = config_.colorFormats;
	if (color_formats_.empty()) {
		color_formats_.push_back(config_.colorFormat);
	}
	for (VkFormat format : color_formats_) {
		if (format == VK_FORMAT_UNDEFINED) {
			throw invalidArgument("offscreen target requires a color format");
		}
	}

	VkPhysicalDeviceProperties properties{};
	vkGetPhysicalDeviceProperties(device_.physicalDevice(), &properties);
	if (color_formats_.size() > properties.limits.maxColorAttachments) {
		throw invalidArgument("offscreen target requests " + std::to_string(color_formats_.size()) +
		                      " color attachments but the device supports at most " +
		                      std::to_string(properties.limits.maxColorAttachments));
	}

	try {
		createColorResources();
		if (config_.depth) {
			createDepthResources();
		}
		createRenderPass();
		createFramebuffer();
		createSamplers();
		transitionToShaderRead();
	} catch (...) {
		destroyResources();
		throw;
	}
}

OffscreenTarget::~OffscreenTarget() {
	destroyResources();
}

const ImageResource *OffscreenTarget::imageResource(const ImageHandle &handle) const noexcept {
	return device_.resources().image(handle);
}

VkImage OffscreenTarget::imageFor(const ImageHandle &handle) const noexcept {
	const auto *resource = imageResource(handle);
	return resource == nullptr ? VK_NULL_HANDLE : resource->image;
}

VkImageView OffscreenTarget::viewFor(const ImageHandle &handle) const noexcept {
	const auto *resource = imageResource(handle);
	return resource == nullptr ? VK_NULL_HANDLE : resource->view;
}

VkSampler OffscreenTarget::samplerFor(const ImageHandle &handle) const noexcept {
	const auto *resource = imageResource(handle);
	return resource == nullptr ? VK_NULL_HANDLE : resource->sampler;
}

const ImageHandle &OffscreenTarget::resolvedHandle(std::uint32_t index) const {
	if (!resolve_images_.empty()) {
		return resolve_images_[index];
	}
	return color_images_[index];
}

VkFormat OffscreenTarget::colorFormat(std::uint32_t index) const noexcept {
	return index < color_formats_.size() ? color_formats_[index] : VK_FORMAT_UNDEFINED;
}

VkImage OffscreenTarget::colorImage(std::uint32_t index) const noexcept {
	if (index >= colorCount()) {
		return VK_NULL_HANDLE;
	}
	return imageFor(resolvedHandle(index));
}

VkImageView OffscreenTarget::colorView(std::uint32_t index) const noexcept {
	if (index >= colorCount()) {
		return VK_NULL_HANDLE;
	}
	return viewFor(resolvedHandle(index));
}

VkSampler OffscreenTarget::sampler(std::uint32_t index) const noexcept {
	if (index >= colorCount()) {
		return VK_NULL_HANDLE;
	}
	return samplerFor(resolvedHandle(index));
}

VkImage OffscreenTarget::attachmentImage(std::uint32_t index) const noexcept {
	if (index >= color_images_.size()) {
		return VK_NULL_HANDLE;
	}
	return imageFor(color_images_[index]);
}

VkImageView OffscreenTarget::attachmentView(std::uint32_t index) const noexcept {
	if (index >= color_images_.size()) {
		return VK_NULL_HANDLE;
	}
	return viewFor(color_images_[index]);
}

VkImage OffscreenTarget::resolveImage(std::uint32_t index) const noexcept {
	if (index >= colorCount()) {
		return VK_NULL_HANDLE;
	}
	return imageFor(resolvedHandle(index));
}

VkImageView OffscreenTarget::resolveView(std::uint32_t index) const noexcept {
	if (index >= colorCount()) {
		return VK_NULL_HANDLE;
	}
	return viewFor(resolvedHandle(index));
}

ImageHandle OffscreenTarget::createImage(
    VkFormat              format,
    VkSampleCountFlagBits sampleCount,
    VkImageUsageFlags     usage,
    VkImageAspectFlags    aspectMask,
    const char           *label) {
	VkImageCreateInfo image_info{};
	image_info.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	image_info.imageType     = VK_IMAGE_TYPE_2D;
	image_info.format        = format;
	image_info.extent        = { config_.width, config_.height, 1 };
	image_info.mipLevels     = 1;
	image_info.arrayLayers   = 1;
	image_info.samples       = sampleCount;
	image_info.tiling        = VK_IMAGE_TILING_OPTIMAL;
	image_info.usage         = usage;
	image_info.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
	image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

	const std::string description = std::string("offscreen ") + label;
	VkImage           image       = VK_NULL_HANDLE;
	checkVk(vkCreateImage(device_.device(), &image_info, nullptr, &image),
	        "create " + description + " image");

	ImageHandle handle{};
	try {
		handle = device_.resources().registerImage(
		    { image, VK_NULL_HANDLE, 0, 0, VK_NULL_HANDLE, VK_NULL_HANDLE });
	} catch (...) {
		vkDestroyImage(device_.device(), image, nullptr);
		throw;
	}

	try {
		ImageResource *resource = device_.resources().image(handle);
		if (resource == nullptr) {
			throw runtimeError(description + " resource is missing");
		}

		VkMemoryRequirements requirements{};
		vkGetImageMemoryRequirements(device_.device(), image, &requirements);
		const DeviceMemoryAllocation allocation =
		    device_.allocateMemory(requirements, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

		resource->memory = allocation.memory;
		resource->offset = allocation.offset;
		resource->size   = allocation.size;
		checkVk(vkBindImageMemory(device_.device(), image, allocation.memory, allocation.offset),
		        "bind " + description + " image memory");

		VkImageViewCreateInfo view_info{};
		view_info.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		view_info.image                           = image;
		view_info.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
		view_info.format                          = format;
		view_info.subresourceRange.aspectMask     = aspectMask;
		view_info.subresourceRange.baseMipLevel   = 0;
		view_info.subresourceRange.levelCount     = 1;
		view_info.subresourceRange.baseArrayLayer = 0;
		view_info.subresourceRange.layerCount     = 1;

		VkImageView view = VK_NULL_HANDLE;
		checkVk(vkCreateImageView(device_.device(), &view_info, nullptr, &view),
		        "create " + description + " image view");
		resource->view = view;
	} catch (...) {
		releaseImage(handle);
		throw;
	}
	return handle;
}

void OffscreenTarget::createColorResources() {
	const VkImageUsageFlags sampled_usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
	                                        VK_IMAGE_USAGE_SAMPLED_BIT |
	                                        VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
	                                        config_.imageUsage;
	const VkFormatFeatureFlags required = VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT |
	                                      VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
	                                      VK_FORMAT_FEATURE_TRANSFER_SRC_BIT;

	color_images_.reserve(color_formats_.size());
	if (isMultisampled()) {
		resolve_images_.reserve(color_formats_.size());
	}

	for (VkFormat format : color_formats_) {
		VkFormatProperties properties{};
		vkGetPhysicalDeviceFormatProperties(device_.physicalDevice(), format, &properties);
		if ((properties.optimalTilingFeatures & required) != required) {
			throw runtimeError(
			    "offscreen color format does not support color attachment, sampling, and transfer source");
		}

		if (isMultisampled()) {
			if (!device_.supportsSampleCount(format, config_.sampleCount, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)) {
				throw runtimeError("offscreen color format does not support the requested sample count " +
				                   std::to_string(static_cast<int>(config_.sampleCount)));
			}
			color_images_.push_back(createImage(format,
			                                    config_.sampleCount,
			                                    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
			                                    VK_IMAGE_ASPECT_COLOR_BIT,
			                                    "multisample color"));
			resolve_images_.push_back(createImage(format,
			                                      VK_SAMPLE_COUNT_1_BIT,
			                                      sampled_usage,
			                                      VK_IMAGE_ASPECT_COLOR_BIT,
			                                      "resolve color"));
		} else {
			color_images_.push_back(createImage(format,
			                                    VK_SAMPLE_COUNT_1_BIT,
			                                    sampled_usage,
			                                    VK_IMAGE_ASPECT_COLOR_BIT,
			                                    "color"));
		}
	}
}

VkFormat OffscreenTarget::findDepthFormat() const {
	constexpr VkFormat candidates[] = {
		VK_FORMAT_D32_SFLOAT,
		VK_FORMAT_D32_SFLOAT_S8_UINT,
		VK_FORMAT_D24_UNORM_S8_UINT,
	};

	for (VkFormat format : candidates) {
		if (!device_.supportsSampleCount(format, config_.sampleCount,
		                                 VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)) {
			continue;
		}
		VkFormatProperties properties{};
		vkGetPhysicalDeviceFormatProperties(device_.physicalDevice(), format, &properties);
		if ((properties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0) {
			return format;
		}
	}
	throw runtimeError("no supported depth-stencil format found for offscreen target");
}

void OffscreenTarget::createDepthResources() {
	depth_format_ = findDepthFormat();
	VkImageAspectFlags aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
	if (hasStencilComponent(depth_format_)) {
		aspect |= VK_IMAGE_ASPECT_STENCIL_BIT;
	}
	depth_image_ = createImage(depth_format_,
	                           config_.sampleCount,
	                           VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
	                           aspect,
	                           "depth");
}

void OffscreenTarget::createRenderPass() {
	// The resolved color images begin and end in the sampled layout. That makes
	// every completed offscreen pass immediately sampleable by a later pass.
	std::vector<VkAttachmentDescription> attachments;
	std::vector<VkAttachmentReference>   color_references;
	std::vector<VkAttachmentReference>   resolve_references;

	attachments.reserve(color_formats_.size() * 2 + (hasDepth() ? 1 : 0));
	color_references.reserve(color_formats_.size());
	if (isMultisampled()) {
		resolve_references.reserve(color_formats_.size());
	}

	for (VkFormat format : color_formats_) {
		VkAttachmentDescription color_attachment{};
		color_attachment.format         = format;
		color_attachment.samples        = config_.sampleCount;
		color_attachment.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
		color_attachment.storeOp        = isMultisampled() ? VK_ATTACHMENT_STORE_OP_DONT_CARE
		                                                   : VK_ATTACHMENT_STORE_OP_STORE;
		color_attachment.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		color_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		color_attachment.initialLayout  = isMultisampled() ? VK_IMAGE_LAYOUT_UNDEFINED
		                                                   : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		color_attachment.finalLayout    = isMultisampled() ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
		                                                   : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		VkAttachmentReference color_reference{};
		color_reference.attachment = static_cast<std::uint32_t>(attachments.size());
		color_reference.layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		attachments.push_back(color_attachment);
		color_references.push_back(color_reference);

		if (isMultisampled()) {
			VkAttachmentDescription resolve_attachment{};
			resolve_attachment.format         = format;
			resolve_attachment.samples        = VK_SAMPLE_COUNT_1_BIT;
			resolve_attachment.loadOp         = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			resolve_attachment.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
			resolve_attachment.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			resolve_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
			resolve_attachment.initialLayout  = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			resolve_attachment.finalLayout    = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

			VkAttachmentReference resolve_reference{};
			resolve_reference.attachment = static_cast<std::uint32_t>(attachments.size());
			resolve_reference.layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			attachments.push_back(resolve_attachment);
			resolve_references.push_back(resolve_reference);
		}
	}

	VkAttachmentReference depth_reference{};
	if (hasDepth()) {
		VkAttachmentDescription depth_attachment{};
		depth_attachment.format         = depth_format_;
		depth_attachment.samples        = config_.sampleCount;
		depth_attachment.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
		depth_attachment.storeOp        = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		depth_attachment.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		depth_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		depth_attachment.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
		depth_attachment.finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

		depth_reference.attachment = static_cast<std::uint32_t>(attachments.size());
		depth_reference.layout     = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		attachments.push_back(depth_attachment);
	}

	VkSubpassDescription subpass{};
	subpass.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.colorAttachmentCount    = static_cast<std::uint32_t>(color_references.size());
	subpass.pColorAttachments       = color_references.data();
	subpass.pResolveAttachments     = isMultisampled() ? resolve_references.data() : nullptr;
	subpass.pDepthStencilAttachment = hasDepth() ? &depth_reference : nullptr;

	// Wait for any earlier sampling or transfer read of a color image before
	// clearing it, and for earlier depth writes before reusing the depth image.
	VkSubpassDependency begin_dependency{};
	begin_dependency.srcSubpass    = VK_SUBPASS_EXTERNAL;
	begin_dependency.dstSubpass    = 0;
	begin_dependency.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
	                                 VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
	                                 VK_PIPELINE_STAGE_TRANSFER_BIT |
	                                 VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
	                                 VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
	begin_dependency.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
	                                 VK_ACCESS_SHADER_READ_BIT |
	                                 VK_ACCESS_TRANSFER_READ_BIT |
	                                 VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
	begin_dependency.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
	                                 VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
	                                 VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
	begin_dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
	                                 VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
	                                 VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

	// Make the completed color writes and resolves visible to a later sampled or
	// transfer read. The final layout transition to SHADER_READ_ONLY is part of
	// this.
	VkSubpassDependency end_dependency{};
	end_dependency.srcSubpass    = 0;
	end_dependency.dstSubpass    = VK_SUBPASS_EXTERNAL;
	end_dependency.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	end_dependency.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	end_dependency.dstStageMask  = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
	                               VK_PIPELINE_STAGE_TRANSFER_BIT;
	end_dependency.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT;

	const VkSubpassDependency dependencies[] = { begin_dependency, end_dependency };

	VkRenderPassCreateInfo render_pass_info{};
	render_pass_info.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	render_pass_info.attachmentCount = static_cast<std::uint32_t>(attachments.size());
	render_pass_info.pAttachments    = attachments.data();
	render_pass_info.subpassCount    = 1;
	render_pass_info.pSubpasses      = &subpass;
	render_pass_info.dependencyCount = 2;
	render_pass_info.pDependencies   = dependencies;
	VkRenderPass render_pass         = VK_NULL_HANDLE;
	checkVk(vkCreateRenderPass(device_.device(), &render_pass_info, nullptr, &render_pass),
	        "create offscreen render pass");
	try {
		render_pass_ = device_.resources().registerRenderPass({ render_pass });
	} catch (...) {
		vkDestroyRenderPass(device_.device(), render_pass, nullptr);
		throw;
	}
}

void OffscreenTarget::createFramebuffer() {
	std::vector<VkImageView> attachments;
	attachments.reserve(color_formats_.size() * 2 + (hasDepth() ? 1 : 0));

	for (std::uint32_t index = 0; index < colorCount(); ++index) {
		attachments.push_back(attachmentView(index));
		if (isMultisampled()) {
			attachments.push_back(resolveView(index));
		}
	}
	if (hasDepth()) {
		attachments.push_back(viewFor(depth_image_));
	}

	VkFramebufferCreateInfo info{};
	info.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
	info.renderPass      = renderPass();
	info.attachmentCount = static_cast<std::uint32_t>(attachments.size());
	info.pAttachments    = attachments.data();
	info.width           = config_.width;
	info.height          = config_.height;
	info.layers          = 1;

	VkFramebuffer framebuffer = VK_NULL_HANDLE;
	checkVk(vkCreateFramebuffer(device_.device(), &info, nullptr, &framebuffer),
	        "create offscreen framebuffer");
	try {
		framebuffer_ = device_.resources().registerFramebuffer({ framebuffer });
	} catch (...) {
		vkDestroyFramebuffer(device_.device(), framebuffer, nullptr);
		throw;
	}
}

void OffscreenTarget::createSamplerFor(ImageHandle &image) {
	VkSamplerCreateInfo info{};
	info.sType                   = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	info.magFilter               = config_.filter;
	info.minFilter               = config_.filter;
	info.addressModeU            = config_.addressMode;
	info.addressModeV            = config_.addressMode;
	info.addressModeW            = config_.addressMode;
	info.anisotropyEnable        = VK_FALSE;
	info.maxAnisotropy           = 1.0f;
	info.borderColor             = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
	info.unnormalizedCoordinates = VK_FALSE;
	info.compareEnable           = VK_FALSE;
	info.compareOp               = VK_COMPARE_OP_ALWAYS;
	info.mipmapMode              = VK_SAMPLER_MIPMAP_MODE_LINEAR;
	info.minLod                  = 0.0f;
	info.maxLod                  = 0.0f;

	VkSampler sampler = VK_NULL_HANDLE;
	checkVk(vkCreateSampler(device_.device(), &info, nullptr, &sampler),
	        "create offscreen sampler");

	ImageResource *resource = device_.resources().image(image);
	if (resource == nullptr) {
		vkDestroySampler(device_.device(), sampler, nullptr);
		throw runtimeError("offscreen color image resource is missing");
	}
	resource->sampler = sampler;
}

void OffscreenTarget::createSamplers() {
	// Every resolved image gets its own sampler. With MSAA these are the resolve
	// images; otherwise they are the color attachments themselves.
	if (isMultisampled()) {
		for (ImageHandle &image : resolve_images_) {
			createSamplerFor(image);
		}
	} else {
		for (ImageHandle &image : color_images_) {
			createSamplerFor(image);
		}
	}
}

void OffscreenTarget::transitionToShaderRead() {
	// A fresh resolve image cannot enter the render pass until it is in the
	// layout the render pass expects for its initial layout.
	VkCommandBuffer command_buffer = device_.beginSingleTimeCommands();
	for (std::uint32_t index = 0; index < colorCount(); ++index) {
		imageBarrier(command_buffer,
		             colorImage(index),
		             VK_IMAGE_LAYOUT_UNDEFINED,
		             VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		             0,
		             VK_ACCESS_SHADER_READ_BIT,
		             VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
		             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
	}
	device_.endSingleTimeCommands(command_buffer);
}

void OffscreenTarget::beginRenderPass(
    VkCommandBuffer      commandBuffer,
    std::array<float, 4> clearColor) const {
	const std::vector<std::array<float, 4>> clear_colors(colorCount(), clearColor);
	beginRenderPass(commandBuffer, clear_colors);
}

void OffscreenTarget::beginRenderPass(
    VkCommandBuffer                          commandBuffer,
    const std::vector<std::array<float, 4>> &clearColors) const {
	if (commandBuffer == VK_NULL_HANDLE) {
		return;
	}

	// Clear values are indexed by attachment, including resolve attachments
	// whose load op ignores the value. Keeping the array attachment-aligned
	// ensures the depth clear lands on the depth attachment even with MSAA.
	std::vector<VkClearValue> clear_values;
	clear_values.reserve(colorCount() * (isMultisampled() ? 2 : 1) + (hasDepth() ? 1 : 0));

	const std::array<float, 4> fallback = { 0.0f, 0.0f, 0.0f, 1.0f };
	for (std::uint32_t index = 0; index < colorCount(); ++index) {
		const std::array<float, 4> &source = index < clearColors.size() ? clearColors[index] : fallback;
		VkClearValue                color{};
		std::copy(source.begin(), source.end(), color.color.float32);
		clear_values.push_back(color);
		if (isMultisampled()) {
			clear_values.push_back(VkClearValue{});
		}
	}
	if (hasDepth()) {
		VkClearValue depth{};
		depth.depthStencil = { 1.0f, 0 };
		clear_values.push_back(depth);
	}

	VkRenderPassBeginInfo info{};
	info.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	info.renderPass        = renderPass();
	info.framebuffer       = framebuffer();
	info.renderArea.offset = { 0, 0 };
	info.renderArea.extent = extent();
	info.clearValueCount   = static_cast<std::uint32_t>(clear_values.size());
	info.pClearValues      = clear_values.data();
	vkCmdBeginRenderPass(commandBuffer, &info, VK_SUBPASS_CONTENTS_INLINE);
}

void OffscreenTarget::endRenderPass(VkCommandBuffer commandBuffer) const {
	if (commandBuffer != VK_NULL_HANDLE) {
		vkCmdEndRenderPass(commandBuffer);
	}
}

void OffscreenTarget::setViewportAndScissor(VkCommandBuffer commandBuffer) const {
	if (commandBuffer == VK_NULL_HANDLE) {
		return;
	}

	VkViewport viewport{};
	viewport.width    = static_cast<float>(config_.width);
	viewport.height   = static_cast<float>(config_.height);
	viewport.maxDepth = 1.0f;
	vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

	VkRect2D scissor{};
	scissor.extent = extent();
	vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
}

std::vector<std::uint8_t> OffscreenTarget::readPixels(std::uint32_t attachment) const {
	if (attachment >= colorCount()) {
		throw outOfRange("offscreen color attachment index " + std::to_string(attachment) +
		                 " is out of range (" + std::to_string(colorCount()) + " attachments)");
	}
	const VkFormat format = color_formats_[attachment];
	if (!isSupportedReadbackFormat(format)) {
		throw runtimeError("offscreen readback supports only 8-bit RGBA or BGRA color formats");
	}

	const VkDeviceSize pixel_count =
	    static_cast<VkDeviceSize>(config_.width) * static_cast<VkDeviceSize>(config_.height);
	const VkDeviceSize byte_count = pixel_count * 4;

	std::vector<std::uint8_t> initial(static_cast<std::size_t>(byte_count), 0);
	Buffer                    readback(device_, initial.data(), byte_count, VK_BUFFER_USAGE_TRANSFER_DST_BIT);

	const VkImage image = colorImage(attachment);
	VkCommandBuffer command_buffer = device_.beginSingleTimeCommands();
	imageBarrier(command_buffer,
	             image,
	             VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
	             VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
	             VK_ACCESS_SHADER_READ_BIT,
	             VK_ACCESS_TRANSFER_READ_BIT,
	             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
	             VK_PIPELINE_STAGE_TRANSFER_BIT);

	VkBufferImageCopy region{};
	region.bufferOffset                    = 0;
	region.bufferRowLength                 = 0;
	region.bufferImageHeight               = 0;
	region.imageSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
	region.imageSubresource.mipLevel       = 0;
	region.imageSubresource.baseArrayLayer = 0;
	region.imageSubresource.layerCount     = 1;
	region.imageOffset                     = { 0, 0, 0 };
	region.imageExtent                     = { config_.width, config_.height, 1 };
	vkCmdCopyImageToBuffer(command_buffer,
	                       image,
	                       VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
	                       readback.buffer(),
	                       1,
	                       &region);

	imageBarrier(command_buffer,
	             image,
	             VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
	             VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
	             VK_ACCESS_TRANSFER_READ_BIT,
	             VK_ACCESS_SHADER_READ_BIT,
	             VK_PIPELINE_STAGE_TRANSFER_BIT,
	             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
	device_.endSingleTimeCommands(command_buffer);

	std::vector<std::uint8_t> pixels(static_cast<std::size_t>(byte_count));
	readback.read(pixels.data(), byte_count);

	// stb and most image tools expect RGBA byte order, while a BGRA target
	// stores the same channels in the opposite order.
	if (isBgraFormat(format)) {
		for (VkDeviceSize index = 0; index < pixel_count; ++index) {
			std::swap(pixels[index * 4 + 0], pixels[index * 4 + 2]);
		}
	}
	return pixels;
}

void OffscreenTarget::savePng(const std::string &path, std::uint32_t attachment) const {
	const std::vector<std::uint8_t> pixels = readPixels(attachment);
	if (stbi_write_png(path.c_str(),
	                   static_cast<int>(config_.width),
	                   static_cast<int>(config_.height),
	                   4,
	                   pixels.data(),
	                   static_cast<int>(config_.width * 4)) == 0) {
		throw runtimeError("failed to write PNG file '" + path + "'");
	}
}

void OffscreenTarget::releaseImage(ImageHandle &handle) noexcept {
	if (!handle.valid()) {
		return;
	}
	const auto                  *resource = device_.resources().image(handle);
	const DeviceMemoryAllocation allocation =
	    resource == nullptr ? DeviceMemoryAllocation{} :
	                          DeviceMemoryAllocation{ resource->memory, resource->offset, resource->size, 0, nullptr };
	device_.resources().unregisterImage(handle);
	handle = {};
	if (allocation.memory != VK_NULL_HANDLE) {
		device_.freeMemory(allocation);
	}
}

void OffscreenTarget::destroyResources() noexcept {
	if (device_.device() == VK_NULL_HANDLE) {
		return;
	}
	device_.waitIdleNoThrow();

	if (framebuffer_.valid()) {
		device_.resources().unregisterFramebuffer(framebuffer_);
		framebuffer_ = {};
	}
	if (render_pass_.valid()) {
		device_.resources().unregisterRenderPass(render_pass_);
		render_pass_ = {};
	}

	for (ImageHandle &image : color_images_) {
		releaseImage(image);
	}
	color_images_.clear();
	for (ImageHandle &image : resolve_images_) {
		releaseImage(image);
	}
	resolve_images_.clear();
	releaseImage(depth_image_);
	depth_format_ = VK_FORMAT_UNDEFINED;
}

} // namespace oxide
