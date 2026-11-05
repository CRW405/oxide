#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <vector>

namespace oxide {

// A Vulkan handle supplied by another owner. Pipeline creation may use it but
// never destroys it. Use PipelineLayout for layouts owned by Oxide.
class BorrowedPipelineLayout {
  public:
	constexpr BorrowedPipelineLayout() noexcept = default;
	explicit constexpr BorrowedPipelineLayout(VkPipelineLayout handle) noexcept : handle_(handle) {}

	[[nodiscard]] constexpr VkPipelineLayout get() const noexcept { return handle_; }
	[[nodiscard]] constexpr bool valid() const noexcept { return handle_ != VK_NULL_HANDLE; }

  private:
	VkPipelineLayout handle_ = VK_NULL_HANDLE;
};

struct VertexInputDescription {
	std::vector<VkVertexInputBindingDescription>   bindings;
	std::vector<VkVertexInputAttributeDescription> attributes;

	VertexInputDescription &addBinding(
	    std::uint32_t binding,
	    std::uint32_t stride,
	    VkVertexInputRate inputRate = VK_VERTEX_INPUT_RATE_VERTEX);
	VertexInputDescription &addAttribute(
	    std::uint32_t location,
	    std::uint32_t binding,
	    VkFormat format,
	    std::uint32_t offset);
};

// Owns the Vulkan create-info data needed to build a graphics pipeline.
// Internal pointers are repaired after copies and moves.
struct PipelineConfigInfo {
	PipelineConfigInfo() = default;
	PipelineConfigInfo(const PipelineConfigInfo &other);
	PipelineConfigInfo &operator=(const PipelineConfigInfo &other);
	PipelineConfigInfo(PipelineConfigInfo &&other) noexcept;
	PipelineConfigInfo &operator=(PipelineConfigInfo &&other) noexcept;

	VkPipelineVertexInputStateCreateInfo   vertexInputInfo;
	VkPipelineViewportStateCreateInfo      viewportInfo;
	VkPipelineInputAssemblyStateCreateInfo inputAssemblyInfo;
	VkPipelineRasterizationStateCreateInfo rasterizationInfo;
	VkPipelineMultisampleStateCreateInfo   multisampleInfo;
	VkPipelineColorBlendAttachmentState    colorBlendAttachment;
	VkPipelineColorBlendStateCreateInfo    colorBlendInfo;
	VkPipelineDepthStencilStateCreateInfo  depthStencilInfo;
	std::vector<VkDynamicState>            dynamicStates;
	VkPipelineDynamicStateCreateInfo       dynamicStateInfo;
	BorrowedPipelineLayout                 pipelineLayout;
	VkRenderPass                           renderPass     = VK_NULL_HANDLE;
	std::uint32_t                          subpass        = 0;

	PipelineConfigInfo &setVertexInput(const VertexInputDescription &description);
	PipelineConfigInfo &setRenderPass(VkRenderPass renderPass) noexcept;
	PipelineConfigInfo &setPipelineLayout(BorrowedPipelineLayout pipelineLayout) noexcept;

  private:
	void refreshInternalPointers() noexcept;

	std::vector<VkVertexInputBindingDescription>   vertexBindings_;
	std::vector<VkVertexInputAttributeDescription> vertexAttributes_;
};

} // namespace oxide
