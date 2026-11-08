#pragma once

#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include <oxide/vulkan/device/vulkan_device.hpp>

namespace oxide {

// ============================================================================
// Offscreen render target configuration
//
// The target renders into one or more color images that can be sampled
// afterwards. Set colorFormats for multiple render targets (MRT); when it is
// empty, colorFormat describes the single color attachment. Enable depth when
// the offscreen scene needs depth testing.
//
// A sampleCount above VK_SAMPLE_COUNT_1_BIT makes every color and depth image
// multisampled. Each color attachment is automatically resolved into a
// single-sample image that can be sampled or read back.
//
// imageUsage adds extra usage beyond COLOR_ATTACHMENT, SAMPLED, and TRANSFER_SRC,
// which are included automatically for rendering and PNG readback.
// ============================================================================
struct OffscreenConfig {
	std::uint32_t         width       = 0;
	std::uint32_t         height      = 0;
	VkFormat              colorFormat = VK_FORMAT_R8G8B8A8_SRGB;

	// Optional MRT format list. When non-empty it replaces colorFormat and
	// defines one color attachment per entry.
	std::vector<VkFormat> colorFormats;

	VkImageUsageFlags     imageUsage  = 0;
	bool                  depth       = false;
	VkFilter              filter      = VK_FILTER_LINEAR;
	VkSamplerAddressMode  addressMode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	VkSampleCountFlagBits sampleCount = VK_SAMPLE_COUNT_1_BIT;
};

// ============================================================================
// Offscreen render target
//
// Owns one or more color images, optional depth image, render pass, framebuffer,
// and a sampler per resolved color image. Every color attachment finishes each
// render pass in SHADER_READ_ONLY_OPTIMAL, so it can be sampled by a later pass
// without a manual barrier.
//
// With sampleCount > 1 the framebuffer uses the multisampled images as color
// attachments and resolves them into single-sample images. colorImage(),
// colorView(), and sampler() always refer to the resolved, sampleable image.
// attachmentImage()/attachmentView() expose the multisampled attachment.
//
// The same class works with a windowed VulkanContext or with a headless
// VulkanDevice. Record the target's render pass into any command buffer, then
// either sample colorView(index)/sampler(index) or call readPixels()/savePng()
// for CPU access.
// ============================================================================
class OffscreenTarget {
  public:
	OffscreenTarget(VulkanDevice &device, const OffscreenConfig &config);
	~OffscreenTarget();

	OffscreenTarget(const OffscreenTarget &)            = delete;
	OffscreenTarget &operator=(const OffscreenTarget &) = delete;
	OffscreenTarget(OffscreenTarget &&)                 = delete;
	OffscreenTarget &operator=(OffscreenTarget &&)      = delete;

	[[nodiscard]] VkRenderPass renderPass() const noexcept {
		const auto *resource = device_.resources().renderPass(render_pass_);
		return resource == nullptr ? VK_NULL_HANDLE : resource->render_pass;
	}
	[[nodiscard]] VkFramebuffer framebuffer() const noexcept {
		const auto *resource = device_.resources().framebuffer(framebuffer_);
		return resource == nullptr ? VK_NULL_HANDLE : resource->framebuffer;
	}
	[[nodiscard]] VkExtent2D extent() const noexcept {
		return { config_.width, config_.height };
	}

	[[nodiscard]] std::uint32_t colorCount() const noexcept {
		return static_cast<std::uint32_t>(color_formats_.size());
	}
	[[nodiscard]] VkFormat colorFormat(std::uint32_t index = 0) const noexcept;
	[[nodiscard]] VkSampleCountFlagBits sampleCount() const noexcept { return config_.sampleCount; }
	[[nodiscard]] bool isMultisampled() const noexcept {
		return config_.sampleCount != VK_SAMPLE_COUNT_1_BIT;
	}

