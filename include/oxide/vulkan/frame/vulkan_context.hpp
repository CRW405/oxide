#pragma once

#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

#include <oxide/glfw/glfw_oxide.hpp>
#include <oxide/vulkan/device/vulkan_device.hpp>

namespace oxide {

constexpr std::uint32_t MAX_FRAMES_IN_FLIGHT = 2;

struct Frame {
	VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
	std::uint32_t   imageIndex    = 0;
	std::uint32_t   frameIndex    = 0;
};

// ============================================================================
// Swapchain recreation notification
//
// Describes the swapchain state after a successful recreation. When
// renderPassChanged is true the old render pass was destroyed, so every
// pipeline created against it must be recreated before it is bound again.
// ============================================================================
struct SwapchainRecreationInfo {
	VkRenderPass  renderPass        = VK_NULL_HANDLE;
	VkFormat      format            = VK_FORMAT_UNDEFINED;
	VkExtent2D    extent{};
	bool          renderPassChanged = false;
	std::uint64_t generation        = 0;
};

using SwapchainRecreatedCallback = std::function<void(const SwapchainRecreationInfo &)>;
using SwapchainCallbackId        = std::uint64_t;

class VulkanContext;

class FrameScope {
  public:
	FrameScope(VulkanContext &context, Frame frame, std::array<float, 4> clearColor);
	~FrameScope();

	FrameScope(const FrameScope &)            = delete;
	FrameScope &operator=(const FrameScope &) = delete;
	FrameScope(FrameScope &&other) noexcept;
	FrameScope &operator=(FrameScope &&other) noexcept;

	[[nodiscard]] explicit operator bool() const noexcept {
		return frame_.commandBuffer != VK_NULL_HANDLE;
	}
	[[nodiscard]] VkCommandBuffer commandBuffer() const noexcept { return frame_.commandBuffer; }
	[[nodiscard]] std::uint32_t frameIndex() const noexcept { return frame_.frameIndex; }
	[[nodiscard]] std::uint32_t imageIndex() const noexcept { return frame_.imageIndex; }
	[[nodiscard]] const Frame &frame() const noexcept { return frame_; }

  private:
	VulkanContext *context_ = nullptr;
	Frame          frame_{};
};

// ============================================================================
// Swapchain configuration
//
// Leaves a field unset to keep the automatic choice. Requested values are
// checked against the surface capabilities; an unsupported value logs a warning
// and falls back so a window can still be created on every platform.
// ============================================================================
struct SwapchainConfig {
	// Desired format and color space. Unset prefers B8G8R8A8_SRGB with the
	// nonlinear sRGB color space. Set both fields together to request HDR or a
	// different working format.
	std::optional<VkSurfaceFormatKHR> surfaceFormat;

	// Unset prefers MAILBOX when available, then FIFO.
	std::optional<VkPresentModeKHR> presentMode;

	// Extra usage beyond rendering to the window, for example
	// VK_IMAGE_USAGE_TRANSFER_SRC_BIT for screenshots. COLOR_ATTACHMENT is
	// always included because VulkanContext renders to the swapchain.
	VkImageUsageFlags imageUsage = 0;

	// Zero chooses minImageCount + 1, clamped to the surface limits.
	std::uint32_t imageCount = 0;

	// MSAA sample count for window rendering. When greater than one,
	// VulkanContext renders into a multisampled color and depth image and
	// resolves the color image into the swapchain image. Unsupported values log
	// a warning and fall back to VK_SAMPLE_COUNT_1_BIT.
	VkSampleCountFlagBits sampleCount = VK_SAMPLE_COUNT_1_BIT;
};

// ============================================================================
// Window-owned Vulkan rendering resources
//
// Owns the presentation resources for one window. The device is shared, while
// the surface, swapchain, render pass, framebuffers, and frame synchronization
// belong to this context.
// ============================================================================
class VulkanContext {
  public:
	explicit VulkanContext(
	    VulkanDevice          &device,
	    Window                *window,
	    const SwapchainConfig &config = {});
	~VulkanContext();

	VulkanContext(const VulkanContext &)            = delete;
	VulkanContext &operator=(const VulkanContext &) = delete;
	VulkanContext(VulkanContext &&)                 = delete;
	VulkanContext &operator=(VulkanContext &&)      = delete;

	[[nodiscard]] VkRenderPass                    renderPass() const noexcept { return render_pass_; }
	[[nodiscard]] VkExtent2D                      extent() const noexcept { return swapchain_extent_; }
	[[nodiscard]] VkFormat                        swapchainFormat() const noexcept { return swapchain_format_; }
	[[nodiscard]] VkFormat                        depthFormat() const noexcept { return depth_format_; }
	[[nodiscard]] VkSampleCountFlagBits           sampleCount() const noexcept { return sample_count_; }
	[[nodiscard]] VkPresentModeKHR                swapchainPresentMode() const noexcept {
		return swapchain_present_mode_;
	}
	[[nodiscard]] VkImageUsageFlags swapchainImageUsage() const noexcept { return swapchain_image_usage_; }

	// Increments after every successful swapchain recreation. Store the value
	// used when a pipeline was built and compare it to detect stale pipelines.
	[[nodiscard]] std::uint64_t swapchainGeneration() const noexcept { return swapchain_generation_; }

