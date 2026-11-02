#include <oxide/vulkan/vulkan_device.hpp>
#include <oxide/oxide_config.hpp>
#include <oxide/tools/log.hpp>
#include <oxide/vulkan/vk_error.hpp>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <algorithm>
#include <cstring>
#include <set>
#include <stdexcept>
#include <utility>

namespace oxide {

// -----------------------------------------------------------------------------
// Vulkan instance, device, and queue setup
//
// Validation is enabled by default for learning and development, and can be
// disabled for release builds with -DOXIDE_ENABLE_VALIDATION=OFF. The debug
// messenger is attached to instance creation so validation can report setup
// errors as well as errors that occur after the instance exists.
// -----------------------------------------------------------------------------
constexpr bool        enable_validation   = OXIDE_ENABLE_VALIDATION != 0;
constexpr const char *validation_layer    = "VK_LAYER_KHRONOS_validation";
constexpr const char *swapchain_extension = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
constexpr const char *debug_extension     = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;

VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT      severity,
    VkDebugUtilsMessageTypeFlagsEXT             type,
    const VkDebugUtilsMessengerCallbackDataEXT *callback_data,
    void *) {
	if (callback_data == nullptr) {
		return VK_FALSE;
	}

	LogLevel level = LogLevel::Info;
	if ((severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) != 0) {
		level = LogLevel::Error;
	} else if ((severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) != 0) {
		level = LogLevel::Warning;
	}

	std::string source = "Vulkan";
	if ((type & VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT) != 0) {
		source += " validation";
	} else if ((type & VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT) != 0) {
		source += " performance";
	} else if ((type & VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT) != 0) {
		source += " general";
	}

	std::string message = source;
	message += ": ";
	message += callback_data->pMessage != nullptr ? callback_data->pMessage : "(no message)";
	logMessage(level, message, {});
	return VK_FALSE;
}

VkDebugUtilsMessengerCreateInfoEXT debugMessengerInfo() {
	VkDebugUtilsMessengerCreateInfoEXT info{};
	info.sType           = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
	// Warnings and errors only: loader info/verbose messages are noisy and
	// make real problems harder to spot in the terminal.
	info.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
	                       VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
	info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
	                   VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
	                   VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
	info.pfnUserCallback = debugCallback;
	return info;
}

bool instanceExtensionAvailable(const char *name) {
	// Query the loader before adding optional instance extensions.
	std::uint32_t count = 0;
	checkVk(vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr), "enumerate Vulkan instance extensions");
	std::vector<VkExtensionProperties> extensions(count);
	if (count > 0) {
		checkVk(vkEnumerateInstanceExtensionProperties(nullptr, &count, extensions.data()),
		        "enumerate Vulkan instance extensions");
	}
	return std::any_of(extensions.begin(), extensions.end(), [name](const VkExtensionProperties &extension) {
		return std::strcmp(extension.extensionName, name) == 0;
	});
}

bool validationAvailable() {
	// Validation is required by this development-oriented bootstrap.
	std::uint32_t count = 0;
	checkVk(vkEnumerateInstanceLayerProperties(&count, nullptr), "enumerate Vulkan instance layers");
	std::vector<VkLayerProperties> layers(count);
	if (count > 0) {
		checkVk(vkEnumerateInstanceLayerProperties(&count, layers.data()), "enumerate Vulkan instance layers");
	}
	return std::any_of(layers.begin(), layers.end(), [](const VkLayerProperties &layer) {
		return std::strcmp(layer.layerName, validation_layer) == 0;
	});
}

bool swapchainExtensionAvailable(VkPhysicalDevice device) {
	// A device must expose swapchain support before it can present.
	std::uint32_t count = 0;
	checkVk(vkEnumerateDeviceExtensionProperties(device, nullptr, &count, nullptr),
	        "enumerate Vulkan device extensions");
	std::vector<VkExtensionProperties> extensions(count);
	if (count > 0) {
		checkVk(vkEnumerateDeviceExtensionProperties(device, nullptr, &count, extensions.data()),
		        "enumerate Vulkan device extensions");
	}
	return std::any_of(extensions.begin(), extensions.end(), [](const VkExtensionProperties &extension) {
		return std::strcmp(extension.extensionName, swapchain_extension) == 0;
	});
}

