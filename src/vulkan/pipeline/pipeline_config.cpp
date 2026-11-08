#include <oxide/vulkan/pipeline/pipeline_config.hpp>

#include <utility>

namespace oxide {

VertexInputDescription &VertexInputDescription::addBinding(
    std::uint32_t binding,
    std::uint32_t stride,
    VkVertexInputRate inputRate) {
	bindings.push_back({ binding, stride, inputRate });
	return *this;
}

VertexInputDescription &VertexInputDescription::addAttribute(
    std::uint32_t location,
    std::uint32_t binding,
    VkFormat format,
    std::uint32_t offset) {
	attributes.push_back({ location, binding, format, offset });
	return *this;
}

PipelineConfigInfo::PipelineConfigInfo(const PipelineConfigInfo &other)
    : vertexInputInfo(other.vertexInputInfo),
      viewportInfo(other.viewportInfo),
      inputAssemblyInfo(other.inputAssemblyInfo),
      rasterizationInfo(other.rasterizationInfo),
      multisampleInfo(other.multisampleInfo),
      colorBlendAttachment(other.colorBlendAttachment),
      colorBlendAttachments(other.colorBlendAttachments),
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
		colorBlendAttachments = other.colorBlendAttachments;
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
      colorBlendAttachments(std::move(other.colorBlendAttachments)),
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
		colorBlendAttachments = std::move(other.colorBlendAttachments);
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
	if (colorBlendAttachments.empty()) {
		colorBlendInfo.attachmentCount = 1;
		colorBlendInfo.pAttachments    = &colorBlendAttachment;
	} else {
		colorBlendInfo.attachmentCount = static_cast<std::uint32_t>(colorBlendAttachments.size());
		colorBlendInfo.pAttachments    = colorBlendAttachments.data();
	}
	dynamicStateInfo.dynamicStateCount = static_cast<std::uint32_t>(dynamicStates.size());
	dynamicStateInfo.pDynamicStates    = dynamicStates.data();
	vertexInputInfo.pVertexBindingDescriptions = vertexBindings_.data();
	vertexInputInfo.vertexBindingDescriptionCount =
	    static_cast<std::uint32_t>(vertexBindings_.size());
	vertexInputInfo.pVertexAttributeDescriptions = vertexAttributes_.data();
	vertexInputInfo.vertexAttributeDescriptionCount =
	    static_cast<std::uint32_t>(vertexAttributes_.size());
}

PipelineConfigInfo &PipelineConfigInfo::setVertexInput(
    const VertexInputDescription &description) {
	vertexBindings_ = description.bindings;
	vertexAttributes_ = description.attributes;
	refreshInternalPointers();
	return *this;
}

PipelineConfigInfo &PipelineConfigInfo::setRenderPass(VkRenderPass renderPass) noexcept {
	this->renderPass = renderPass;
	return *this;
}

PipelineConfigInfo &PipelineConfigInfo::setPipelineLayout(
    BorrowedPipelineLayout pipelineLayout) noexcept {
	this->pipelineLayout = pipelineLayout;
	return *this;
}

PipelineConfigInfo &PipelineConfigInfo::setColorAttachmentCount(std::uint32_t count) {
	if (count == 0) {
		count = 1;
	}

	VkPipelineColorBlendAttachmentState state = colorBlendAttachment;
	if (!colorBlendAttachments.empty()) {
		state = colorBlendAttachments.front();
	}
	if (state.colorWriteMask == 0) {
		state.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
		                       VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
	}

	colorBlendAttachments.assign(count, state);
	colorBlendAttachment = colorBlendAttachments.front();
	refreshInternalPointers();
	return *this;
}

PipelineConfigInfo &PipelineConfigInfo::setMultisampleCount(VkSampleCountFlagBits sampleCount) noexcept {
	multisampleInfo.rasterizationSamples = sampleCount;
	return *this;
}

} // namespace oxide
