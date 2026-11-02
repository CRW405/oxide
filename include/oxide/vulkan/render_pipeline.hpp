#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <string>
#include <vector>

#include <oxide/vulkan/vulkan_device.hpp>

namespace oxide {

// ============================================================================
// Vertex input descriptions
// ============================================================================
struct VertexInputDescription {
	std::vector<VkVertexInputBindingDescription>   bindings;
	std::vector<VkVertexInputAttributeDescription> attributes;

	VertexInputDescription &addBinding(
	    uint32_t binding,
	    uint32_t stride,
	    VkVertexInputRate inputRate = VK_VERTEX_INPUT_RATE_VERTEX);
	VertexInputDescription &addAttribute(
	    uint32_t location,
	    uint32_t binding,
	    VkFormat format,
	    uint32_t offset);
};

// ============================================================================
// Pipeline configuration
//
// Describes the fixed-function state used to create a graphics pipeline.
// Vulkan stores pointers in several create-info structs, so this type repairs
// its internal pointers whenever it is copied or moved.
// ============================================================================
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
	VkPipelineLayout                       pipelineLayout = VK_NULL_HANDLE;
	VkRenderPass                           renderPass     = VK_NULL_HANDLE;
	uint32_t                               subpass        = 0;

	// Convenience helpers for the common setup. The public fields below remain
	// available for applications that need to override individual values.
	PipelineConfigInfo &setVertexInput(const VertexInputDescription &description);
	PipelineConfigInfo &setRenderPass(VkRenderPass renderPass) noexcept;
	PipelineConfigInfo &setPipelineLayout(VkPipelineLayout pipelineLayout) noexcept;

  private:
	void refreshInternalPointers() noexcept;

	std::vector<VkVertexInputBindingDescription>   vertexBindings_;
	std::vector<VkVertexInputAttributeDescription> vertexAttributes_;
};

class Pipeline {
  public:
	Pipeline(
	    VulkanDevice             &device,
	    const std::string        &vertFilepath,
	    const std::string        &fragFilepath,
	    const PipelineConfigInfo &configInfo);

	~Pipeline();

	Pipeline(const Pipeline &)            = delete;
	Pipeline &operator=(const Pipeline &) = delete;

	Pipeline(Pipeline &&) noexcept;
	Pipeline &operator=(Pipeline &&) = delete;

	void bind(VkCommandBuffer commandBuffer);

	// Draw helpers assume this pipeline is already bound. Their default
	// arguments cover the common single-instance, start-of-buffer case.
	void draw(
	    VkCommandBuffer commandBuffer,
	    std::uint32_t   vertexCount,
	    std::uint32_t   instanceCount = 1,
	    std::uint32_t   firstVertex   = 0,
	    std::uint32_t   firstInstance = 0) const noexcept;
	void drawIndexed(
	    VkCommandBuffer commandBuffer,
	    std::uint32_t   indexCount,
	    std::uint32_t   instanceCount = 1,
	    std::uint32_t   firstIndex    = 0,
	    std::int32_t    vertexOffset  = 0,
	    std::uint32_t   firstInstance = 0) const noexcept;

	[[nodiscard]] VkPipeline       pipeline() const noexcept { return pipeline_; }
	[[nodiscard]] VkPipelineLayout layout() const noexcept { return layout_; }

	static PipelineConfigInfo defaultPipelineConfigInfo();

  private:
	VulkanDevice    &device_;
	VkPipeline       pipeline_         = VK_NULL_HANDLE;
	VkPipelineLayout layout_           = VK_NULL_HANDLE;
	VkShaderModule   vertShaderModule_ = VK_NULL_HANDLE;
	VkShaderModule   fragShaderModule_ = VK_NULL_HANDLE;
	bool             owns_layout_      = false;
};

// ============================================================================
// Compute pipeline
//
// Builds a compute pipeline from one SPIR-V file. Pass a PipelineLayout when
// the shader uses descriptors or push constants; otherwise the pipeline owns an
// empty layout. Dispatch after binding the pipeline and any descriptor sets.
// ============================================================================
class ComputePipeline {
  public:
	ComputePipeline(
	    VulkanDevice      &device,
	    const std::string &compFilepath,
	    VkPipelineLayout   layout = VK_NULL_HANDLE);

	~ComputePipeline();

	ComputePipeline(const ComputePipeline &)            = delete;
	ComputePipeline &operator=(const ComputePipeline &) = delete;

	ComputePipeline(ComputePipeline &&) noexcept;
	ComputePipeline &operator=(ComputePipeline &&) = delete;

	void bind(VkCommandBuffer commandBuffer) const;
	void dispatch(
	    VkCommandBuffer commandBuffer,
	    std::uint32_t   groupCountX = 1,
	    std::uint32_t   groupCountY = 1,
	    std::uint32_t   groupCountZ = 1) const noexcept;

	[[nodiscard]] VkPipeline       pipeline() const noexcept { return pipeline_; }
	[[nodiscard]] VkPipelineLayout layout() const noexcept { return layout_; }

  private:
	VulkanDevice    &device_;
	VkPipeline       pipeline_    = VK_NULL_HANDLE;
	VkPipelineLayout layout_      = VK_NULL_HANDLE;
	bool             owns_layout_ = false;
};

}
