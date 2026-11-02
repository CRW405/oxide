#include <oxide/vulkan/descriptor_set.hpp>
#include <oxide/tools/log.hpp>
#include <oxide/vulkan/vk_error.hpp>

#include <stdexcept>
#include <vector>

namespace oxide {

// -----------------------------------------------------------------------------
// Descriptor layout lifetime
// -----------------------------------------------------------------------------
DescriptorSetLayout::DescriptorSetLayout(
    VulkanDevice &device,
    std::initializer_list<DescriptorBinding> bindings)
    : device_(device) {
	std::vector<VkDescriptorSetLayoutBinding> layoutBindings;
	layoutBindings.reserve(bindings.size());
	for (const DescriptorBinding &binding : bindings) {
		layoutBindings.push_back(binding.build());
	}

	VkDescriptorSetLayoutCreateInfo createInfo{};
	createInfo.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	createInfo.bindingCount = static_cast<std::uint32_t>(layoutBindings.size());
	createInfo.pBindings    = layoutBindings.data();

	checkVk(vkCreateDescriptorSetLayout(device_.device(), &createInfo, nullptr, &layout_),
	        "create descriptor set layout");
}

DescriptorSetLayout::DescriptorSetLayout(VulkanDevice &device, const DescriptorBinding &binding)
    : DescriptorSetLayout(device, { binding }) {}

DescriptorSetLayout::~DescriptorSetLayout() {
	if (layout_ != VK_NULL_HANDLE && device_.device() != VK_NULL_HANDLE) {
		vkDestroyDescriptorSetLayout(device_.device(), layout_, nullptr);
	}
}

// -----------------------------------------------------------------------------
// Descriptor pool and set allocation
// -----------------------------------------------------------------------------
DescriptorPool::DescriptorPool(
    VulkanDevice                                &device,
    std::initializer_list<VkDescriptorPoolSize> sizes,
    std::uint32_t                                maxSets)
    : device_(device) {
	if (sizes.size() == 0) {
		throw invalidArgument("descriptor pool requires at least one descriptor size");
	}
	if (maxSets == 0) {
		throw invalidArgument("descriptor pool requires at least one descriptor set");
	}
	std::vector<VkDescriptorPoolSize> poolSizes(sizes);

	VkDescriptorPoolCreateInfo createInfo{};
	createInfo.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	createInfo.poolSizeCount = static_cast<std::uint32_t>(poolSizes.size());
	createInfo.pPoolSizes    = poolSizes.data();
	createInfo.maxSets       = maxSets;

	checkVk(vkCreateDescriptorPool(device_.device(), &createInfo, nullptr, &pool_),
	        "create descriptor pool");
}

DescriptorPool::DescriptorPool(
    VulkanDevice   &device,
    VkDescriptorType type,
    std::uint32_t    descriptorCount,
    std::uint32_t    maxSets)
    : DescriptorPool(device, { { type, descriptorCount } }, maxSets) {}

DescriptorPool::~DescriptorPool() {
	if (pool_ != VK_NULL_HANDLE && device_.device() != VK_NULL_HANDLE) {
		vkDestroyDescriptorPool(device_.device(), pool_, nullptr);
	}
}

VkDescriptorSet DescriptorPool::allocate(VkDescriptorSetLayout layout) {
	VkDescriptorSetAllocateInfo allocation{};
	allocation.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	allocation.descriptorPool     = pool_;
	allocation.descriptorSetCount = 1;
	allocation.pSetLayouts        = &layout;

	VkDescriptorSet set = VK_NULL_HANDLE;
	checkVk(vkAllocateDescriptorSets(device_.device(), &allocation, &set), "allocate descriptor set");
	return set;
}

DescriptorSet::DescriptorSet(DescriptorPool &pool, VkDescriptorSetLayout layout)
    : layout_(layout) {
	set_ = pool.allocate(layout);
}

void DescriptorSet::bind(
    VkCommandBuffer     commandBuffer,
    VkPipelineLayout    pipelineLayout,
    std::uint32_t       firstSet,
    VkPipelineBindPoint bindPoint) const {
	vkCmdBindDescriptorSets(commandBuffer,
	                        bindPoint,
	                        pipelineLayout,
	                        firstSet,
	                        1,
	                        &set_,
	                        0,
	                        nullptr);
}

} // namespace oxide