VulkanDevice::VulkanDevice(GLFWwindow *main_window, std::string app_name) : app_name_(std::move(app_name)) {
	//
	// Step 1: create the instance and a temporary surface used only to evaluate
	// queue families during physical-device selection.
	//
	if (main_window == nullptr) {
		throw invalidArgument("VulkanDevice requires a valid window");
	}

	createInstance(app_name_);

	VkSurfaceKHR selection_surface = VK_NULL_HANDLE;
	const VkResult surface_result =
	    glfwCreateWindowSurface(instance_, main_window, nullptr, &selection_surface);
	if (surface_result != VK_SUCCESS) {
		destroyDebugMessenger();
		vkDestroyInstance(instance_, nullptr);
		instance_ = VK_NULL_HANDLE;
		throw vkError("create Vulkan surface for device selection", surface_result);
	}

	try {
		// Step 2: choose a device and create the logical queues used by rendering.
		selectPhysicalDevice(selection_surface);
		createLogicalDevice(selection_surface);
	} catch (...) {
		if (command_pool_ != VK_NULL_HANDLE) {
			vkDestroyCommandPool(device_, command_pool_, nullptr);
			command_pool_ = VK_NULL_HANDLE;
		}
		if (device_ != VK_NULL_HANDLE) {
			vkDestroyDevice(device_, nullptr);
			device_ = VK_NULL_HANDLE;
		}
		vkDestroySurfaceKHR(instance_, selection_surface, nullptr);
		destroyDebugMessenger();
		vkDestroyInstance(instance_, nullptr);
		instance_ = VK_NULL_HANDLE;
		throw;
	}
	vkDestroySurfaceKHR(instance_, selection_surface, nullptr);
}

VulkanDevice::~VulkanDevice() {
	if (device_ != VK_NULL_HANDLE) {
		logVkFailure(vkDeviceWaitIdle(device_), "wait for Vulkan device during shutdown");
		if (command_pool_ != VK_NULL_HANDLE) {
			vkDestroyCommandPool(device_, command_pool_, nullptr);
		}
		vkDestroyDevice(device_, nullptr);
	}
	destroyDebugMessenger();
	if (instance_ != VK_NULL_HANDLE) {
		vkDestroyInstance(instance_, nullptr);
	}
}

void VulkanDevice::waitIdle() const {
	if (device_ != VK_NULL_HANDLE) {
		checkVk(vkDeviceWaitIdle(device_), "wait for Vulkan device");
	}
}

void VulkanDevice::waitIdleNoThrow() const noexcept {
	if (device_ != VK_NULL_HANDLE) {
		logVkFailure(vkDeviceWaitIdle(device_), "wait for Vulkan device");
	}
}

