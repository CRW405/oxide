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
	colorBlendInfo.pAttachments = &colorBlendAttachment;
	dynamicStateInfo.pDynamicStates = dynamicStates.data();
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

} // namespace oxide
