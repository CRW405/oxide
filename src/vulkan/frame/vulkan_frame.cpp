#include <oxide/vulkan/frame/vulkan_context.hpp>
#include <oxide/tools/log.hpp>
#include <oxide/vulkan/core/vk_error.hpp>

#include <algorithm>

namespace oxide {

FrameScope::FrameScope(VulkanContext &context, Frame frame, std::array<float, 4> clearColor)
    : context_(&context), frame_(frame) {
	if (*this) {
		context_->beginRenderPass(frame_, clearColor);
	}
}

FrameScope::~FrameScope() {
	if (*this) {
		context_->endRenderPass(frame_);
		context_->endFrame(frame_);
	}
}

FrameScope::FrameScope(FrameScope &&other) noexcept : context_(other.context_), frame_(other.frame_) {
	other.context_ = nullptr;
	other.frame_   = {};
}

FrameScope &FrameScope::operator=(FrameScope &&other) noexcept {
	if (this != &other) {
		if (*this) {
			context_->endRenderPass(frame_);
			context_->endFrame(frame_);
		}
		context_       = other.context_;
		frame_         = other.frame_;
		other.context_ = nullptr;
		other.frame_   = {};
	}
	return *this;
}

void VulkanContext::createCommandResources() {
	const QueueFamilies     queues = device_.findQueueFamilies(device_.physicalDevice(), surface_);
	VkCommandPoolCreateInfo pool{};
	pool.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	pool.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	pool.queueFamilyIndex = queues.graphics;
	checkVk(vkCreateCommandPool(device_.device(), &pool, nullptr, &command_pool_),
	        "create swapchain command pool");

	VkCommandBufferAllocateInfo allocation{};
	allocation.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	allocation.commandPool        = command_pool_;
	allocation.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	allocation.commandBufferCount = MAX_FRAMES_IN_FLIGHT;
	checkVk(vkAllocateCommandBuffers(device_.device(), &allocation, command_buffers_.data()),
	        "allocate frame command buffers");

	VkSemaphoreCreateInfo semaphore{};
	semaphore.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
	VkFenceCreateInfo fence{};
	fence.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	fence.flags = VK_FENCE_CREATE_SIGNALED_BIT;

	images_in_flight_.assign(swapchain_images_.size(), VK_NULL_HANDLE);
	render_finished_.resize(swapchain_images_.size());
	for (std::uint32_t index = 0; index < MAX_FRAMES_IN_FLIGHT; ++index) {
		checkVk(vkCreateSemaphore(device_.device(), &semaphore, nullptr, &image_available_[index]),
		        "create image-available semaphore");
		checkVk(vkCreateFence(device_.device(), &fence, nullptr, &in_flight_[index]),
		        "create frame fence");
	}
	for (VkSemaphore &signal : render_finished_) {
		checkVk(vkCreateSemaphore(device_.device(), &semaphore, nullptr, &signal),
		        "create render-finished semaphore");
	}
}

Frame VulkanContext::beginFrame() {
	if (framebuffer_resized_) {
		framebuffer_resized_ = false;
		recreateSwapchain();
		return {};
	}

	const VkFence fence = in_flight_[current_frame_];
	checkVk(vkWaitForFences(device_.device(), 1, &fence, VK_TRUE, UINT64_MAX), "wait for frame fence");
	Frame          frame{ command_buffers_[current_frame_], 0, current_frame_ };
	const VkResult result = vkAcquireNextImageKHR(
	    device_.device(), swapchain_, UINT64_MAX, image_available_[current_frame_], VK_NULL_HANDLE, &frame.imageIndex);
	if (result == VK_ERROR_OUT_OF_DATE_KHR) {
		recreateSwapchain();
		return {};
	}
	if (result == VK_ERROR_SURFACE_LOST_KHR) {
		recoverSurface();
		return {};
	}
	if (result == VK_ERROR_DEVICE_LOST) {
		throw vkError("acquire swapchain image: device lost", result);
	}
	if (result == VK_SUBOPTIMAL_KHR) {
		OXIDE_LOG_WARNING << "swapchain image acquired with VK_SUBOPTIMAL_KHR; recreating swapchain";
	} else if (result != VK_SUCCESS) {
		throw vkError("acquire swapchain image", result);
	}

	if (images_in_flight_[frame.imageIndex] != VK_NULL_HANDLE) {
		checkVk(vkWaitForFences(device_.device(), 1, &images_in_flight_[frame.imageIndex], VK_TRUE, UINT64_MAX),
		        "wait for swapchain image fence");
	}
	images_in_flight_[frame.imageIndex] = fence;
	checkVk(vkResetFences(device_.device(), 1, &fence), "reset frame fence");
	checkVk(vkResetCommandBuffer(frame.commandBuffer, 0), "reset frame command buffer");

	VkCommandBufferBeginInfo begin{};
	begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	checkVk(vkBeginCommandBuffer(frame.commandBuffer, &begin), "begin frame command buffer");
	return frame;
}

FrameScope VulkanContext::beginFrameScope(std::array<float, 4> clearColor) {
	return FrameScope(*this, beginFrame(), clearColor);
}

void VulkanContext::endFrame(const Frame &frame) {
	if (frame.commandBuffer == VK_NULL_HANDLE) {
		return;
	}
	checkVk(vkEndCommandBuffer(frame.commandBuffer), "end frame command buffer");

	VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	VkSubmitInfo         submit{};
	submit.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submit.waitSemaphoreCount   = 1;
	submit.pWaitSemaphores      = &image_available_[frame.frameIndex];
	submit.pWaitDstStageMask    = &wait_stage;
	submit.commandBufferCount   = 1;
	submit.pCommandBuffers      = &frame.commandBuffer;
	submit.signalSemaphoreCount = 1;
	submit.pSignalSemaphores    = &render_finished_[frame.imageIndex];
	checkVk(vkQueueSubmit(device_.graphicsQueue(), 1, &submit, in_flight_[frame.frameIndex]),
	        "submit frame command buffer");

	VkPresentInfoKHR present{};
	present.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	present.waitSemaphoreCount = 1;
	present.pWaitSemaphores    = &render_finished_[frame.imageIndex];
	present.swapchainCount     = 1;
	present.pSwapchains        = &swapchain_;
	present.pImageIndices      = &frame.imageIndex;
	const VkResult result      = vkQueuePresentKHR(device_.presentQueue(), &present);
	if (result == VK_ERROR_OUT_OF_DATE_KHR) {
		recreateSwapchain();
	} else if (result == VK_ERROR_SURFACE_LOST_KHR) {
		recoverSurface();
	} else if (result == VK_ERROR_DEVICE_LOST) {
		throw vkError("submit/present frame: device lost", result);
	} else if (result == VK_SUBOPTIMAL_KHR) {
		OXIDE_LOG_WARNING << "swapchain presented with VK_SUBOPTIMAL_KHR; recreating swapchain";
		recreateSwapchain();
	} else if (result != VK_SUCCESS) {
		throw vkError("present swapchain image", result);
	}

	current_frame_ = (current_frame_ + 1) % MAX_FRAMES_IN_FLIGHT;
}

void VulkanContext::beginRenderPass(const Frame &frame, std::array<float, 4> clearColor) {
	if (frame.commandBuffer == VK_NULL_HANDLE) {
		return;
	}

	VkClearValue clearValues[2]{};
	std::copy(clearColor.begin(), clearColor.end(), clearValues[0].color.float32);
	clearValues[1].depthStencil = { 1.0f, 0 };

	VkRenderPassBeginInfo renderPassInfo{};
	renderPassInfo.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	renderPassInfo.renderPass        = render_pass_;
	renderPassInfo.framebuffer       = framebuffer(frame.imageIndex);
	renderPassInfo.renderArea.offset = { 0, 0 };
	renderPassInfo.renderArea.extent = swapchain_extent_;
	renderPassInfo.clearValueCount   = 2;
	renderPassInfo.pClearValues      = clearValues;
	vkCmdBeginRenderPass(frame.commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
}

void VulkanContext::endRenderPass(const Frame &frame) {
	if (frame.commandBuffer != VK_NULL_HANDLE) {
		vkCmdEndRenderPass(frame.commandBuffer);
	}
}

void VulkanContext::setViewportAndScissor(VkCommandBuffer commandBuffer) const {
	if (commandBuffer == VK_NULL_HANDLE) {
		return;
	}

	VkViewport viewport{};
	viewport.width    = static_cast<float>(swapchain_extent_.width);
	viewport.height   = static_cast<float>(swapchain_extent_.height);
	viewport.maxDepth = 1.0f;
	vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

	VkRect2D scissor{};
	scissor.extent = swapchain_extent_;
	vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
}

} // namespace oxide
