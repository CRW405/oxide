#pragma once

#include <oxide/glfw/glfw_oxide.hpp>
#include <oxide/vulkan/frame/vulkan_context.hpp>
#include <oxide/vulkan/device/vulkan_device.hpp>

#include <string>

namespace oxide {

// Owns the common GLFW, window, device, and presentation-context bootstrap.
class Application {
  public:
	Application(
	    const WindowConfig   &window_config = {},
	    const char           *app_name      = "Oxide",
	    const SwapchainConfig &swapchain_config = {});
	Application(
	    int                       width,
	    int                       height,
	    std::string               app_name,
	    const SwapchainConfig    &swapchain_config = {});

	Application(const Application &)            = delete;
	Application &operator=(const Application &) = delete;
	Application(Application &&)                 = delete;
	Application &operator=(Application &&)      = delete;

	[[nodiscard]] Window          &window() noexcept { return window_; }
	[[nodiscard]] VulkanDevice    &device() noexcept { return device_; }
	[[nodiscard]] ResourceRegistry &resources() noexcept { return device_.resources(); }
	[[nodiscard]] VulkanContext   &context() noexcept { return context_; }

  private:
	GLFWContext   glfw_;
	Window        window_;
	VulkanDevice  device_;
	VulkanContext context_;
};

} // namespace oxide
