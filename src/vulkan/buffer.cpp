#include <oxide/vulkan/buffer.hpp>
#include <oxide/tools/log.hpp>
#include <oxide/vulkan/vk_error.hpp>

#include <cstring>
#include <stdexcept>

namespace oxide {

// -----------------------------------------------------------------------------
// Buffer lifetime and data upload
// -----------------------------------------------------------------------------
Buffer::Buffer(VulkanDevice &device, const void *data, VkDeviceSize size, VkBufferUsageFlags usage)
    : device_(device), size_(size) {
	if (data == nullptr || size_ == 0) {
		throw invalidArgument("buffer requires non-empty data");
	}

	VkBufferCreateInfo bufferInfo{};
	bufferInfo.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufferInfo.size        = size_;
	bufferInfo.usage       = usage;
	bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	checkVk(vkCreateBuffer(device_.device(), &bufferInfo, nullptr, &buffer_), "create buffer");

	VkMemoryRequirements memoryRequirements;
	vkGetBufferMemoryRequirements(device_.device(), buffer_, &memoryRequirements);

	std::uint32_t memory_type = 0;
	try {
		memory_type = device_.findMemoryType(
		    memoryRequirements.memoryTypeBits,
		    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	} catch (...) {
		vkDestroyBuffer(device_.device(), buffer_, nullptr);
		buffer_ = VK_NULL_HANDLE;
		throw;
	}

	VkMemoryAllocateInfo allocation{};
	allocation.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	allocation.allocationSize  = memoryRequirements.size;
	allocation.memoryTypeIndex = memory_type;

	const VkResult allocate_result = vkAllocateMemory(device_.device(), &allocation, nullptr, &memory_);
	if (allocate_result != VK_SUCCESS) {
		vkDestroyBuffer(device_.device(), buffer_, nullptr);
		buffer_ = VK_NULL_HANDLE;
		throw vkError("allocate buffer memory", allocate_result);
	}

	const VkResult bind_result = vkBindBufferMemory(device_.device(), buffer_, memory_, 0);
	if (bind_result != VK_SUCCESS) {
		vkFreeMemory(device_.device(), memory_, nullptr);
		vkDestroyBuffer(device_.device(), buffer_, nullptr);
		memory_ = VK_NULL_HANDLE;
		buffer_ = VK_NULL_HANDLE;
		throw vkError("bind buffer memory", bind_result);
	}

	void          *mappedData = nullptr;
	const VkResult map_result = vkMapMemory(device_.device(), memory_, 0, size_, 0, &mappedData);
	if (map_result != VK_SUCCESS) {
		vkFreeMemory(device_.device(), memory_, nullptr);
		vkDestroyBuffer(device_.device(), buffer_, nullptr);
		memory_ = VK_NULL_HANDLE;
		buffer_ = VK_NULL_HANDLE;
		throw vkError("map buffer memory", map_result);
	}
	std::memcpy(mappedData, data, static_cast<std::size_t>(size_));
	vkUnmapMemory(device_.device(), memory_);
}

Buffer::~Buffer() {
	if (device_.device() != VK_NULL_HANDLE) {
		vkDestroyBuffer(device_.device(), buffer_, nullptr);
		vkFreeMemory(device_.device(), memory_, nullptr);
	}
}

Buffer::Buffer(Buffer &&other) noexcept
    : device_(other.device_), buffer_(other.buffer_), memory_(other.memory_), size_(other.size_) {
	other.buffer_ = VK_NULL_HANDLE;
	other.memory_ = VK_NULL_HANDLE;
	other.size_   = 0;
}

void Buffer::bind(VkCommandBuffer commandBuffer, std::uint32_t binding) const {
	const VkDeviceSize offset = 0;
	vkCmdBindVertexBuffers(commandBuffer, binding, 1, &buffer_, &offset);
}

void Buffer::update(const void *data, VkDeviceSize size, VkDeviceSize offset) {
	if (data == nullptr || size == 0 || offset > size_ || size > size_ - offset) {
		throw invalidArgument("buffer update range is out of bounds");
	}

	void          *mappedData = nullptr;
	const VkResult map_result = vkMapMemory(device_.device(), memory_, offset, size, 0, &mappedData);
	if (map_result != VK_SUCCESS) {
		throw vkError("map buffer memory for update", map_result);
	}
	std::memcpy(mappedData, data, static_cast<std::size_t>(size));
	vkUnmapMemory(device_.device(), memory_);
}

void Buffer::read(void *data, VkDeviceSize size, VkDeviceSize offset) const {
	if (data == nullptr || size == 0 || offset > size_ || size > size_ - offset) {
		throw invalidArgument("buffer read range is out of bounds");
	}

	void          *mappedData = nullptr;
	const VkResult map_result = vkMapMemory(device_.device(), memory_, offset, size, 0, &mappedData);
	if (map_result != VK_SUCCESS) {
		throw vkError("map buffer memory for read", map_result);
	}
	std::memcpy(data, mappedData, static_cast<std::size_t>(size));
	vkUnmapMemory(device_.device(), memory_);
}

// -----------------------------------------------------------------------------
// Index buffer binding
// -----------------------------------------------------------------------------
void IndexBuffer::bind(VkCommandBuffer commandBuffer) const {
	vkCmdBindIndexBuffer(commandBuffer, buffer_, 0, index_type_);
}

// -----------------------------------------------------------------------------
// Mesh binding and drawing
// -----------------------------------------------------------------------------
void Mesh::bind(VkCommandBuffer commandBuffer) const {
	vertex_buffer_.bind(commandBuffer);
	index_buffer_.bind(commandBuffer);
}

void Mesh::draw(VkCommandBuffer commandBuffer) const {
	vkCmdDrawIndexed(commandBuffer, index_count_, 1, 0, 0, 0);
}

} // namespace oxide
