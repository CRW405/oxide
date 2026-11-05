#include <oxide/vulkan/resources/buffer.hpp>
#include <oxide/tools/log.hpp>
#include <oxide/vulkan/core/vk_error.hpp>

#include <cstring>

namespace oxide {

Buffer::Buffer(VulkanDevice &device, const void *data, VkDeviceSize size, VkBufferUsageFlags usage)
    : device_(device), size_(size) {
	if (data == nullptr || size == 0) throw invalidArgument("buffer requires non-empty data");
	const bool device_local = (usage & (VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT)) != 0;
	VkBufferCreateInfo info{};
	info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	info.size = size_;
	info.usage = device_local ? usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT : usage;
	info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	VkBuffer buffer = VK_NULL_HANDLE;
	checkVk(vkCreateBuffer(device_.device(), &info, nullptr, &buffer), "create buffer");
	VkMemoryRequirements requirements{};
	vkGetBufferMemoryRequirements(device_.device(), buffer, &requirements);
	DeviceMemoryAllocation allocation{};
	try {
		allocation = device_.allocateMemory(requirements,
		    device_local ? VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT :
		                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
		checkVk(vkBindBufferMemory(device_.device(), buffer, allocation.memory, allocation.offset), "bind buffer memory");
		handle_ = device_.resources().registerBuffer({buffer, allocation.memory, allocation.offset, size_});
		if (!device_local) {
			void *mapped = device_.mapMemory(allocation);
			if (mapped == nullptr) throw runtimeError("map buffer memory");
			std::memcpy(mapped, data, static_cast<std::size_t>(size_));
		} else {
			Buffer staging(device_, data, size_, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
			VkCommandBufferAllocateInfo ai{};
			ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
			ai.commandPool = device_.commandPool();
			ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
			ai.commandBufferCount = 1;
			VkCommandBuffer command = VK_NULL_HANDLE;
			checkVk(vkAllocateCommandBuffers(device_.device(), &ai, &command), "allocate buffer upload command");
			VkCommandBufferBeginInfo bi{};
			bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
			bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
			try {
				checkVk(vkBeginCommandBuffer(command, &bi), "begin buffer upload command");
				VkBufferCopy copy{ 0, 0, size_ };
				vkCmdCopyBuffer(command, staging.buffer(), buffer, 1, &copy);
				checkVk(vkEndCommandBuffer(command), "end buffer upload command");
				VkSubmitInfo submit{};
				submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
				submit.commandBufferCount = 1;
				submit.pCommandBuffers = &command;
				checkVk(vkQueueSubmit(device_.graphicsQueue(), 1, &submit, VK_NULL_HANDLE), "submit buffer upload");
				checkVk(vkQueueWaitIdle(device_.graphicsQueue()), "wait for buffer upload");
			} catch (...) {
				vkFreeCommandBuffers(device_.device(), device_.commandPool(), 1, &command);
				throw;
			}
			vkFreeCommandBuffers(device_.device(), device_.commandPool(), 1, &command);
		}
	} catch (...) {
		if (handle_.valid()) {
			device_.resources().unregisterBuffer(handle_);
		} else if (buffer != VK_NULL_HANDLE) {
			vkDestroyBuffer(device_.device(), buffer, nullptr);
		}
		handle_ = {};
		throw;
	}
}

Buffer::~Buffer() {
	if (device_.device() != VK_NULL_HANDLE) {
		const auto *resource = device_.resources().buffer(handle_);
		const DeviceMemoryAllocation allocation =
		    resource == nullptr ? DeviceMemoryAllocation{} :
		                          DeviceMemoryAllocation{resource->memory, resource->offset, resource->size, 0, nullptr};
		device_.resources().unregisterBuffer(handle_);
		if (allocation.memory != VK_NULL_HANDLE) {
			device_.freeMemory(allocation);
		}
	}
}

Buffer::Buffer(Buffer &&other) noexcept
    : device_(other.device_), size_(other.size_), handle_(other.handle_) {
	other.size_ = 0;
	other.handle_ = {};
}

void Buffer::bind(VkCommandBuffer commandBuffer, std::uint32_t binding) const {
	const VkDeviceSize offset = 0;
	const VkBuffer buffer = this->buffer();
	vkCmdBindVertexBuffers(commandBuffer, binding, 1, &buffer, &offset);
}

void Buffer::update(const void *data, VkDeviceSize size, VkDeviceSize offset) {
	if (data == nullptr || size == 0 || offset > size_ || size > size_ - offset)
		throw invalidArgument("buffer update range is out of bounds");
	const auto *resource = device_.resources().buffer(handle_);
	if (resource == nullptr) throw runtimeError("buffer resource is no longer valid");
	void *mapped = device_.mapMemory({ resource->memory, resource->offset, resource->size, 0, nullptr });
	if (mapped == nullptr) throw runtimeError("map buffer memory for update");
	std::memcpy(static_cast<char *>(mapped) + offset, data, static_cast<std::size_t>(size));
}

void Buffer::read(void *data, VkDeviceSize size, VkDeviceSize offset) const {
	if (data == nullptr || size == 0 || offset > size_ || size > size_ - offset)
		throw invalidArgument("buffer read range is out of bounds");
	const auto *resource = device_.resources().buffer(handle_);
	if (resource == nullptr) throw runtimeError("buffer resource is no longer valid");
	void *mapped = device_.mapMemory({ resource->memory, resource->offset, resource->size, 0, nullptr });
	if (mapped == nullptr) throw runtimeError("map buffer memory for read");
	std::memcpy(data, static_cast<char *>(mapped) + offset, static_cast<std::size_t>(size));
}

void IndexBuffer::bind(VkCommandBuffer commandBuffer) const {
	vkCmdBindIndexBuffer(commandBuffer, buffer(), 0, index_type_);
}

void Mesh::bind(VkCommandBuffer commandBuffer) const {
	vertex_buffer_.bind(commandBuffer);
	index_buffer_.bind(commandBuffer);
}

void Mesh::draw(VkCommandBuffer commandBuffer) const {
	vkCmdDrawIndexed(commandBuffer, index_count_, 1, 0, 0, 0);
}

} // namespace oxide
