#include <oxide/vulkan/pipeline/render_pipeline.hpp>
#include <oxide/vulkan/frame/vulkan_context.hpp>
#include <oxide/tools/log.hpp>
#include <oxide/vulkan/core/vk_error.hpp>

#include <fstream>
#include <stdexcept>
#include <utility>

namespace oxide {

struct SwapchainPipeline::CallbackState {
	SwapchainPipeline *owner = nullptr;
};

SwapchainPipeline::SwapchainPipeline(
    VulkanContext       &context,
    VulkanDevice        &device,
    const std::string   &vertFilepath,
    const std::string   &fragFilepath,
    PipelineConfigInfo   configInfo)
    : context_(context),
      device_(device),
      vertFilepath_(vertFilepath),
      fragFilepath_(fragFilepath),
      config_(std::move(configInfo)),
      callback_state_(std::make_shared<CallbackState>()) {
	callback_state_->owner = this;
	config_.setRenderPass(context_.renderPass());
	rebuild(context_.renderPass());
	const std::weak_ptr<CallbackState> state = callback_state_;
	callback_id_ = context_.addSwapchainRecreatedCallback([state](const SwapchainRecreationInfo &info) {
		if (const std::shared_ptr<CallbackState> callback = state.lock();
		    callback != nullptr && callback->owner != nullptr && info.renderPassChanged) {
			callback->owner->rebuild(info.renderPass);
		}
	});
}

SwapchainPipeline::SwapchainPipeline(
    VulkanContext       &context,
    VulkanDevice        &device,
    GraphicsShaderFiles  shaders,
    PipelineConfigInfo   configInfo)
    : SwapchainPipeline(context, device, shaders.vertex, shaders.fragment, std::move(configInfo)) {}

void SwapchainPipeline::rebuild(VkRenderPass renderPass) {
	// The pipeline must always match the context's render pass sample count,
	// including after swapchain recreation falls back to a different count.
	config_.setRenderPass(renderPass);
	config_.setMultisampleCount(context_.sampleCount());
	pipeline_ = std::make_unique<Pipeline>(device_, vertFilepath_, fragFilepath_, config_);
}

SwapchainPipeline::~SwapchainPipeline() {
	if (callback_state_ != nullptr) {
		callback_state_->owner = nullptr;
	}
	if (callback_id_ != 0) {
		context_.removeSwapchainRecreatedCallback(callback_id_);
	}
}

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

Pipeline::Pipeline(
    VulkanDevice             &device,
    const std::string        &vertFilepath,
    const std::string        &fragFilepath,
    const PipelineConfigInfo &configInfo)
    : device_(device) {
	if (configInfo.renderPass == VK_NULL_HANDLE) {
		throw invalidArgument("graphics pipeline requires a valid render pass");
	}
	VkShaderModule vertex_shader_module   = VK_NULL_HANDLE;
	VkShaderModule fragment_shader_module = VK_NULL_HANDLE;
	try {
		auto acquireShader = [this](const std::string &path, ShaderHandle &handle) {
			handle = device_.resources().acquireShader(path);
			if (handle.valid()) {
				const auto *resource = device_.resources().shader(handle);
				return resource == nullptr ? VK_NULL_HANDLE : resource->module;
			}
			const auto code = readShaderFile(path);
			const VkShaderModule module = createShaderModule(device_, code, path);
			handle = device_.resources().registerShader({module, 1, path});
			return module;
		};
		vertex_shader_module = acquireShader(vertFilepath, vert_shader_handle_);
		fragment_shader_module = acquireShader(fragFilepath, frag_shader_handle_);

		if (!configInfo.pipelineLayout.valid()) {
			// Use an empty layout until descriptors or push constants are needed.
			VkPipelineLayoutCreateInfo layoutInfo{};
			layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
			checkVk(vkCreatePipelineLayout(device_.device(), &layoutInfo, nullptr, &layout_),
			        "create default pipeline layout");
			owns_layout_ = true;
			layout_handle_ = device_.resources().registerPipelineLayout({layout_});
		} else {
			layout_ = configInfo.pipelineLayout.get();
		}

		VkSpecializationInfo vertex_specialization = configInfo.vertexSpecialization.info();
		VkSpecializationInfo fragment_specialization = configInfo.fragmentSpecialization.info();

		VkPipelineShaderStageCreateInfo shaderStages[2]{};
		shaderStages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		shaderStages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
		shaderStages[0].module = vertex_shader_module;
		shaderStages[0].pName  = "main";
		shaderStages[0].pSpecializationInfo =
		    configInfo.vertexSpecialization.empty() ? nullptr : &vertex_specialization;

		shaderStages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		shaderStages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
		shaderStages[1].module = fragment_shader_module;
		shaderStages[1].pName  = "main";
		shaderStages[1].pSpecializationInfo =
		    configInfo.fragmentSpecialization.empty() ? nullptr : &fragment_specialization;

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

		VkPipeline pipeline = VK_NULL_HANDLE;
		checkVk(vkCreateGraphicsPipelines(device_.device(), device_.pipelineCache(), 1, &pipelineInfo, nullptr, &pipeline),
		        "create graphics pipeline");
		handle_ = device_.resources().registerPipeline({pipeline, layout_});
	} catch (...) {
		if (frag_shader_handle_.valid()) {
			device_.resources().unregisterShader(frag_shader_handle_);
			frag_shader_handle_ = {};
		}
		if (vert_shader_handle_.valid()) {
			device_.resources().unregisterShader(vert_shader_handle_);
			vert_shader_handle_ = {};
		}
		if (owns_layout_ && layout_ != VK_NULL_HANDLE) {
			device_.resources().unregisterPipelineLayout(layout_handle_);
			layout_      = VK_NULL_HANDLE;
			owns_layout_ = false;
			layout_handle_ = {};
		}
		throw;
	}
}

Pipeline::Pipeline(
    VulkanDevice &device,
    const GraphicsShaderFiles &shaders,
    const PipelineConfigInfo &configInfo)
    : Pipeline(device, shaders.vertex, shaders.fragment, configInfo) {}

Pipeline::~Pipeline() {
	if (device_.device() != VK_NULL_HANDLE) {
		device_.resources().unregisterPipeline(handle_);
		if (vert_shader_handle_.valid()) {
			device_.resources().unregisterShader(vert_shader_handle_);
		}
		if (frag_shader_handle_.valid()) {
			device_.resources().unregisterShader(frag_shader_handle_);
		}
		if (owns_layout_ && layout_ != VK_NULL_HANDLE) {
			device_.resources().unregisterPipelineLayout(layout_handle_);
			layout_ = VK_NULL_HANDLE;
			layout_handle_ = {};
		}
	}
}

Pipeline::Pipeline(Pipeline &&other) noexcept
    : device_(other.device_),
      layout_(other.layout_),
      owns_layout_(other.owns_layout_),
      handle_(other.handle_),
      vert_shader_handle_(other.vert_shader_handle_),
      frag_shader_handle_(other.frag_shader_handle_),
      layout_handle_(other.layout_handle_) {
	other.layout_           = VK_NULL_HANDLE;
	other.owns_layout_      = false;
	other.handle_            = {};
	other.vert_shader_handle_ = {};
	other.frag_shader_handle_ = {};
	other.layout_handle_ = {};
}

VkPipeline Pipeline::pipeline() const noexcept {
	const auto *resource = device_.resources().pipeline(handle_);
	return resource == nullptr ? VK_NULL_HANDLE : resource->pipeline;
}

VkPipelineLayout Pipeline::layout() const noexcept {
	if (layout_handle_.valid()) {
		const auto *resource = device_.resources().pipelineLayout(layout_handle_);
		return resource == nullptr ? VK_NULL_HANDLE : resource->layout;
	}
	return layout_;
}

void Pipeline::bind(VkCommandBuffer commandBuffer) {
	vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline());
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

ComputePipeline::ComputePipeline(
    VulkanDevice      &device,
    const std::string &compFilepath,
    BorrowedPipelineLayout layout,
    const SpecializationConstants &specialization)
    : device_(device) {
	VkShaderModule module = VK_NULL_HANDLE;
	VkPipeline pipeline = VK_NULL_HANDLE;
	try {
	shader_handle_ = device_.resources().acquireShader(compFilepath);
	if (shader_handle_.valid()) {
		const auto *resource = device_.resources().shader(shader_handle_);
		module = resource == nullptr ? VK_NULL_HANDLE : resource->module;
	} else {
		const auto code = readShaderFile(compFilepath);
		module = createShaderModule(device_, code, compFilepath);
		shader_handle_ = device_.resources().registerShader({module, 1, compFilepath});
	}

		if (!layout.valid()) {
			VkPipelineLayoutCreateInfo layout_info{};
			layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
			checkVk(vkCreatePipelineLayout(device_.device(), &layout_info, nullptr, &layout_),
			        "create compute pipeline layout");
			owns_layout_ = true;
			layout_handle_ = device_.resources().registerPipelineLayout({layout_});
		} else {
			layout_ = layout.get();
		}

		VkSpecializationInfo specialization_info = specialization.info();

		VkComputePipelineCreateInfo pipeline_info{};
		pipeline_info.sType        = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
		pipeline_info.stage.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		pipeline_info.stage.stage  = VK_SHADER_STAGE_COMPUTE_BIT;
		pipeline_info.stage.module = module;
		pipeline_info.stage.pName  = "main";
		pipeline_info.stage.pSpecializationInfo =
		    specialization.empty() ? nullptr : &specialization_info;
		pipeline_info.layout       = layout_;
		pipeline_info.basePipelineHandle = VK_NULL_HANDLE;
		pipeline_info.basePipelineIndex  = -1;

		checkVk(vkCreateComputePipelines(device_.device(), device_.pipelineCache(), 1, &pipeline_info, nullptr, &pipeline),
		        "create compute pipeline");
		handle_ = device_.resources().registerPipeline({pipeline, layout_});

	} catch (...) {
		if (module != VK_NULL_HANDLE) {
			device_.resources().unregisterShader(shader_handle_);
			shader_handle_ = {};
		}
		if (owns_layout_ && layout_ != VK_NULL_HANDLE) {
			device_.resources().unregisterPipelineLayout(layout_handle_);
			layout_      = VK_NULL_HANDLE;
			owns_layout_ = false;
			layout_handle_ = {};
		}
		throw;
	}
}

ComputePipeline::ComputePipeline(
    VulkanDevice &device,
    const ComputeShaderFile &shader,
    BorrowedPipelineLayout layout,
    const SpecializationConstants &specialization)
    : ComputePipeline(device, shader.compute, layout, specialization) {}

ComputePipeline::~ComputePipeline() {
	if (device_.device() != VK_NULL_HANDLE) {
		device_.resources().unregisterPipeline(handle_);
		device_.resources().unregisterShader(shader_handle_);
		if (owns_layout_ && layout_ != VK_NULL_HANDLE) {
			device_.resources().unregisterPipelineLayout(layout_handle_);
			layout_ = VK_NULL_HANDLE;
			layout_handle_ = {};
		}
	}
}

ComputePipeline::ComputePipeline(ComputePipeline &&other) noexcept
    : device_(other.device_),
      layout_(other.layout_),
      owns_layout_(other.owns_layout_),
      handle_(other.handle_),
      shader_handle_(other.shader_handle_),
      layout_handle_(other.layout_handle_) {
	other.layout_      = VK_NULL_HANDLE;
	other.owns_layout_ = false;
	other.handle_      = {};
	other.shader_handle_ = {};
	other.layout_handle_ = {};
}

VkPipeline ComputePipeline::pipeline() const noexcept {
	const auto *resource = device_.resources().pipeline(handle_);
	return resource == nullptr ? VK_NULL_HANDLE : resource->pipeline;
}

VkPipelineLayout ComputePipeline::layout() const noexcept {
	if (layout_handle_.valid()) {
		const auto *resource = device_.resources().pipelineLayout(layout_handle_);
		return resource == nullptr ? VK_NULL_HANDLE : resource->layout;
	}
	return layout_;
}

void ComputePipeline::bind(VkCommandBuffer commandBuffer) const {
	vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline());
}

void ComputePipeline::dispatch(
    VkCommandBuffer commandBuffer,
    std::uint32_t   groupCountX,
    std::uint32_t   groupCountY,
    std::uint32_t   groupCountZ) const noexcept {
	vkCmdDispatch(commandBuffer, groupCountX, groupCountY, groupCountZ);
}

PipelineConfigInfo Pipeline::defaultPipelineConfigInfo() {
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
