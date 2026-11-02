#include <oxide/glfw/glfw_oxide.hpp>
#include <oxide/tools/log.hpp>
#include <oxide/vulkan/buffer.hpp>
#include <oxide/vulkan/descriptor_set.hpp>
#include <oxide/vulkan/pipeline_layout.hpp>
#include <oxide/vulkan/render_pipeline.hpp>
#include <oxide/vulkan/vk_error.hpp>
#include <oxide/vulkan/vulkan_device.hpp>

#include <cstdint>
#include <cstdio>
#include <exception>
#include <vector>

// -----------------------------------------------------------------------------
// Compute example
//
// Dispatch one compute workgroup per 64 elements to increment a storage buffer,
// wait for the GPU, then map the host-visible buffer and read the results back.
// -----------------------------------------------------------------------------
int main() {
	std::printf("Running Compute Example\n");
	try {
		oxide::GLFWContext  glfw;
		oxide::Window       window({ 320, 240, "Compute Example", false });
		oxide::VulkanDevice device(window.handle(), "Compute Example");

		constexpr std::uint32_t element_count = 64;
		std::vector<std::uint32_t> values(element_count, 7);

		// Buffer always allocates host-visible memory, so the same allocation can
		// be used as a storage buffer and read back after the GPU finishes.
		oxide::Buffer storage_buffer(device,
		                             values.data(),
		                             values.size() * sizeof(std::uint32_t),
		                             VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);

		oxide::DescriptorSetLayout descriptor_layout(device, oxide::storageBuffer(0));
		oxide::DescriptorPool      descriptor_pool(device, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1);
		oxide::DescriptorSet       descriptor_set(descriptor_pool, descriptor_layout.handle());

		VkDescriptorBufferInfo buffer_info{};
		buffer_info.buffer = storage_buffer.buffer();
		buffer_info.offset = 0;
		buffer_info.range  = storage_buffer.size();

		VkWriteDescriptorSet write{};
		write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		write.dstSet          = descriptor_set.handle();
		write.dstBinding      = 0;
		write.dstArrayElement = 0;
		write.descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		write.descriptorCount = 1;
		write.pBufferInfo     = &buffer_info;
		vkUpdateDescriptorSets(device.device(), 1, &write, 0, nullptr);

		oxide::PipelineLayout  pipeline_layout(device, { descriptor_layout.handle() }, {});
		oxide::ComputePipeline pipeline(device,
		                                OXIDE_SHADER_DIR "/increment.comp.spv",
		                                pipeline_layout.handle());

		// Declared after the GPU resources so it is destroyed first, including on
		// exception unwinding.
		oxide::DeviceIdleGuard idle_guard(device);

		VkCommandBufferAllocateInfo allocation{};
		allocation.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		allocation.commandPool        = device.commandPool();
		allocation.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		allocation.commandBufferCount = 1;

		VkCommandBuffer command_buffer = VK_NULL_HANDLE;
		oxide::checkVk(vkAllocateCommandBuffers(device.device(), &allocation, &command_buffer),
		        "allocate compute command buffer");

		VkCommandBufferBeginInfo begin{};
		begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

		try {
			oxide::checkVk(vkBeginCommandBuffer(command_buffer, &begin), "begin compute command buffer");

			pipeline.bind(command_buffer);
			descriptor_set.bind(command_buffer, pipeline_layout.handle(), 0, VK_PIPELINE_BIND_POINT_COMPUTE);
			pipeline.dispatch(command_buffer, (element_count + 63) / 64);

			oxide::checkVk(vkEndCommandBuffer(command_buffer), "end compute command buffer");

			VkSubmitInfo submit{};
			submit.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
			submit.commandBufferCount = 1;
			submit.pCommandBuffers    = &command_buffer;
			oxide::checkVk(vkQueueSubmit(device.graphicsQueue(), 1, &submit, VK_NULL_HANDLE),
			        "submit compute command buffer");
			oxide::checkVk(vkQueueWaitIdle(device.graphicsQueue()), "wait for compute command buffer");
		} catch (...) {
			vkFreeCommandBuffers(device.device(), device.commandPool(), 1, &command_buffer);
			throw;
		}
		vkFreeCommandBuffers(device.device(), device.commandPool(), 1, &command_buffer);

		storage_buffer.read(values.data(), values.size() * sizeof(std::uint32_t));

		bool correct = true;
		for (std::uint32_t value : values) {
			if (value != 8) {
				correct = false;
				break;
			}
		}
		std::printf("values[0] = %u, values[%u] = %u\n",
		            values.front(),
		            element_count - 1,
		            values.back());
		if (!correct) {
			OXIDE_LOG_ERROR << "compute dispatch produced unexpected values";
			return 1;
		}
	} catch (const std::exception &error) {
		OXIDE_LOG_FATAL << "unhandled exception: " << error.what();
		return 1;
	}
	return 0;
}
