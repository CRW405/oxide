#include <oxide/vulkan/pipeline/pipeline_layout.hpp>
#include <oxide/tools/log.hpp>
#include <oxide/vulkan/core/vk_error.hpp>
#include <oxide/vulkan/pipeline/shader_reflection.hpp>

#include <stdexcept>

namespace oxide {

PipelineLayout::PipelineLayout(
    VulkanDevice &device,
    const VkDescriptorSetLayout *descriptorSetLayouts,
    std::uint32_t descriptorSetLayoutCount,
    const VkPushConstantRange *pushConstantRanges,
    std::uint32_t pushConstantRangeCount)
    : device_(device) {
	if (descriptorSetLayoutCount > 0 && descriptorSetLayouts == nullptr) {
		throw invalidArgument("pipeline layout requires descriptor set layouts when the count is non-zero");
	}
	if (pushConstantRangeCount > 0 && pushConstantRanges == nullptr) {
		throw invalidArgument("pipeline layout requires push constant ranges when the count is non-zero");
	}

	if (descriptorSetLayoutCount > 0) {
		descriptor_set_layouts_.assign(descriptorSetLayouts,
		                               descriptorSetLayouts + descriptorSetLayoutCount);
	}
	if (pushConstantRangeCount > 0) {
		push_constant_ranges_.assign(pushConstantRanges, pushConstantRanges + pushConstantRangeCount);
	}

	VkPipelineLayoutCreateInfo createInfo{};
	createInfo.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	createInfo.setLayoutCount         = static_cast<std::uint32_t>(descriptor_set_layouts_.size());
	createInfo.pSetLayouts            = descriptor_set_layouts_.data();
	createInfo.pushConstantRangeCount = static_cast<std::uint32_t>(push_constant_ranges_.size());
	createInfo.pPushConstantRanges    = push_constant_ranges_.data();

	checkVk(vkCreatePipelineLayout(device_.device(), &createInfo, nullptr, &layout_),
	        "create pipeline layout");
	resource_handle_ = device_.resources().registerPipelineLayout({layout_});
}

PipelineLayout::PipelineLayout(VulkanDevice &device, const ShaderReflection &reflection)
    : device_(device) {
	const std::uint32_t set_count = reflection.descriptorSetCount();
	descriptor_set_layouts_.resize(set_count, VK_NULL_HANDLE);
	owned_descriptor_layouts_.reserve(set_count);

	try {
		for (std::uint32_t set = 0; set < set_count; ++set) {
			const std::vector<VkDescriptorSetLayoutBinding> &bindings =
			    reflection.descriptorBindings(set);

			VkDescriptorSetLayoutCreateInfo create_info{};
			create_info.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
			create_info.bindingCount = static_cast<std::uint32_t>(bindings.size());
			create_info.pBindings    = bindings.data();

			VkDescriptorSetLayout descriptor_layout = VK_NULL_HANDLE;
			checkVk(vkCreateDescriptorSetLayout(device_.device(), &create_info, nullptr, &descriptor_layout),
			        "create reflected descriptor set layout");
			descriptor_set_layouts_[set] = descriptor_layout;
			owned_descriptor_layouts_.push_back(
			    device_.resources().registerDescriptorLayout({descriptor_layout}));
		}

		push_constant_ranges_ = reflection.pushConstantRanges();

		VkPipelineLayoutCreateInfo create_info{};
		create_info.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		create_info.setLayoutCount         = static_cast<std::uint32_t>(descriptor_set_layouts_.size());
		create_info.pSetLayouts            = descriptor_set_layouts_.data();
		create_info.pushConstantRangeCount = static_cast<std::uint32_t>(push_constant_ranges_.size());
		create_info.pPushConstantRanges    = push_constant_ranges_.data();

		checkVk(vkCreatePipelineLayout(device_.device(), &create_info, nullptr, &layout_),
		        "create reflected pipeline layout");
		resource_handle_ = device_.resources().registerPipelineLayout({layout_});
	} catch (...) {
		for (DescriptorLayoutHandle handle : owned_descriptor_layouts_) {
			device_.resources().unregisterDescriptorLayout(handle);
		}
		owned_descriptor_layouts_.clear();
		descriptor_set_layouts_.clear();
		throw;
	}
}

PipelineLayout::PipelineLayout(
    VulkanDevice &device,
    std::initializer_list<VkDescriptorSetLayout> descriptorSetLayouts,
    std::initializer_list<VkPushConstantRange> pushConstantRanges)
    : PipelineLayout(
          device,
          descriptorSetLayouts.begin(),
          static_cast<std::uint32_t>(descriptorSetLayouts.size()),
          pushConstantRanges.begin(),
          static_cast<std::uint32_t>(pushConstantRanges.size())) {}

PipelineLayout::PipelineLayout(
    VulkanDevice &device,
    std::initializer_list<VkPushConstantRange> pushConstantRanges)
    : PipelineLayout(device, nullptr, 0, pushConstantRanges.begin(),
                     static_cast<std::uint32_t>(pushConstantRanges.size())) {}

PipelineLayout::~PipelineLayout() {
	if (device_.device() == VK_NULL_HANDLE) {
		return;
	}
	// The pipeline layout references its descriptor set layouts, so destroy it
	// first. Owned descriptor set layouts are created only by the reflection
	// constructor.
	if (layout_ != VK_NULL_HANDLE) {
		device_.resources().unregisterPipelineLayout(resource_handle_);
		layout_ = VK_NULL_HANDLE;
	}
	for (DescriptorLayoutHandle handle : owned_descriptor_layouts_) {
		device_.resources().unregisterDescriptorLayout(handle);
	}
	owned_descriptor_layouts_.clear();
}

void PipelineLayout::bindDescriptorSet(
    VkCommandBuffer commandBuffer,
    VkDescriptorSet descriptorSet,
    std::uint32_t   firstSet) const {
	vkCmdBindDescriptorSets(commandBuffer,
	                        VK_PIPELINE_BIND_POINT_GRAPHICS,
	                        layout_,
	                        firstSet,
	                        1,
	                        &descriptorSet,
	                        0,
	                        nullptr);
}

}
