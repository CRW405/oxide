#include <oxide/vulkan/render_pipeline.hpp>
#include <oxide/tools/log.hpp>
#include <oxide/vulkan/vk_error.hpp>

#include <fstream>
#include <stdexcept>
#include <utility>

namespace oxide {

namespace {

std::vector<char> readShaderFile(const std::string &filepath) {
	// Shader binaries are read as raw bytes for VkShaderModule creation.
	std::ifstream file(filepath, std::ios::ate | std::ios::binary);

	if (!file.is_open()) {
		throw runtimeError("failed to open shader file: " + filepath);
	}

	const std::streampos end = file.tellg();
	if (end == std::streampos(-1)) {
		throw runtimeError("failed to determine shader file size: " + filepath);
	}
	const auto file_size = static_cast<std::size_t>(end);
	if (file_size == 0) {
		throw runtimeError("shader file is empty: " + filepath);
	}
	if (file_size % 4 != 0) {
		throw runtimeError("shader file size is not a multiple of 4 bytes: " + filepath);
	}

	std::vector<char> buffer(file_size);
	file.seekg(0);
	if (!file.read(buffer.data(), static_cast<std::streamsize>(file_size))) {
		throw runtimeError("failed to read shader file: " + filepath);
	}

	return buffer;
}

VkShaderModule createShaderModule(
    VulkanDevice            &device,
    const std::vector<char> &code,
    const std::string       &filepath) {
	// Vulkan consumes SPIR-V as 32-bit words even though files are byte streams.
	VkShaderModuleCreateInfo create_info{};
	create_info.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	create_info.codeSize = code.size();
	create_info.pCode    = reinterpret_cast<const std::uint32_t *>(code.data());

	VkShaderModule module = VK_NULL_HANDLE;
	checkVk(vkCreateShaderModule(device.device(), &create_info, nullptr, &module),
	        "create shader module for " + filepath);
	return module;
}

} // namespace

// -----------------------------------------------------------------------------
// Vertex input and graphics pipeline setup
// -----------------------------------------------------------------------------
VertexInputDescription &VertexInputDescription::addBinding(
    uint32_t binding,
    uint32_t stride,
    VkVertexInputRate inputRate) {
	bindings.push_back({ binding, stride, inputRate });
	return *this;
}

VertexInputDescription &VertexInputDescription::addAttribute(
    uint32_t location,
    uint32_t binding,
    VkFormat format,
    uint32_t offset) {
	attributes.push_back({ location, binding, format, offset });
	return *this;
}

// -----------------------------------------------------------------------------
// Pipeline creation has two independent inputs: shader modules and the
// fixed-function configuration. The shader modules are temporary construction
// resources; the pipeline and its layout remain available for command recording.
// -----------------------------------------------------------------------------
PipelineConfigInfo::PipelineConfigInfo(const PipelineConfigInfo &other)
    : vertexInputInfo(other.vertexInputInfo),
      viewportInfo(other.viewportInfo),
      inputAssemblyInfo(other.inputAssemblyInfo),
      rasterizationInfo(other.rasterizationInfo),
      multisampleInfo(other.multisampleInfo),
      colorBlendAttachment(other.colorBlendAttachment),
      colorBlendInfo(other.colorBlendInfo),
      depthStencilInfo(other.depthStencilInfo),
      dynamicStates(other.dynamicStates),
      dynamicStateInfo(other.dynamicStateInfo),
      pipelineLayout(other.pipelineLayout),
      renderPass(other.renderPass),
      subpass(other.subpass),
      vertexBindings_(other.vertexBindings_),
      vertexAttributes_(other.vertexAttributes_) {
	refreshInternalPointers();
}

PipelineConfigInfo &PipelineConfigInfo::operator=(const PipelineConfigInfo &other) {
	if (this != &other) {
		vertexInputInfo      = other.vertexInputInfo;
		viewportInfo         = other.viewportInfo;
		inputAssemblyInfo    = other.inputAssemblyInfo;
		rasterizationInfo    = other.rasterizationInfo;
		multisampleInfo      = other.multisampleInfo;
		colorBlendAttachment = other.colorBlendAttachment;
		colorBlendInfo       = other.colorBlendInfo;
		depthStencilInfo     = other.depthStencilInfo;
		dynamicStates        = other.dynamicStates;
		dynamicStateInfo     = other.dynamicStateInfo;
		pipelineLayout       = other.pipelineLayout;
		renderPass           = other.renderPass;
		subpass              = other.subpass;
		vertexBindings_      = other.vertexBindings_;
		vertexAttributes_    = other.vertexAttributes_;
		refreshInternalPointers();
	}
	return *this;
}

PipelineConfigInfo::PipelineConfigInfo(PipelineConfigInfo &&other) noexcept
    : vertexInputInfo(other.vertexInputInfo),
      viewportInfo(other.viewportInfo),
      inputAssemblyInfo(other.inputAssemblyInfo),
      rasterizationInfo(other.rasterizationInfo),
      multisampleInfo(other.multisampleInfo),
      colorBlendAttachment(other.colorBlendAttachment),
      colorBlendInfo(other.colorBlendInfo),
      depthStencilInfo(other.depthStencilInfo),
      dynamicStates(std::move(other.dynamicStates)),
      dynamicStateInfo(other.dynamicStateInfo),
      pipelineLayout(other.pipelineLayout),
      renderPass(other.renderPass),
      subpass(other.subpass),
      vertexBindings_(std::move(other.vertexBindings_)),
      vertexAttributes_(std::move(other.vertexAttributes_)) {
	refreshInternalPointers();
}

PipelineConfigInfo &PipelineConfigInfo::operator=(PipelineConfigInfo &&other) noexcept {
	if (this != &other) {
		vertexInputInfo      = other.vertexInputInfo;
		viewportInfo         = other.viewportInfo;
		inputAssemblyInfo    = other.inputAssemblyInfo;
		rasterizationInfo    = other.rasterizationInfo;
		multisampleInfo      = other.multisampleInfo;
		colorBlendAttachment = other.colorBlendAttachment;
		colorBlendInfo       = other.colorBlendInfo;
		depthStencilInfo     = other.depthStencilInfo;
		dynamicStates        = std::move(other.dynamicStates);
		dynamicStateInfo     = other.dynamicStateInfo;
		pipelineLayout       = other.pipelineLayout;
		renderPass           = other.renderPass;
		subpass              = other.subpass;
		vertexBindings_      = std::move(other.vertexBindings_);
		vertexAttributes_    = std::move(other.vertexAttributes_);
		refreshInternalPointers();
	}
	return *this;
}

void PipelineConfigInfo::refreshInternalPointers() noexcept {
	// -------------------------------------------------------------------------
	// These pointers refer to members owned by this config. Refreshing them is
	// required after a copy or move because the object's address may change.
	// -------------------------------------------------------------------------
	colorBlendInfo.pAttachments     = &colorBlendAttachment;
	dynamicStateInfo.pDynamicStates = dynamicStates.data();
	vertexInputInfo.pVertexBindingDescriptions    = vertexBindings_.data();
	vertexInputInfo.vertexBindingDescriptionCount  = static_cast<uint32_t>(vertexBindings_.size());
	vertexInputInfo.pVertexAttributeDescriptions   = vertexAttributes_.data();
	vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(vertexAttributes_.size());
}

// -----------------------------------------------------------------------------
// Pipeline configuration helpers
//
// These setters cover the common setup while leaving every field public. An
// application can keep chaining the helpers or assign the Vulkan fields
// directly when it needs different fixed-function state.
// -----------------------------------------------------------------------------
PipelineConfigInfo &PipelineConfigInfo::setVertexInput(const VertexInputDescription &description) {
	vertexBindings_   = description.bindings;
	vertexAttributes_ = description.attributes;
	refreshInternalPointers();
	return *this;
}

PipelineConfigInfo &PipelineConfigInfo::setRenderPass(VkRenderPass renderPass) noexcept {
	this->renderPass = renderPass;
	return *this;
}

PipelineConfigInfo &PipelineConfigInfo::setPipelineLayout(VkPipelineLayout pipelineLayout) noexcept {
	this->pipelineLayout = pipelineLayout;
	return *this;
}

Pipeline::Pipeline(
    VulkanDevice             &device,
    const std::string        &vertFilepath,
    const std::string        &fragFilepath,
    const PipelineConfigInfo &configInfo)
    : device_(device) {

	// -------------------------------------------------------------------------
	// Step 1: load shader bytecode and create temporary shader modules. The
	// modules are needed only while the graphics pipeline is being built.
	// -------------------------------------------------------------------------
	auto vertCode = readShaderFile(vertFilepath);
	auto fragCode = readShaderFile(fragFilepath);

	try {
		vertShaderModule_ = createShaderModule(device_, vertCode, vertFilepath);
		fragShaderModule_ = createShaderModule(device_, fragCode, fragFilepath);

		if (configInfo.pipelineLayout == VK_NULL_HANDLE) {
			// Use an empty layout until descriptors or push constants are needed.
			VkPipelineLayoutCreateInfo layoutInfo{};
			layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
			checkVk(vkCreatePipelineLayout(device_.device(), &layoutInfo, nullptr, &layout_),
			        "create default pipeline layout");
			owns_layout_ = true;
		} else {
			layout_ = configInfo.pipelineLayout;
		}

		// -------------------------------------------------------------------------
		// Step 2: connect shader stages to the fixed-function configuration and
		// create the graphics pipeline for the context's render pass.
		// -------------------------------------------------------------------------
		VkPipelineShaderStageCreateInfo shaderStages[2]{};
		shaderStages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		shaderStages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
		shaderStages[0].module = vertShaderModule_;
		shaderStages[0].pName  = "main";

		shaderStages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		shaderStages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
		shaderStages[1].module = fragShaderModule_;
		shaderStages[1].pName  = "main";

		VkGraphicsPipelineCreateInfo pipelineInfo{};
		pipelineInfo.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
		pipelineInfo.stageCount          = 2;
		pipelineInfo.pStages             = shaderStages;
		pipelineInfo.pVertexInputState   = &configInfo.vertexInputInfo;
		pipelineInfo.pInputAssemblyState = &configInfo.inputAssemblyInfo;
		pipelineInfo.pViewportState      = &configInfo.viewportInfo;
		pipelineInfo.pRasterizationState = &configInfo.rasterizationInfo;
		pipelineInfo.pMultisampleState   = &configInfo.multisampleInfo;
		pipelineInfo.pColorBlendState    = &configInfo.colorBlendInfo;
		pipelineInfo.pDepthStencilState  = &configInfo.depthStencilInfo;
		pipelineInfo.pDynamicState       = &configInfo.dynamicStateInfo;

		pipelineInfo.layout     = layout_;
		pipelineInfo.renderPass = configInfo.renderPass;
		pipelineInfo.subpass    = configInfo.subpass;

		pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;
		pipelineInfo.basePipelineIndex  = -1;

		checkVk(vkCreateGraphicsPipelines(device_.device(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline_),
		        "create graphics pipeline");
	} catch (...) {
		if (fragShaderModule_ != VK_NULL_HANDLE) {
			vkDestroyShaderModule(device_.device(), fragShaderModule_, nullptr);
			fragShaderModule_ = VK_NULL_HANDLE;
		}
		if (vertShaderModule_ != VK_NULL_HANDLE) {
			vkDestroyShaderModule(device_.device(), vertShaderModule_, nullptr);
			vertShaderModule_ = VK_NULL_HANDLE;
		}
		if (owns_layout_ && layout_ != VK_NULL_HANDLE) {
			vkDestroyPipelineLayout(device_.device(), layout_, nullptr);
			layout_      = VK_NULL_HANDLE;
			owns_layout_ = false;
		}
		throw;
	}
}

Pipeline::~Pipeline() {
	if (device_.device() != VK_NULL_HANDLE) {
		if (vertShaderModule_ != VK_NULL_HANDLE) {
			vkDestroyShaderModule(device_.device(), vertShaderModule_, nullptr);
		}
		if (fragShaderModule_ != VK_NULL_HANDLE) {
			vkDestroyShaderModule(device_.device(), fragShaderModule_, nullptr);
		}
		if (pipeline_ != VK_NULL_HANDLE) {
			vkDestroyPipeline(device_.device(), pipeline_, nullptr);
		}
		if (owns_layout_ && layout_ != VK_NULL_HANDLE) {
			vkDestroyPipelineLayout(device_.device(), layout_, nullptr);
		}
	}
}

Pipeline::Pipeline(Pipeline &&other) noexcept
    : device_(other.device_),
      pipeline_(other.pipeline_),
      layout_(other.layout_),
      vertShaderModule_(other.vertShaderModule_),
      fragShaderModule_(other.fragShaderModule_),
      owns_layout_(other.owns_layout_) {
	other.pipeline_         = VK_NULL_HANDLE;
	other.layout_           = VK_NULL_HANDLE;
	other.vertShaderModule_ = VK_NULL_HANDLE;
	other.fragShaderModule_ = VK_NULL_HANDLE;
	other.owns_layout_      = false;
}

void Pipeline::bind(VkCommandBuffer commandBuffer) {
	// Binding makes this pipeline the active graphics state for later draws.
	vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
}

void Pipeline::draw(
    VkCommandBuffer commandBuffer,
    std::uint32_t   vertexCount,
    std::uint32_t   instanceCount,
    std::uint32_t   firstVertex,
    std::uint32_t   firstInstance) const noexcept {
	vkCmdDraw(commandBuffer, vertexCount, instanceCount, firstVertex, firstInstance);
}

void Pipeline::drawIndexed(
    VkCommandBuffer commandBuffer,
    std::uint32_t   indexCount,
    std::uint32_t   instanceCount,
    std::uint32_t   firstIndex,
    std::int32_t    vertexOffset,
    std::uint32_t   firstInstance) const noexcept {
	vkCmdDrawIndexed(commandBuffer,
	                 indexCount,
	                 instanceCount,
	                 firstIndex,
	                 vertexOffset,
	                 firstInstance);
}

// -----------------------------------------------------------------------------
// Compute pipeline creation
// -----------------------------------------------------------------------------
ComputePipeline::ComputePipeline(
    VulkanDevice      &device,
    const std::string &compFilepath,
    VkPipelineLayout   layout)
    : device_(device) {
	const auto code = readShaderFile(compFilepath);

	VkShaderModule module = VK_NULL_HANDLE;
	try {
		module = createShaderModule(device_, code, compFilepath);

		if (layout == VK_NULL_HANDLE) {
			VkPipelineLayoutCreateInfo layout_info{};
			layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
			checkVk(vkCreatePipelineLayout(device_.device(), &layout_info, nullptr, &layout_),
			        "create compute pipeline layout");
			owns_layout_ = true;
		} else {
			layout_ = layout;
		}

		VkComputePipelineCreateInfo pipeline_info{};
		pipeline_info.sType        = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
		pipeline_info.stage.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		pipeline_info.stage.stage  = VK_SHADER_STAGE_COMPUTE_BIT;
		pipeline_info.stage.module = module;
		pipeline_info.stage.pName  = "main";
		pipeline_info.layout       = layout_;
		pipeline_info.basePipelineHandle = VK_NULL_HANDLE;
		pipeline_info.basePipelineIndex  = -1;

		checkVk(vkCreateComputePipelines(device_.device(), VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &pipeline_),
		        "create compute pipeline");

		vkDestroyShaderModule(device_.device(), module, nullptr);
	} catch (...) {
		if (module != VK_NULL_HANDLE) {
			vkDestroyShaderModule(device_.device(), module, nullptr);
		}
		if (owns_layout_ && layout_ != VK_NULL_HANDLE) {
			vkDestroyPipelineLayout(device_.device(), layout_, nullptr);
			layout_      = VK_NULL_HANDLE;
			owns_layout_ = false;
		}
		throw;
	}
}

ComputePipeline::~ComputePipeline() {
	if (device_.device() != VK_NULL_HANDLE) {
		if (pipeline_ != VK_NULL_HANDLE) {
			vkDestroyPipeline(device_.device(), pipeline_, nullptr);
		}
		if (owns_layout_ && layout_ != VK_NULL_HANDLE) {
			vkDestroyPipelineLayout(device_.device(), layout_, nullptr);
		}
	}
}

ComputePipeline::ComputePipeline(ComputePipeline &&other) noexcept
    : device_(other.device_),
      pipeline_(other.pipeline_),
      layout_(other.layout_),
      owns_layout_(other.owns_layout_) {
	other.pipeline_    = VK_NULL_HANDLE;
	other.layout_      = VK_NULL_HANDLE;
	other.owns_layout_ = false;
}

void ComputePipeline::bind(VkCommandBuffer commandBuffer) const {
	vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_);
}

void ComputePipeline::dispatch(
    VkCommandBuffer commandBuffer,
    std::uint32_t   groupCountX,
    std::uint32_t   groupCountY,
    std::uint32_t   groupCountZ) const noexcept {
	vkCmdDispatch(commandBuffer, groupCountX, groupCountY, groupCountZ);
}

PipelineConfigInfo Pipeline::defaultPipelineConfigInfo() {
	// -------------------------------------------------------------------------
	// Establish the smallest useful triangle pipeline: fixed-function defaults,
	// dynamic viewport/scissor state, and no depth attachment.
	// -------------------------------------------------------------------------
	PipelineConfigInfo configInfo{};

	configInfo.inputAssemblyInfo.sType                  = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	configInfo.inputAssemblyInfo.topology               = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	configInfo.inputAssemblyInfo.primitiveRestartEnable = VK_FALSE;

	configInfo.viewportInfo.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	configInfo.viewportInfo.viewportCount = 1;
	configInfo.viewportInfo.pViewports    = nullptr;
	configInfo.viewportInfo.scissorCount  = 1;
	configInfo.viewportInfo.pScissors     = nullptr;

	configInfo.rasterizationInfo.sType                   = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	configInfo.rasterizationInfo.depthClampEnable        = VK_FALSE;
	configInfo.rasterizationInfo.rasterizerDiscardEnable = VK_FALSE;
	configInfo.rasterizationInfo.polygonMode             = VK_POLYGON_MODE_FILL;
	configInfo.rasterizationInfo.lineWidth               = 1.0f;
	configInfo.rasterizationInfo.cullMode                = VK_CULL_MODE_NONE;
	configInfo.rasterizationInfo.frontFace               = VK_FRONT_FACE_CLOCKWISE;
	configInfo.rasterizationInfo.depthBiasEnable         = VK_FALSE;

	configInfo.multisampleInfo.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	configInfo.multisampleInfo.sampleShadingEnable  = VK_FALSE;
	configInfo.multisampleInfo.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

	configInfo.colorBlendAttachment.colorWriteMask =
	    VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
	configInfo.colorBlendAttachment.blendEnable = VK_FALSE;

	configInfo.colorBlendInfo.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	configInfo.colorBlendInfo.logicOpEnable   = VK_FALSE;
	configInfo.colorBlendInfo.attachmentCount = 1;
	configInfo.colorBlendInfo.pAttachments    = &configInfo.colorBlendAttachment;

	configInfo.depthStencilInfo.sType                 = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
	configInfo.depthStencilInfo.depthTestEnable       = VK_FALSE;
	configInfo.depthStencilInfo.depthWriteEnable      = VK_TRUE;
	configInfo.depthStencilInfo.depthCompareOp        = VK_COMPARE_OP_LESS;
	configInfo.depthStencilInfo.depthBoundsTestEnable = VK_FALSE;
	configInfo.depthStencilInfo.stencilTestEnable     = VK_FALSE;

	configInfo.dynamicStates                      = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
	configInfo.dynamicStateInfo.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	configInfo.dynamicStateInfo.dynamicStateCount = static_cast<uint32_t>(configInfo.dynamicStates.size());
	configInfo.dynamicStateInfo.pDynamicStates    = configInfo.dynamicStates.data();

	configInfo.vertexInputInfo.sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	configInfo.vertexInputInfo.vertexBindingDescriptionCount   = 0;
	configInfo.vertexInputInfo.pVertexBindingDescriptions      = nullptr;
	configInfo.vertexInputInfo.vertexAttributeDescriptionCount = 0;
	configInfo.vertexInputInfo.pVertexAttributeDescriptions    = nullptr;

	return configInfo;
}

} // namespace oxide
