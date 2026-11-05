#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <string>
#include <vector>

#include <oxide/vulkan/core/resource_registry.hpp>

struct GLFWwindow;

namespace oxide {

struct QueueFamilies {
	std::uint32_t graphics    = 0;
	std::uint32_t present     = 0;
	bool          hasGraphics = false;
	bool          hasPresent  = false;
};

struct DeviceMemoryAllocation {
	VkDeviceMemory memory = VK_NULL_HANDLE;
	VkDeviceSize   offset = 0;
	VkDeviceSize   size   = 0;
	std::uint32_t  type   = 0;
	void          *mapped = nullptr;
};

// ============================================================================
// Shared Vulkan device
//
// Owns the Vulkan instance and logical device shared by renderer resources.
// Device selection uses the window surface because presentation support belongs
// to a queue family and surface pair.
// ============================================================================
class VulkanDevice {
  public:
	explicit VulkanDevice(GLFWwindow *main_window, std::string app_name = "Oxide");
	explicit VulkanDevice(std::string app_name);
	~VulkanDevice();

	VulkanDevice(const VulkanDevice &)            = delete;
	VulkanDevice &operator=(const VulkanDevice &) = delete;
	VulkanDevice(VulkanDevice &&)                 = delete;
	VulkanDevice &operator=(VulkanDevice &&)      = delete;

	[[nodiscard]] VkInstance       instance() const noexcept { return instance_; }
	[[nodiscard]] VkPhysicalDevice physicalDevice() const noexcept { return physical_device_; }
	[[nodiscard]] VkDevice         device() const noexcept { return device_; }
	[[nodiscard]] VkQueue          graphicsQueue() const noexcept { return graphics_queue_; }
	[[nodiscard]] VkQueue          presentQueue() const noexcept { return present_queue_; }
	[[nodiscard]] VkCommandPool    commandPool() const noexcept { return command_pool_; }
	[[nodiscard]] VkPipelineCache  pipelineCache() const noexcept { return pipeline_cache_; }
	[[nodiscard]] QueueFamilies    findQueueFamilies(VkPhysicalDevice device, VkSurfaceKHR surface) const;
	[[nodiscard]] std::uint32_t    findMemoryType(std::uint32_t typeFilter, VkMemoryPropertyFlags properties) const;
	[[nodiscard]] DeviceMemoryAllocation allocateMemory(
	    const VkMemoryRequirements &requirements, VkMemoryPropertyFlags properties);
	void                           freeMemory(const DeviceMemoryAllocation &allocation) noexcept;
	[[nodiscard]] void            *mapMemory(const DeviceMemoryAllocation &allocation) const;
	[[nodiscard]] const VkPhysicalDeviceFeatures &features() const noexcept { return enabled_features_; }
	[[nodiscard]] bool             deviceLost() const noexcept { return device_lost_; }
	[[nodiscard]] ResourceRegistry &resources() noexcept { return resources_; }
	[[nodiscard]] const ResourceRegistry &resources() const noexcept { return resources_; }
	void                           waitIdle() const;

	// Wait for the device without throwing. Intended for destructors and other
	// cleanup paths where an exception would terminate the process.
	void waitIdleNoThrow() const noexcept;

  private:
	void createInstance(const std::string app_name);
	void setupDebugMessenger();
	void destroyDebugMessenger() noexcept;
	void selectPhysicalDevice(VkSurfaceKHR surface);
	[[nodiscard]] int scorePhysicalDevice(
	    VkPhysicalDevice device,
	    VkSurfaceKHR     surface,
	    QueueFamilies    &queues,
	    VkPhysicalDeviceFeatures &features) const;
	void createLogicalDevice(VkSurfaceKHR surface);
	void createPipelineCache();
	void markDeviceLost() const noexcept { device_lost_ = true; }

	std::string      app_name_;
	VkInstance       instance_        = VK_NULL_HANDLE;
	VkPhysicalDevice physical_device_ = VK_NULL_HANDLE;
	VkDevice         device_          = VK_NULL_HANDLE;
	VkQueue          graphics_queue_  = VK_NULL_HANDLE;
	VkQueue          present_queue_   = VK_NULL_HANDLE;
	VkCommandPool    command_pool_    = VK_NULL_HANDLE;
	VkPipelineCache  pipeline_cache_  = VK_NULL_HANDLE;
	VkPhysicalDeviceFeatures enabled_features_{};
	mutable bool device_lost_ = false;
	struct MemoryBlock {
		struct FreeRange {
			VkDeviceSize offset = 0;
			VkDeviceSize size = 0;
		};
		VkDeviceMemory memory = VK_NULL_HANDLE;
		VkDeviceSize size = 0;
		VkDeviceSize used = 0;
		std::uint32_t type = 0;
		void *mapped = nullptr;
		std::vector<FreeRange> free_ranges;
	};
	std::vector<MemoryBlock> memory_blocks_;
	ResourceRegistry resources_;
	VkDebugUtilsMessengerEXT debug_messenger_ = VK_NULL_HANDLE;
};

// ============================================================================
// Device-idle scope guard
//
// Waits for the device to become idle when the guard is destroyed. Declare it
// after every GPU resource in a scope so it is destroyed first, including when
// the scope exits with an exception. That ensures resource destructors never
// release objects that in-flight work still references.
//
//   VulkanDevice device(...);
//   {
//       Buffer buffer(device, ...);
//       DeviceIdleGuard idle(device); // destroyed before buffer
//       ...
//   }
// ============================================================================
class DeviceIdleGuard {
  public:
	explicit DeviceIdleGuard(const VulkanDevice &device) noexcept : device_(device) {}
	~DeviceIdleGuard() noexcept { device_.waitIdleNoThrow(); }

	DeviceIdleGuard(const DeviceIdleGuard &)            = delete;
	DeviceIdleGuard &operator=(const DeviceIdleGuard &) = delete;
	DeviceIdleGuard(DeviceIdleGuard &&)                 = delete;
	DeviceIdleGuard &operator=(DeviceIdleGuard &&)      = delete;

  private:
	const VulkanDevice &device_;
};

}
