#include <oxide/vulkan/frame/vulkan_context.hpp>
#include <oxide/tools/log.hpp>
#include <oxide/vulkan/core/vk_error.hpp>

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace oxide {

namespace {

std::unordered_map<GLFWwindow *, VulkanContext *> window_contexts;

bool hasStencilComponent(VkFormat format) noexcept {
	return format == VK_FORMAT_D32_SFLOAT_S8_UINT || format == VK_FORMAT_D24_UNORM_S8_UINT;
}

void framebufferResizeCallback(GLFWwindow *window, int, int) {
	const auto context = window_contexts.find(window);
	if (context != window_contexts.end()) {
		context->second->notifyFramebufferResized();
	}
}

} // namespace

VulkanContext::VulkanContext(
    VulkanDevice          &device,
    Window                *window,
    const SwapchainConfig &config)
    : device_(device), window_(window), swapchain_config_(config) {
	if (window_ == nullptr || window_->handle() == nullptr) {
		throw invalidArgument("VulkanContext requires a valid window");
	}

	transparent_ = window_->transparent();

	window_contexts[window_->handle()] = this;
	glfwSetFramebufferSizeCallback(window_->handle(), framebufferResizeCallback);
	try {
		createSurface();
		createSwapchain();
		createDepthResources();
		createColorResources();
		createRenderPass();
		createFramebuffers();
		createCommandResources();
	} catch (...) {
		destroyWindowResources();
		if (surface_ != VK_NULL_HANDLE) {
			vkDestroySurfaceKHR(device_.instance(), surface_, nullptr);
			surface_ = VK_NULL_HANDLE;
		}
		window_contexts.erase(window_->handle());
		glfwSetFramebufferSizeCallback(window_->handle(), nullptr);
		throw;
	}
}

VulkanContext::~VulkanContext() {
	// Wait for GPU work before releasing resources referenced by command buffers.
	destroyWindowResources();
	if (surface_ != VK_NULL_HANDLE) {
		vkDestroySurfaceKHR(device_.instance(), surface_, nullptr);
	}
	if (window_ != nullptr && window_->handle() != nullptr) {
		const auto context = window_contexts.find(window_->handle());
		if (context != window_contexts.end() && context->second == this) {
			window_contexts.erase(context);
			glfwSetFramebufferSizeCallback(window_->handle(), nullptr);
		}
	}
}

void VulkanContext::createSurface() {
	// The surface connects the Vulkan instance to this window system window.
	checkVk(glfwCreateWindowSurface(device_.instance(), window_->handle(), nullptr, &surface_),
	        "create window surface");
}

VulkanContext::SwapchainSupportDetails VulkanContext::querySwapchainSupport(
    VkPhysicalDevice physical_device, VkSurfaceKHR surface) const {
	// Collect capabilities, formats, and present modes before choosing settings.
	SwapchainSupportDetails result{};
	checkVk(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device, surface, &result.capabilities),
	        "query surface capabilities");

	std::uint32_t count = 0;
	checkVk(vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface, &count, nullptr),
	        "query surface formats");
	result.formats.resize(count);
	if (count > 0) {
		checkVk(vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface, &count, result.formats.data()),
		        "query surface formats");
	}

	count = 0;
	checkVk(vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device, surface, &count, nullptr),
	        "query surface present modes");
	result.presentModes.resize(count);
	if (count > 0) {
		checkVk(vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device, surface, &count, result.presentModes.data()),
		        "query surface present modes");
	}
	return result;
}

