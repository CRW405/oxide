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
// The target always renders into a color image that can be sampled afterwards.
// Enable depth when the offscreen scene needs depth testing. imageUsage adds
// extra usage beyond COLOR_ATTACHMENT, SAMPLED, and TRANSFER_SRC, which are
// included automatically for rendering and PNG readback.
// ============================================================================
struct OffscreenConfig {
	std::uint32_t        width       = 0;
	std::uint32_t        height      = 0;
	VkFormat             colorFormat = VK_FORMAT_R8G8B8A8_SRGB;
	VkImageUsageFlags    imageUsage  = 0;
	bool                 depth       = false;
	VkFilter             filter      = VK_FILTER_LINEAR;
	VkSamplerAddressMode addressMode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
};

// ============================================================================
// Offscreen render target
//
// Owns a color image, optional depth image, render pass, framebuffer, and
// sampler. The color image finishes every render pass in
// SHADER_READ_ONLY_OPTIMAL, so it can be sampled by a later pass without a
// manual barrier. The same class works with a windowed VulkanContext or with a
// headless VulkanDevice.
//
// Record the target's render pass into any command buffer, then either sample
// colorView()/sampler() or call readPixels()/savePng() for CPU access.
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
	[[nodiscard]] VkFormat colorFormat() const noexcept { return config_.colorFormat; }
	[[nodiscard]] VkImage colorImage() const noexcept {
		const auto *resource = device_.resources().image(color_image_);
		return resource == nullptr ? VK_NULL_HANDLE : resource->image;
	}
	[[nodiscard]] VkImageView colorView() const noexcept {
		const auto *resource = device_.resources().image(color_image_);
		return resource == nullptr ? VK_NULL_HANDLE : resource->view;
	}
	[[nodiscard]] VkSampler sampler() const noexcept {
		const auto *resource = device_.resources().image(color_image_);
		return resource == nullptr ? VK_NULL_HANDLE : resource->sampler;
	}
	[[nodiscard]] bool hasDepth() const noexcept { return depth_image_.valid(); }
	[[nodiscard]] const OffscreenConfig &config() const noexcept { return config_; }

	// Begin and end the offscreen render pass, and set the matching dynamic
	// viewport and scissor. The clear color applies when the pass begins.
	void beginRenderPass(
	    VkCommandBuffer          commandBuffer,
	    std::array<float, 4>     clearColor = { 0.0f, 0.0f, 0.0f, 1.0f }) const;
	void endRenderPass(VkCommandBuffer commandBuffer) const;
	void setViewportAndScissor(VkCommandBuffer commandBuffer) const;

	// Copy the color attachment into tightly packed RGBA8 pixels. The image is
	// returned to SHADER_READ_ONLY_OPTIMAL before the call finishes, so it can
	// still be sampled afterwards. BGRA formats are swizzled to RGBA.
	[[nodiscard]] std::vector<std::uint8_t> readPixels() const;

	// Render the current color attachment to a PNG file using stb_image_write,
	// which is a required build dependency.
	void savePng(const std::string &path) const;

  private:
	void createColorResources();
	void createDepthResources();
	void createRenderPass();
	void createFramebuffer();
	void createSampler();
	void transitionToShaderRead();
	void releaseImage(ImageHandle &handle) noexcept;
	void destroyResources() noexcept;

	[[nodiscard]] VkFormat findDepthFormat() const;

	VulkanDevice &device_;
	OffscreenConfig config_;

	ImageHandle       color_image_;
	ImageHandle       depth_image_;
	RenderPassHandle  render_pass_;
	FramebufferHandle framebuffer_;
	VkFormat          depth_format_ = VK_FORMAT_UNDEFINED;
};

} // namespace oxide
