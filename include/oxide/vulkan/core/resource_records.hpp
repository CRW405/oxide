#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <string>

namespace oxide {

struct BufferResource {
	VkBuffer       buffer = VK_NULL_HANDLE;
	VkDeviceMemory memory = VK_NULL_HANDLE;
	VkDeviceSize   offset = 0;
	VkDeviceSize   size   = 0;
};

struct ImageResource {
	VkImage        image = VK_NULL_HANDLE;
	VkDeviceMemory memory = VK_NULL_HANDLE;
	VkDeviceSize   offset = 0;
	VkDeviceSize   size   = 0;
	VkImageView    view = VK_NULL_HANDLE;
	VkSampler      sampler = VK_NULL_HANDLE;
};

struct ShaderResource {
	VkShaderModule module = VK_NULL_HANDLE;
	std::uint32_t  references = 1;
	std::string    key;
};

struct PipelineResource {
	VkPipeline       pipeline = VK_NULL_HANDLE;
	VkPipelineLayout layout = VK_NULL_HANDLE;
};

struct DescriptorLayoutResource {
	VkDescriptorSetLayout layout = VK_NULL_HANDLE;
};

struct DescriptorPoolResource {
	VkDescriptorPool pool = VK_NULL_HANDLE;
};

struct PipelineLayoutResource {
	VkPipelineLayout layout = VK_NULL_HANDLE;
};

} // namespace oxide