void VulkanContext::createSwapchain() {
	const QueueFamilies queues = device_.findQueueFamilies(device_.physicalDevice(), surface_);
	if (!queues.hasGraphics || !queues.hasPresent) {
		throw runtimeError("Vulkan device cannot present to this window surface");
	}
	const SwapchainSupportDetails support = querySwapchainSupport(device_.physicalDevice(), surface_);
	if (support.formats.empty() || support.presentModes.empty()) {
		throw runtimeError("surface does not support a usable swapchain");
	}

	const bool format_is_undefined =
	    support.formats.size() == 1 && support.formats.front().format == VK_FORMAT_UNDEFINED;

	VkSurfaceFormatKHR surface_format{};
	bool               format_selected = false;
	if (swapchain_config_.surfaceFormat) {
		const VkSurfaceFormatKHR requested = *swapchain_config_.surfaceFormat;
		if (format_is_undefined) {
			// The surface has no preferred format, so the application may choose.
			surface_format  = requested;
			format_selected = true;
			OXIDE_LOG_INFO << "surface reports VK_FORMAT_UNDEFINED; using requested format "
			               << static_cast<int>(requested.format);
		} else {
			const auto requested_format = std::find_if(
			    support.formats.begin(), support.formats.end(), [&requested](const VkSurfaceFormatKHR &format) {
				    return format.format == requested.format && format.colorSpace == requested.colorSpace;
			    });
			if (requested_format != support.formats.end()) {
				surface_format  = *requested_format;
				format_selected = true;
			} else {
				OXIDE_LOG_WARNING << "requested swapchain format " << static_cast<int>(requested.format)
				                  << " with color space " << static_cast<int>(requested.colorSpace)
				                  << " is unavailable; falling back";
			}
		}
	}

	if (!format_selected) {
		const auto preferred_format = std::find_if(
		    support.formats.begin(), support.formats.end(), [](const VkSurfaceFormatKHR &format) {
			    return format.format == VK_FORMAT_B8G8R8A8_SRGB &&
			           format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
		    });
		if (preferred_format != support.formats.end()) {
			surface_format = *preferred_format;
		} else if (format_is_undefined) {
			surface_format = { VK_FORMAT_B8G8R8A8_SRGB, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR };
			OXIDE_LOG_INFO << "surface reports VK_FORMAT_UNDEFINED; using B8G8R8A8_SRGB";
		} else {
			surface_format = support.formats.front();
			OXIDE_LOG_WARNING << "preferred surface format B8G8R8A8_SRGB is unavailable; using format "
			                  << static_cast<int>(surface_format.format);
		}
	}

	VkPresentModeKHR present_mode = VK_PRESENT_MODE_FIFO_KHR;
	if (swapchain_config_.presentMode) {
		const VkPresentModeKHR requested = *swapchain_config_.presentMode;
		if (std::find(support.presentModes.begin(), support.presentModes.end(), requested) !=
		    support.presentModes.end()) {
			present_mode = requested;
		} else {
			OXIDE_LOG_WARNING << "requested present mode " << static_cast<int>(requested)
			                  << " is unavailable; using FIFO";
		}
	} else {
		const auto mailbox = std::find(support.presentModes.begin(), support.presentModes.end(),
		                               VK_PRESENT_MODE_MAILBOX_KHR);
		if (mailbox != support.presentModes.end()) {
			present_mode = VK_PRESENT_MODE_MAILBOX_KHR;
		} else {
			OXIDE_LOG_INFO << "mailbox present mode is unavailable; using FIFO";
		}
	}
	swapchain_present_mode_ = present_mode;

	// Rendering always needs the color attachment usage; other usages are
	// requested by the application when it needs a screenshot or copy source.
	VkImageUsageFlags image_usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | swapchain_config_.imageUsage;
	if ((support.capabilities.supportedUsageFlags & image_usage) != image_usage) {
		OXIDE_LOG_WARNING << "requested swapchain image usage " << image_usage
		                  << " is unsupported (" << support.capabilities.supportedUsageFlags
		                  << "); using COLOR_ATTACHMENT only";
		image_usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	}
	swapchain_image_usage_ = image_usage;

	// Wayland composites the surface using its alpha channel only when the
	// surface is not marked opaque. Prefer pre-multiplied alpha because it is
	// the common transparent mode; fall back to whatever the surface supports.
	VkCompositeAlphaFlagBitsKHR composite_alpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
	if (transparent_) {
		const VkCompositeAlphaFlagBitsKHR transparent_modes[] = {
			VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
			VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
			VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
		};
		bool found_transparent_mode = false;
		for (VkCompositeAlphaFlagBitsKHR mode : transparent_modes) {
			if ((support.capabilities.supportedCompositeAlpha & mode) != 0) {
				composite_alpha         = mode;
				found_transparent_mode = true;
				break;
			}
		}
		if (!found_transparent_mode) {
			OXIDE_LOG_WARNING << "transparent window requested but the surface supports no "
			                     "transparent composite alpha mode; falling back to opaque";
		}
	}

	if (support.capabilities.currentExtent.width != std::numeric_limits<std::uint32_t>::max()) {
		swapchain_extent_ = support.capabilities.currentExtent;
	} else {
		int width  = 0;
		int height = 0;
		glfwGetFramebufferSize(window_->handle(), &width, &height);
		swapchain_extent_ = {
			std::clamp(static_cast<std::uint32_t>(std::max(width, 0)), support.capabilities.minImageExtent.width,
			           support.capabilities.maxImageExtent.width),
			std::clamp(static_cast<std::uint32_t>(std::max(height, 0)), support.capabilities.minImageExtent.height,
			           support.capabilities.maxImageExtent.height)
		};
	}

	const bool    explicit_image_count = swapchain_config_.imageCount != 0;
	std::uint32_t image_count =
	    explicit_image_count ? swapchain_config_.imageCount : support.capabilities.minImageCount + 1;
	const std::uint32_t max_image_count = support.capabilities.maxImageCount == 0
	                                          ? std::numeric_limits<std::uint32_t>::max()
	                                          : support.capabilities.maxImageCount;
	const std::uint32_t clamped_image_count =
	    std::clamp(image_count, support.capabilities.minImageCount, max_image_count);
	if (clamped_image_count != image_count) {
		if (explicit_image_count) {
			OXIDE_LOG_WARNING << "requested swapchain image count " << image_count
			                  << " is outside the supported range [" << support.capabilities.minImageCount
			                  << ", " << max_image_count << "]; using " << clamped_image_count;
		}
		image_count = clamped_image_count;
	}

	const bool               separate_queues  = queues.graphics != queues.present;
	const std::uint32_t      queue_families[] = { queues.graphics, queues.present };
	VkSwapchainCreateInfoKHR info{};
	info.sType                 = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
	info.surface               = surface_;
	info.minImageCount         = image_count;
	info.imageFormat           = surface_format.format;
	info.imageColorSpace       = surface_format.colorSpace;
	info.imageExtent           = swapchain_extent_;
	info.imageArrayLayers      = 1;
	info.imageUsage            = image_usage;
	info.imageSharingMode      = separate_queues ? VK_SHARING_MODE_CONCURRENT : VK_SHARING_MODE_EXCLUSIVE;
	info.queueFamilyIndexCount = separate_queues ? 2 : 0;
	info.pQueueFamilyIndices   = separate_queues ? queue_families : nullptr;
	info.preTransform          = support.capabilities.currentTransform;
	info.compositeAlpha        = composite_alpha;
	info.presentMode           = present_mode;
	info.clipped               = VK_TRUE;
	checkVk(vkCreateSwapchainKHR(device_.device(), &info, nullptr, &swapchain_), "create swapchain");

	// Create one color view for every image owned by the swapchain.
	checkVk(vkGetSwapchainImagesKHR(device_.device(), swapchain_, &image_count, nullptr),
	        "query swapchain images");
	if (image_count == 0) {
		throw runtimeError("swapchain reported zero images");
	}
	swapchain_images_.resize(image_count);
	checkVk(vkGetSwapchainImagesKHR(device_.device(), swapchain_, &image_count, swapchain_images_.data()),
	        "query swapchain images");
	swapchain_format_ = surface_format.format;

	// MSAA is limited by the chosen swapchain format. Fall back early so the
	// color, depth, render pass, and pipeline sample counts stay consistent.
	sample_count_ = swapchain_config_.sampleCount;
	if (sample_count_ != VK_SAMPLE_COUNT_1_BIT &&
	    !device_.supportsSampleCount(swapchain_format_, sample_count_, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)) {
		OXIDE_LOG_WARNING << "requested sample count " << static_cast<int>(sample_count_)
		                  << " is unsupported for the swapchain format; using VK_SAMPLE_COUNT_1_BIT";
		sample_count_ = VK_SAMPLE_COUNT_1_BIT;
	}
	swapchain_image_views_.resize(image_count);

	for (std::uint32_t index = 0; index < image_count; ++index) {
		VkImageViewCreateInfo view{};
		view.sType                       = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		view.image                       = swapchain_images_[index];
		view.viewType                    = VK_IMAGE_VIEW_TYPE_2D;
		view.format                      = swapchain_format_;
		view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		view.subresourceRange.levelCount = 1;
		view.subresourceRange.layerCount = 1;
		checkVk(vkCreateImageView(device_.device(), &view, nullptr, &swapchain_image_views_[index]),
		        "create swapchain image view");
	}
}

