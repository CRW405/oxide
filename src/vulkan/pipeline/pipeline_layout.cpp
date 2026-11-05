#include <oxide/vulkan/pipeline/pipeline_layout.hpp>
#include <oxide/tools/log.hpp>
#include <oxide/vulkan/core/vk_error.hpp>

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

	VkPipelineLayoutCreateInfo createInfo{};
	createInfo.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	createInfo.setLayoutCount         = descriptorSetLayoutCount;
	createInfo.pSetLayouts            = descriptorSetLayouts;
	createInfo.pushConstantRangeCount = pushConstantRangeCount;
	createInfo.pPushConstantRanges    = pushConstantRanges;

	checkVk(vkCreatePipelineLayout(device_.device(), &createInfo, nullptr, &layout_),
	        "create pipeline layout");
	resource_handle_ = device_.resources().registerPipelineLayout({layout_});
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
	if (layout_ != VK_NULL_HANDLE && device_.device() != VK_NULL_HANDLE) {
		device_.resources().unregisterPipelineLayout(resource_handle_);
		layout_ = VK_NULL_HANDLE;
	}
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