void VulkanDevice::createInstance(const std::string app_name) {
	// -------------------------------------------------------------------------
	// Step 1: verify development layers and gather the extensions required by
	// GLFW. A missing validation layer is downgraded to a warning so release
	// builds still start on systems without the layer installed.
	// -------------------------------------------------------------------------
	bool validation_enabled = enable_validation;
	if (validation_enabled && !validationAvailable()) {
		OXIDE_LOG_WARNING << "Vulkan validation layer '" << validation_layer
		                  << "' is unavailable; continuing without validation";
		validation_enabled = false;
	}

	// -------------------------------------------------------------------------
	// Step 2: describe the application and create the Vulkan instance. The
	// debug messenger create info is chained here to capture startup messages.
	// -------------------------------------------------------------------------
	VkApplicationInfo app_info{};
	app_info.sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	app_info.pApplicationName   = app_name.c_str();
	app_info.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
	app_info.pEngineName        = "Oxide";
	app_info.engineVersion      = VK_MAKE_VERSION(0, 1, 0);
	app_info.apiVersion         = VK_API_VERSION_1_0;

	std::uint32_t glfw_extension_count = 0;
	const char  **glfw_extensions       = glfwGetRequiredInstanceExtensions(&glfw_extension_count);
	if (glfw_extensions == nullptr) {
		throw runtimeError("GLFW did not provide Vulkan extensions");
	}
	std::vector<const char *> extensions(glfw_extensions, glfw_extensions + glfw_extension_count);
	const bool                has_debug_extension =
	    validation_enabled && instanceExtensionAvailable(debug_extension);
	if (validation_enabled && !has_debug_extension) {
		OXIDE_LOG_WARNING << "Vulkan debug utils extension is unavailable; "
		                     "validation messages will not be printed";
	}
	if (validation_enabled && has_debug_extension) {
		extensions.push_back(debug_extension);
	}

	VkInstanceCreateInfo info{};
	info.sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	info.pApplicationInfo        = &app_info;
	info.enabledExtensionCount   = static_cast<std::uint32_t>(extensions.size());
	info.ppEnabledExtensionNames = extensions.data();
	if (validation_enabled) {
		info.enabledLayerCount   = 1;
		info.ppEnabledLayerNames = &validation_layer;
	}
	VkDebugUtilsMessengerCreateInfoEXT debug_info{};
	if (validation_enabled && has_debug_extension) {
		debug_info = debugMessengerInfo();
		info.pNext = &debug_info;
	}
	checkVk(vkCreateInstance(&info, nullptr, &instance_), "create Vulkan instance");
	if (validation_enabled && has_debug_extension) {
		try {
			setupDebugMessenger();
		} catch (...) {
			vkDestroyInstance(instance_, nullptr);
			instance_ = VK_NULL_HANDLE;
			throw;
		}
	}
}

void VulkanDevice::setupDebugMessenger() {
	// Create the callback that reports validation messages after instance setup.
	const auto create_messenger = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
	    vkGetInstanceProcAddr(instance_, "vkCreateDebugUtilsMessengerEXT"));
	if (create_messenger == nullptr) {
		throw runtimeError("failed to load Vulkan debug messenger function");
	}
	const VkDebugUtilsMessengerCreateInfoEXT info = debugMessengerInfo();
	checkVk(create_messenger(instance_, &info, nullptr, &debug_messenger_),
	        "create Vulkan debug messenger");
}

void VulkanDevice::destroyDebugMessenger() noexcept {
	// The messenger must be removed before its owning instance is destroyed.
	if (instance_ == VK_NULL_HANDLE || debug_messenger_ == VK_NULL_HANDLE) {
		return;
	}
	const auto destroy_messenger = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
	    vkGetInstanceProcAddr(instance_, "vkDestroyDebugUtilsMessengerEXT"));
	if (destroy_messenger != nullptr) {
		destroy_messenger(instance_, debug_messenger_, nullptr);
	} else {
		OXIDE_LOG_WARNING << "Vulkan destroy debug messenger function is unavailable; "
		                     "the messenger was not destroyed";
	}
	debug_messenger_ = VK_NULL_HANDLE;
}