	// Resolved, single-sample image resources. These are the images a later
	// pass should sample. With no MSAA they are the render attachments
	// themselves.
	[[nodiscard]] VkImage  colorImage(std::uint32_t index = 0) const noexcept;
	[[nodiscard]] VkImageView colorView(std::uint32_t index = 0) const noexcept;
	[[nodiscard]] VkSampler   sampler(std::uint32_t index = 0) const noexcept;

	// The actual color attachment used by the framebuffer. With MSAA this is
	// the multisampled image; otherwise it is the same as colorImage().
	[[nodiscard]] VkImage     attachmentImage(std::uint32_t index = 0) const noexcept;
	[[nodiscard]] VkImageView attachmentView(std::uint32_t index = 0) const noexcept;

	// The explicit resolve image when MSAA is active, otherwise the regular
	// single-sample color image.
	[[nodiscard]] VkImage     resolveImage(std::uint32_t index = 0) const noexcept;
	[[nodiscard]] VkImageView resolveView(std::uint32_t index = 0) const noexcept;

	[[nodiscard]] bool                   hasDepth() const noexcept { return depth_image_.valid(); }
	[[nodiscard]] const OffscreenConfig &config() const noexcept { return config_; }

	// Begin and end the offscreen render pass, and set the matching dynamic
	// viewport and scissor. The single-color overload applies the clear color to
	// every color attachment. The vector overload supplies one clear color per
	// color attachment.
	void beginRenderPass(
	    VkCommandBuffer      commandBuffer,
	    std::array<float, 4> clearColor = { 0.0f, 0.0f, 0.0f, 1.0f }) const;
	void beginRenderPass(
	    VkCommandBuffer                              commandBuffer,
	    const std::vector<std::array<float, 4>>     &clearColors) const;
	void endRenderPass(VkCommandBuffer commandBuffer) const;
	void setViewportAndScissor(VkCommandBuffer commandBuffer) const;

	// Copy a resolved color attachment into tightly packed RGBA8 pixels. The
	// image is returned to SHADER_READ_ONLY_OPTIMAL before the call finishes, so
	// it can still be sampled afterwards. BGRA formats are swizzled to RGBA.
	[[nodiscard]] std::vector<std::uint8_t> readPixels(std::uint32_t attachment = 0) const;

	// Render a resolved color attachment to a PNG file using stb_image_write,
	// which is a required build dependency.
	void savePng(const std::string &path, std::uint32_t attachment = 0) const;

  private:
	[[nodiscard]] const ImageResource *imageResource(const ImageHandle &handle) const noexcept;
	[[nodiscard]] VkImage              imageFor(const ImageHandle &handle) const noexcept;
	[[nodiscard]] VkImageView          viewFor(const ImageHandle &handle) const noexcept;
	[[nodiscard]] VkSampler            samplerFor(const ImageHandle &handle) const noexcept;
	[[nodiscard]] const ImageHandle   &resolvedHandle(std::uint32_t index) const;

	void createColorResources();
	void createDepthResources();
	void createRenderPass();
	void createFramebuffer();
	void createSamplers();
	void createSamplerFor(ImageHandle &image);
	void transitionToShaderRead();
	void releaseImage(ImageHandle &handle) noexcept;
	void destroyResources() noexcept;

	[[nodiscard]] ImageHandle createImage(
	    VkFormat              format,
	    VkSampleCountFlagBits sampleCount,
	    VkImageUsageFlags     usage,
	    VkImageAspectFlags    aspectMask,
	    const char           *label);
	[[nodiscard]] VkFormat findDepthFormat() const;

	VulkanDevice   &device_;
	OffscreenConfig config_;
	std::vector<VkFormat>   color_formats_;
	std::vector<ImageHandle> color_images_;
	std::vector<ImageHandle> resolve_images_;
	ImageHandle       depth_image_;
	RenderPassHandle  render_pass_;
	FramebufferHandle framebuffer_;
	VkFormat          depth_format_ = VK_FORMAT_UNDEFINED;
};

} // namespace oxide