VkFormat VulkanContext::findDepthFormat(VkSampleCountFlagBits sampleCount) const {
	// Depth-stencil formats are not guaranteed to support attachment usage, so
	// probe the preferred formats before committing to one.
	constexpr VkFormat candidates[] = {
		VK_FORMAT_D32_SFLOAT,
		VK_FORMAT_D32_SFLOAT_S8_UINT,
		VK_FORMAT_D24_UNORM_S8_UINT,
	};

	for (VkFormat format : candidates) {
		if (!device_.supportsSampleCount(format, sampleCount,
		                                 VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)) {
			continue;
		}
		VkFormatProperties properties{};
		vkGetPhysicalDeviceFormatProperties(device_.physicalDevice(), format, &properties);
		if ((properties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0) {
			return format;
		}
	}
	throw runtimeError("no supported depth-stencil format found");
}

void VulkanContext::createDepthResources() {
	try {
		depth_format_ = findDepthFormat(sample_count_);
	} catch (const std::exception &) {
		if (sample_count_ == VK_SAMPLE_COUNT_1_BIT) {
			throw;
		}
		OXIDE_LOG_WARNING << "requested sample count " << static_cast<int>(sample_count_)
		                  << " is unsupported for depth; using VK_SAMPLE_COUNT_1_BIT";
		sample_count_ = VK_SAMPLE_COUNT_1_BIT;
		depth_format_ = findDepthFormat(sample_count_);
	}

	depth_images_.resize(swapchain_images_.size());
	try {
		for (AttachmentImage &attachment : depth_images_) {
			VkImageCreateInfo image_info{};
			image_info.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
			image_info.imageType     = VK_IMAGE_TYPE_2D;
			image_info.format        = depth_format_;
			image_info.extent        = { swapchain_extent_.width, swapchain_extent_.height, 1 };
			image_info.mipLevels     = 1;
			image_info.arrayLayers   = 1;
			image_info.samples       = sample_count_;
			image_info.tiling        = VK_IMAGE_TILING_OPTIMAL;
			image_info.usage         = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
			image_info.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
			image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			checkVk(vkCreateImage(device_.device(), &image_info, nullptr, &attachment.image),
			        "create depth image");

			VkMemoryRequirements memory_requirements{};
			vkGetImageMemoryRequirements(device_.device(), attachment.image, &memory_requirements);

			const DeviceMemoryAllocation allocation =
			    device_.allocateMemory(memory_requirements, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
			attachment.memory       = allocation.memory;
			attachment.memoryOffset = allocation.offset;
			attachment.memorySize   = allocation.size;
			checkVk(vkBindImageMemory(device_.device(), attachment.image, attachment.memory,
			                          attachment.memoryOffset),
			        "bind depth image memory");

			VkImageViewCreateInfo view_info{};
			view_info.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
			view_info.image                           = attachment.image;
			view_info.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
			view_info.format                          = depth_format_;
			view_info.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_DEPTH_BIT;
			view_info.subresourceRange.baseMipLevel   = 0;
			view_info.subresourceRange.levelCount     = 1;
			view_info.subresourceRange.baseArrayLayer = 0;
			view_info.subresourceRange.layerCount     = 1;
			if (hasStencilComponent(depth_format_)) {
				view_info.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
			}
			checkVk(vkCreateImageView(device_.device(), &view_info, nullptr, &attachment.view),
			        "create depth image view");
		}
	} catch (...) {
		destroyDepthResources();
		throw;
	}
}

void VulkanContext::destroyDepthResources() noexcept {
	if (device_.device() == VK_NULL_HANDLE) {
		return;
	}
	for (AttachmentImage &attachment : depth_images_) {
		if (attachment.view != VK_NULL_HANDLE) {
			vkDestroyImageView(device_.device(), attachment.view, nullptr);
		}
		if (attachment.image != VK_NULL_HANDLE) {
			vkDestroyImage(device_.device(), attachment.image, nullptr);
		}
		if (attachment.memory != VK_NULL_HANDLE) {
			device_.freeMemory({ attachment.memory, attachment.memoryOffset, attachment.memorySize, 0, nullptr });
		}
		attachment = {};
	}
	depth_images_.clear();
	depth_format_ = VK_FORMAT_UNDEFINED;
}

void VulkanContext::createColorResources() {
	if (sample_count_ == VK_SAMPLE_COUNT_1_BIT) {
		destroyColorResources();
		return;
	}

	color_images_.resize(swapchain_images_.size());
	try {
		for (AttachmentImage &attachment : color_images_) {
			VkImageCreateInfo image_info{};
			image_info.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
			image_info.imageType     = VK_IMAGE_TYPE_2D;
			image_info.format        = swapchain_format_;
			image_info.extent        = { swapchain_extent_.width, swapchain_extent_.height, 1 };
			image_info.mipLevels     = 1;
			image_info.arrayLayers   = 1;
			image_info.samples       = sample_count_;
			image_info.tiling        = VK_IMAGE_TILING_OPTIMAL;
			image_info.usage         = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
			image_info.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
			image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			checkVk(vkCreateImage(device_.device(), &image_info, nullptr, &attachment.image),
			        "create multisample color image");

			VkMemoryRequirements memory_requirements{};
			vkGetImageMemoryRequirements(device_.device(), attachment.image, &memory_requirements);

			const DeviceMemoryAllocation allocation =
			    device_.allocateMemory(memory_requirements, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
			attachment.memory       = allocation.memory;
			attachment.memoryOffset = allocation.offset;
			attachment.memorySize   = allocation.size;
			checkVk(vkBindImageMemory(device_.device(), attachment.image, attachment.memory,
			                          attachment.memoryOffset),
			        "bind multisample color image memory");

			VkImageViewCreateInfo view_info{};
			view_info.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
			view_info.image                           = attachment.image;
			view_info.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
			view_info.format                          = swapchain_format_;
			view_info.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
			view_info.subresourceRange.baseMipLevel   = 0;
			view_info.subresourceRange.levelCount     = 1;
			view_info.subresourceRange.baseArrayLayer = 0;
			view_info.subresourceRange.layerCount     = 1;
			checkVk(vkCreateImageView(device_.device(), &view_info, nullptr, &attachment.view),
			        "create multisample color image view");
		}
	} catch (...) {
		destroyColorResources();
		throw;
	}
}

void VulkanContext::destroyColorResources() noexcept {
	if (device_.device() == VK_NULL_HANDLE) {
		return;
	}
	for (AttachmentImage &attachment : color_images_) {
		if (attachment.view != VK_NULL_HANDLE) {
			vkDestroyImageView(device_.device(), attachment.view, nullptr);
		}
		if (attachment.image != VK_NULL_HANDLE) {
			vkDestroyImage(device_.device(), attachment.image, nullptr);
		}
		if (attachment.memory != VK_NULL_HANDLE) {
			device_.freeMemory({ attachment.memory, attachment.memoryOffset, attachment.memorySize, 0, nullptr });
		}
		attachment = {};
	}
	color_images_.clear();
}

void VulkanContext::recoverSurface() {
	device_.waitIdleNoThrow();
	destroyWindowResources();
	if (surface_ != VK_NULL_HANDLE) {
		vkDestroySurfaceKHR(device_.instance(), surface_, nullptr);
		surface_ = VK_NULL_HANDLE;
	}
	createSurface();
	createSwapchain();
	createDepthResources();
	createColorResources();
	createRenderPass();
	createFramebuffers();
	createCommandResources();
	// Surface recovery always replaces the render pass, so long-lived
	// render-pass-dependent objects must be rebuilt just like a normal
	// swapchain recreation.
	notifySwapchainRecreated(true);
}

void VulkanContext::destroySwapchain(bool destroy_render_pass) {
	// Destroy dependent resources before the swapchain. Keep the render pass
	// when only the extent changed.
	for (VkFramebuffer framebuffer : swapchain_framebuffers_) {
		vkDestroyFramebuffer(device_.device(), framebuffer, nullptr);
	}
	swapchain_framebuffers_.clear();

	for (VkImageView view : swapchain_image_views_) {
		vkDestroyImageView(device_.device(), view, nullptr);
	}
	swapchain_image_views_.clear();
	destroyColorResources();
	destroyDepthResources();
	swapchain_images_.clear();

	if (swapchain_ != VK_NULL_HANDLE) {
		vkDestroySwapchainKHR(device_.device(), swapchain_, nullptr);
		swapchain_ = VK_NULL_HANDLE;
	}
	if (destroy_render_pass && render_pass_ != VK_NULL_HANDLE) {
		vkDestroyRenderPass(device_.device(), render_pass_, nullptr);
		render_pass_ = VK_NULL_HANDLE;
	}
}

void VulkanContext::destroyWindowResources() noexcept {
	// Release all device resources owned by this context after the device is idle.
	if (device_.device() == VK_NULL_HANDLE) {
		return;
	}
	logVkFailure(vkDeviceWaitIdle(device_.device()), "wait for Vulkan device during context shutdown");
	for (VkSemaphore semaphore : render_finished_) {
		vkDestroySemaphore(device_.device(), semaphore, nullptr);
	}
	for (VkSemaphore semaphore : image_available_) {
		vkDestroySemaphore(device_.device(), semaphore, nullptr);
	}
	for (VkFence fence : in_flight_) {
		vkDestroyFence(device_.device(), fence, nullptr);
	}
	if (command_pool_ != VK_NULL_HANDLE) {
		vkFreeCommandBuffers(device_.device(), command_pool_, MAX_FRAMES_IN_FLIGHT, command_buffers_.data());
		vkDestroyCommandPool(device_.device(), command_pool_, nullptr);
	}
	destroySwapchain();
}

bool VulkanContext::waitForValidFramebufferSize() const {
	// Minimized windows report zero size and cannot create a usable swapchain.
	// Stop waiting when the window is closed so teardown can proceed instead of
	// blocking forever with no further events to wake the loop.
	int width  = 0;
	int height = 0;
	while (true) {
		glfwGetFramebufferSize(window_->handle(), &width, &height);
		if (width > 0 && height > 0) {
			return true;
		}
		if (window_->shouldClose()) {
			return false;
		}
		glfwWaitEvents();
	}
}

void VulkanContext::setSwapchainRecreatedCallback(SwapchainRecreatedCallback callback) {
	swapchain_recreated_callback_ = std::move(callback);
}

SwapchainCallbackId VulkanContext::addSwapchainRecreatedCallback(SwapchainRecreatedCallback callback) {
	const SwapchainCallbackId id = next_swapchain_callback_id_++;
	additional_swapchain_callbacks_.push_back(SwapchainCallbackEntry{ id, std::move(callback) });
	return id;
}

bool VulkanContext::removeSwapchainRecreatedCallback(SwapchainCallbackId id) noexcept {
	const auto found = std::find_if(
	    additional_swapchain_callbacks_.begin(), additional_swapchain_callbacks_.end(),
	    [id](const SwapchainCallbackEntry &entry) { return entry.id == id; });
	if (found == additional_swapchain_callbacks_.end()) {
		return false;
	}
	additional_swapchain_callbacks_.erase(found);
	return true;
}

void VulkanContext::notifySwapchainRecreated(bool renderPassChanged) {
	++swapchain_generation_;
	const SwapchainRecreationInfo info{
	    render_pass_,
	    swapchain_format_,
	    swapchain_extent_,
	    renderPassChanged,
	    swapchain_generation_,
	};

	if (swapchain_recreated_callback_) {
		swapchain_recreated_callback_(info);
	}
	// Copy before invoking so a callback may remove itself or another callback
	// through the public API without invalidating this iteration.
	const std::vector<SwapchainCallbackEntry> callbacks = additional_swapchain_callbacks_;
	for (const SwapchainCallbackEntry &entry : callbacks) {
		if (entry.callback) {
			entry.callback(info);
		}
	}
}

void VulkanContext::recreateSwapchain() {
	// Rebuild resources affected by a resize. The render pass is retained when
	// the format is unchanged so existing pipelines remain compatible.
	if (device_.device() == VK_NULL_HANDLE) {
		return;
	}
	if (!waitForValidFramebufferSize()) {
		OXIDE_LOG_INFO << "window is closing; skipping swapchain recreation";
		return;
	}
	checkVk(vkDeviceWaitIdle(device_.device()), "wait for Vulkan device before recreating swapchain");
	const VkFormat              old_format       = swapchain_format_;
	const VkSampleCountFlagBits old_sample_count = sample_count_;
	destroySwapchain(false);
	createSwapchain();
	createDepthResources();
	createColorResources();
	const bool render_pass_changed =
	    swapchain_format_ != old_format || sample_count_ != old_sample_count;
	if (render_pass_changed) {
		OXIDE_LOG_WARNING << "swapchain format or sample count changed ("
		                  << static_cast<int>(old_format) << " -> " << static_cast<int>(swapchain_format_)
		                  << ", " << static_cast<int>(old_sample_count) << " -> "
		                  << static_cast<int>(sample_count_)
		                  << "); pipelines created for the old render pass must be recreated";
		if (render_pass_ != VK_NULL_HANDLE) {
			vkDestroyRenderPass(device_.device(), render_pass_, nullptr);
			render_pass_ = VK_NULL_HANDLE;
		}
		createRenderPass();
	}
	createFramebuffers();
	images_in_flight_.assign(swapchain_images_.size(), VK_NULL_HANDLE);

	for (VkSemaphore semaphore : render_finished_) {
		vkDestroySemaphore(device_.device(), semaphore, nullptr);
	}
	render_finished_.assign(swapchain_images_.size(), VK_NULL_HANDLE);
	VkSemaphoreCreateInfo semaphore{};
	semaphore.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
	for (VkSemaphore &signal : render_finished_) {
		checkVk(vkCreateSemaphore(device_.device(), &semaphore, nullptr, &signal),
		        "recreate render-finished semaphore");
	}

	notifySwapchainRecreated(render_pass_changed);
}

VkFramebuffer VulkanContext::framebuffer(std::uint32_t index) const {
	// Keep invalid image indices visible instead of returning an invalid handle.
	if (index >= swapchain_framebuffers_.size()) {
		throw outOfRange("swapchain framebuffer index " + std::to_string(index) + " is out of range (" +
		                 std::to_string(swapchain_framebuffers_.size()) + " framebuffers)");
	}
	return swapchain_framebuffers_[index];
}

void VulkanContext::createRenderPass() {
	const bool multisampled = sample_count_ != VK_SAMPLE_COUNT_1_BIT;

	VkAttachmentDescription attachments[3]{};
	std::uint32_t           attachment_count = 0;

	VkAttachmentDescription color_attachment{};
	color_attachment.format         = swapchain_format_;
	color_attachment.samples        = sample_count_;
	color_attachment.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
	color_attachment.storeOp        = multisampled ? VK_ATTACHMENT_STORE_OP_DONT_CARE
	                                               : VK_ATTACHMENT_STORE_OP_STORE;
	color_attachment.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	color_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	color_attachment.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
	color_attachment.finalLayout    = multisampled ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
	                                               : VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
	const std::uint32_t color_index = attachment_count;
	attachments[attachment_count++] = color_attachment;

	VkAttachmentReference color_reference{};
	color_reference.attachment = color_index;
	color_reference.layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	VkAttachmentReference resolve_reference{};
	if (multisampled) {
		VkAttachmentDescription resolve_attachment{};
		resolve_attachment.format         = swapchain_format_;
		resolve_attachment.samples        = VK_SAMPLE_COUNT_1_BIT;
		resolve_attachment.loadOp         = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		resolve_attachment.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
		resolve_attachment.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		resolve_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		resolve_attachment.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
		resolve_attachment.finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

		resolve_reference.attachment = attachment_count;
		resolve_reference.layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		attachments[attachment_count++] = resolve_attachment;
	}

	VkAttachmentDescription depth_attachment{};
	depth_attachment.format         = depth_format_;
	depth_attachment.samples        = sample_count_;
	depth_attachment.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
	depth_attachment.storeOp        = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	depth_attachment.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	depth_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	depth_attachment.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
	depth_attachment.finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
	const std::uint32_t depth_index = attachment_count;
	attachments[attachment_count++] = depth_attachment;

	VkAttachmentReference depth_reference{};
	depth_reference.attachment = depth_index;
	depth_reference.layout     = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

	VkSubpassDescription subpass{};
	subpass.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.colorAttachmentCount    = 1;
	subpass.pColorAttachments       = &color_reference;
	subpass.pResolveAttachments     = multisampled ? &resolve_reference : nullptr;
	subpass.pDepthStencilAttachment = &depth_reference;

	VkSubpassDependency colorDependency{};
	colorDependency.srcSubpass    = VK_SUBPASS_EXTERNAL;
	colorDependency.dstSubpass    = 0;
	colorDependency.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	colorDependency.srcAccessMask = 0;
	colorDependency.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	colorDependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

	VkSubpassDependency depthDependency{};
	depthDependency.srcSubpass    = VK_SUBPASS_EXTERNAL;
	depthDependency.dstSubpass    = 0;
	depthDependency.srcStageMask  = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
	                                VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
	depthDependency.srcAccessMask = 0;
	depthDependency.dstStageMask  = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
	                                VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
	depthDependency.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
	                                VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

	const VkSubpassDependency dependencies[] = { colorDependency, depthDependency };

	VkRenderPassCreateInfo renderPassInfo{};
	renderPassInfo.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	renderPassInfo.attachmentCount = attachment_count;
	renderPassInfo.pAttachments    = attachments;
	renderPassInfo.subpassCount    = 1;
	renderPassInfo.pSubpasses      = &subpass;
	renderPassInfo.dependencyCount = 2;
	renderPassInfo.pDependencies   = dependencies;

	checkVk(vkCreateRenderPass(device_.device(), &renderPassInfo, nullptr, &render_pass_),
	        "create render pass");
}

void VulkanContext::createFramebuffers() {
	const bool multisampled = sample_count_ != VK_SAMPLE_COUNT_1_BIT;
	if (depth_images_.size() != swapchain_image_views_.size()) {
		throw runtimeError("depth image count does not match the swapchain image count");
	}
	if (multisampled && color_images_.size() != swapchain_image_views_.size()) {
		throw runtimeError("multisample color image count does not match the swapchain image count");
	}
	swapchain_framebuffers_.resize(swapchain_image_views_.size());
	for (size_t i = 0; i < swapchain_image_views_.size(); i++) {
		VkImageView attachments[3]{};
		std::uint32_t attachment_count = 0;
		if (multisampled) {
			attachments[attachment_count++] = color_images_[i].view;
		}
		attachments[attachment_count++] = swapchain_image_views_[i];
		attachments[attachment_count++] = depth_images_[i].view;

		VkFramebufferCreateInfo framebufferInfo{};
		framebufferInfo.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		framebufferInfo.renderPass      = render_pass_;
		framebufferInfo.attachmentCount = attachment_count;
		framebufferInfo.pAttachments    = attachments;
		framebufferInfo.width           = swapchain_extent_.width;
		framebufferInfo.height          = swapchain_extent_.height;
		framebufferInfo.layers          = 1;

		checkVk(vkCreateFramebuffer(device_.device(), &framebufferInfo, nullptr, &swapchain_framebuffers_[i]),
		        "create swapchain framebuffer");
	}
}

}
