#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <memory>
#include <string>

#include <oxide/vulkan/pipeline/pipeline_config.hpp>
#include <oxide/vulkan/pipeline/shader_library.hpp>
#include <oxide/vulkan/device/vulkan_device.hpp>

namespace oxide {

class VulkanContext;

class Pipeline {
  public:
	Pipeline(
	    VulkanDevice             &device,
	    const std::string        &vertFilepath,
	    const std::string        &fragFilepath,
	    const PipelineConfigInfo &configInfo);
	Pipeline(VulkanDevice &device, const GraphicsShaderFiles &shaders, const PipelineConfigInfo &configInfo);

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

	[[nodiscard]] VkPipeline       pipeline() const noexcept;
	[[nodiscard]] VkPipelineLayout layout() const noexcept;
	[[nodiscard]] PipelineHandle handle() const noexcept { return handle_; }

	static PipelineConfigInfo defaultPipelineConfigInfo();

  private:
	VulkanDevice    &device_;
	VkPipelineLayout layout_           = VK_NULL_HANDLE;
	bool             owns_layout_      = false;
	PipelineHandle   handle_{};
	ShaderHandle     vert_shader_handle_{};
	ShaderHandle     frag_shader_handle_{};
	PipelineLayoutHandle layout_handle_{};
};

// Rebuilds a graphics pipeline when a swapchain format change replaces the
// render pass it was created against.
class SwapchainPipeline {
  public:
	SwapchainPipeline(
	    VulkanContext       &context,
	    VulkanDevice        &device,
	    const std::string   &vertFilepath,
	    const std::string   &fragFilepath,
	    PipelineConfigInfo   configInfo);
	SwapchainPipeline(
	    VulkanContext       &context,
	    VulkanDevice        &device,
	    GraphicsShaderFiles  shaders,
	    PipelineConfigInfo   configInfo);
	~SwapchainPipeline();

	SwapchainPipeline(const SwapchainPipeline &)            = delete;
	SwapchainPipeline &operator=(const SwapchainPipeline &) = delete;
	SwapchainPipeline(SwapchainPipeline &&)                 = delete;
	SwapchainPipeline &operator=(SwapchainPipeline &&)      = delete;

	void bind(VkCommandBuffer commandBuffer) const { pipeline_->bind(commandBuffer); }
	[[nodiscard]] Pipeline &pipeline() noexcept { return *pipeline_; }
	[[nodiscard]] const Pipeline &pipeline() const noexcept { return *pipeline_; }

  private:
	void rebuild(VkRenderPass renderPass);

	VulkanContext     &context_;
	VulkanDevice      &device_;
	std::string        vertFilepath_;
	std::string        fragFilepath_;
	PipelineConfigInfo config_;
	std::unique_ptr<Pipeline> pipeline_;
	struct CallbackState;
	std::shared_ptr<CallbackState> callback_state_;
	std::uint64_t                  callback_id_ = 0;
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
	    BorrowedPipelineLayout layout = {},
	    const SpecializationConstants &specialization = {});
	ComputePipeline(
	    VulkanDevice             &device,
	    const ComputeShaderFile  &shader,
	    BorrowedPipelineLayout    layout = {},
	    const SpecializationConstants &specialization = {});

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

	[[nodiscard]] VkPipeline       pipeline() const noexcept;
	[[nodiscard]] VkPipelineLayout layout() const noexcept;
	[[nodiscard]] PipelineHandle handle() const noexcept { return handle_; }

  private:
	VulkanDevice    &device_;
	VkPipelineLayout layout_      = VK_NULL_HANDLE;
	bool             owns_layout_ = false;
	PipelineHandle   handle_{};
	ShaderHandle     shader_handle_{};
	PipelineLayoutHandle layout_handle_{};
};

}