QueueFamilies VulkanDevice::findQueueFamilies(VkPhysicalDevice device, VkSurfaceKHR surface) const {
	//
	// Walk every queue family once and record the first graphics and present
	// queues. A device is usable only when both capabilities are available.
	//
	QueueFamilies result{};
	std::uint32_t count = 0;
	vkGetPhysicalDeviceQueueFamilyProperties(device, &count, nullptr);
	std::vector<VkQueueFamilyProperties> families(count);
	vkGetPhysicalDeviceQueueFamilyProperties(device, &count, families.data());

	for (std::uint32_t index = 0; index < count; ++index) {
		if ((families[index].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0) {
			result.graphics    = index;
			result.hasGraphics = true;
		}

		VkBool32 present = VK_FALSE;
		checkVk(vkGetPhysicalDeviceSurfaceSupportKHR(device, index, surface, &present),
		        "query Vulkan surface presentation support");
		if (present == VK_TRUE) {
			result.present    = index;
			result.hasPresent = true;
		}

		if (result.hasGraphics && result.hasPresent) {
			break;
		}
	}
	return result;
}

std::uint32_t VulkanDevice::findMemoryType(std::uint32_t typeFilter, VkMemoryPropertyFlags properties) const {
	VkPhysicalDeviceMemoryProperties memory_properties;
	vkGetPhysicalDeviceMemoryProperties(physical_device_, &memory_properties);

	for (std::uint32_t index = 0; index < memory_properties.memoryTypeCount; ++index) {
		if ((typeFilter & (1u << index)) != 0 &&
		    (memory_properties.memoryTypes[index].propertyFlags & properties) == properties) {
			return index;
		}
	}

	throw runtimeError("failed to find a suitable Vulkan memory type (typeBits=" +
	                   std::to_string(typeFilter) + ", properties=" +
	                   std::to_string(properties) + ")");
}

void VulkanDevice::selectPhysicalDevice(VkSurfaceKHR surface) {
	//
	// Prefer the first device that can draw, present, and expose the swapchain
	// extension. This keeps selection predictable while the bootstrap is small.
	//
	std::uint32_t count = 0;
	checkVk(vkEnumeratePhysicalDevices(instance_, &count, nullptr), "enumerate Vulkan devices");
	if (count == 0) {
		throw runtimeError("no Vulkan devices found");
	}

	std::vector<VkPhysicalDevice> devices(count);
	checkVk(vkEnumeratePhysicalDevices(instance_, &count, devices.data()), "enumerate Vulkan devices");

	for (VkPhysicalDevice device : devices) {
		const QueueFamilies queues = findQueueFamilies(device, surface);
		if (queues.hasGraphics && queues.hasPresent && swapchainExtensionAvailable(device)) {
			physical_device_ = device;
			VkPhysicalDeviceProperties properties{};
			vkGetPhysicalDeviceProperties(physical_device_, &properties);
			OXIDE_LOG_INFO << "selected Vulkan device: " << properties.deviceName;
			return;
		}
	}
	throw runtimeError("no suitable Vulkan device found");
}

void VulkanDevice::createLogicalDevice(VkSurfaceKHR surface) {
	//
	// Create one queue for each distinct family. When graphics and presentation
	// share a family, the set removes the duplicate automatically.
	//
	const QueueFamilies                  queues   = findQueueFamilies(physical_device_, surface);
	const float                          priority = 1.0f;
	std::vector<VkDeviceQueueCreateInfo> queue_infos;

	for (std::uint32_t family : std::set{ queues.graphics, queues.present }) {
		VkDeviceQueueCreateInfo info{};
		info.sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
		info.queueFamilyIndex = family;
		info.queueCount       = 1;
		info.pQueuePriorities = &priority;
		queue_infos.push_back(info);
	}

	VkDeviceCreateInfo info{};
	info.sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
	info.queueCreateInfoCount    = static_cast<std::uint32_t>(queue_infos.size());
	info.pQueueCreateInfos       = queue_infos.data();
	info.enabledExtensionCount   = 1;
	info.ppEnabledExtensionNames = &swapchain_extension;
	checkVk(vkCreateDevice(physical_device_, &info, nullptr, &device_), "create logical device");

	vkGetDeviceQueue(device_, queues.graphics, 0, &graphics_queue_);
	vkGetDeviceQueue(device_, queues.present, 0, &present_queue_);

	VkCommandPoolCreateInfo pool_info{};
	pool_info.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	pool_info.flags            = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
	pool_info.queueFamilyIndex = queues.graphics;
	checkVk(vkCreateCommandPool(device_, &pool_info, nullptr, &command_pool_), "create Vulkan device command pool");
}

}