	// Called after the swapchain and its dependent resources have been rebuilt.
	// Recreate graphics pipelines here when info.renderPassChanged is true.
	void setSwapchainRecreatedCallback(SwapchainRecreatedCallback callback);

	// Appends a callback and returns an id that can be passed to
	// removeSwapchainRecreatedCallback(). Removing a callback during
	// notification is safe because the callback list is copied first.
	[[nodiscard]] SwapchainCallbackId addSwapchainRecreatedCallback(SwapchainRecreatedCallback callback);
	bool                             removeSwapchainRecreatedCallback(SwapchainCallbackId id) noexcept;
	[[nodiscard]] const std::vector<VkImage>     &swapchainImages() const noexcept { return swapchain_images_; }
	[[nodiscard]] const std::vector<VkImageView> &swapchainImageViews() const noexcept {
		return swapchain_image_views_;
	}
	[[nodiscard]] VkFramebuffer framebuffer(std::uint32_t index) const;

	[[nodiscard]] Frame beginFrame();
	[[nodiscard]] FrameScope beginFrameScope(std::array<float, 4> clearColor);
	void                endFrame(const Frame &frame);
	void                beginRenderPass(const Frame &frame, std::array<float, 4> clearColor);
	void                endRenderPass(const Frame &frame);
	void                setViewportAndScissor(VkCommandBuffer commandBuffer) const;
	void                recreateSwapchain();
	void                recoverSurface();
	void                notifyFramebufferResized() { framebuffer_resized_ = true; }

  private:
	struct SwapchainSupportDetails {
		VkSurfaceCapabilitiesKHR        capabilities{};
		std::vector<VkSurfaceFormatKHR> formats;
		std::vector<VkPresentModeKHR>   presentModes;
	};

	void createSurface();
	void createSwapchain();
	void createColorResources();
	void createDepthResources();
	void createRenderPass();
	void createFramebuffers();
	void createCommandResources();

	void destroySwapchain(bool destroy_render_pass = true);
	void destroyColorResources() noexcept;
	void destroyDepthResources() noexcept;
	void destroyWindowResources() noexcept;
	void notifySwapchainRecreated(bool renderPassChanged);

	// Returns false when the window is closing before a usable size appears.
	[[nodiscard]] bool waitForValidFramebufferSize() const;

	[[nodiscard]] VkFormat                findDepthFormat(VkSampleCountFlagBits sampleCount) const;
	[[nodiscard]] SwapchainSupportDetails querySwapchainSupport(VkPhysicalDevice device, VkSurfaceKHR surface) const;

	VulkanDevice    &device_;
	Window          *window_;
	SwapchainConfig  swapchain_config_;

	VkSurfaceKHR                                      surface_          = VK_NULL_HANDLE;
	VkSwapchainKHR                                    swapchain_        = VK_NULL_HANDLE;
	VkFormat                                          swapchain_format_ = VK_FORMAT_UNDEFINED;
	VkFormat                                          depth_format_     = VK_FORMAT_UNDEFINED;
	VkPresentModeKHR                                  swapchain_present_mode_ = VK_PRESENT_MODE_FIFO_KHR;
	VkImageUsageFlags                                 swapchain_image_usage_  = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	VkExtent2D                                        swapchain_extent_{};
	std::vector<VkImage>                              swapchain_images_;
	std::vector<VkImageView>                          swapchain_image_views_;
	std::vector<VkFramebuffer>                        swapchain_framebuffers_;
	// One attachment set per swapchain image. Reusing a single depth or MSAA
	// color image across frames in flight would let two submissions write the
	// same attachment at once, so the framebuffer for each image owns its own.
	struct AttachmentImage {
		VkImage        image        = VK_NULL_HANDLE;
		VkDeviceMemory memory       = VK_NULL_HANDLE;
		VkDeviceSize   memoryOffset = 0;
		VkDeviceSize   memorySize   = 0;
		VkImageView    view         = VK_NULL_HANDLE;
	};

	std::vector<AttachmentImage> color_images_;
	std::vector<AttachmentImage> depth_images_;
	VkSampleCountFlagBits                             sample_count_       = VK_SAMPLE_COUNT_1_BIT;
	VkRenderPass                                      render_pass_  = VK_NULL_HANDLE;
	VkCommandPool                                     command_pool_ = VK_NULL_HANDLE;
	std::array<VkCommandBuffer, MAX_FRAMES_IN_FLIGHT> command_buffers_{};
	std::array<VkSemaphore, MAX_FRAMES_IN_FLIGHT>     image_available_{};
	std::vector<VkSemaphore>                          render_finished_;
	std::array<VkFence, MAX_FRAMES_IN_FLIGHT>         in_flight_{};
	std::vector<VkFence>                              images_in_flight_;

	std::uint32_t              current_frame_        = 0;
	std::uint64_t              swapchain_generation_ = 0;
	bool                       framebuffer_resized_  = false;
	bool                       transparent_          = false;
	SwapchainRecreatedCallback swapchain_recreated_callback_;

	struct SwapchainCallbackEntry {
		SwapchainCallbackId       id = 0;
		SwapchainRecreatedCallback callback;
	};

	std::vector<SwapchainCallbackEntry> additional_swapchain_callbacks_;
	SwapchainCallbackId                 next_swapchain_callback_id_ = 1;
};

}
