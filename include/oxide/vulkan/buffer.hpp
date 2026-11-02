#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <type_traits>
#include <vector>

#include <oxide/vulkan/vulkan_device.hpp>

namespace oxide {

// ============================================================================
// GPU buffer ownership
// ============================================================================
class Buffer {
  public:
	Buffer(VulkanDevice &device, const void *data, VkDeviceSize size, VkBufferUsageFlags usage);
	~Buffer();

	Buffer(const Buffer &)            = delete;
	Buffer &operator=(const Buffer &) = delete;
	Buffer(Buffer &&other) noexcept;
	Buffer &operator=(Buffer &&) = delete;

	[[nodiscard]] VkBuffer     buffer() const noexcept { return buffer_; }
	[[nodiscard]] VkDeviceSize size() const noexcept { return size_; }
	void                       bind(VkCommandBuffer commandBuffer, std::uint32_t binding = 0) const;
	void                       update(const void *data, VkDeviceSize size, VkDeviceSize offset = 0);

	// Copy data back out of a host-visible buffer after the GPU work that wrote
	// it has completed (for example after VulkanDevice::waitIdle()).
	void read(void *data, VkDeviceSize size, VkDeviceSize offset = 0) const;

	// Upload one typed value without repeating its size at the call site.
	template <typename T>
	void update(const T &data, VkDeviceSize offset = 0) {
		static_assert(!std::is_pointer_v<T>, "pass a value, not a pointer");
		update(&data, static_cast<VkDeviceSize>(sizeof(T)), offset);
	}

  protected:
	VulkanDevice  &device_;
	VkBuffer       buffer_ = VK_NULL_HANDLE;
	VkDeviceMemory memory_ = VK_NULL_HANDLE;
	VkDeviceSize   size_   = 0;
};

// ============================================================================
// Uniform buffer default
// ============================================================================
class UniformBuffer : public Buffer {
  public:
	UniformBuffer(VulkanDevice &device, const void *data, VkDeviceSize size)
	    : Buffer(device, data, size, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT) {}

	template <typename T>
	explicit UniformBuffer(VulkanDevice &device, const T &data)
	    : Buffer(
	          device,
	          &data,
	          static_cast<VkDeviceSize>(sizeof(T)),
	          VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT) {
		static_assert(std::is_trivially_copyable_v<T>, "uniform buffer data must be trivially copyable");
	}
};

// ============================================================================
// Vertex and index draw buffers
// ============================================================================
class VertexBuffer : public Buffer {
  public:
	VertexBuffer(VulkanDevice &device, const void *data, VkDeviceSize size)
	    : Buffer(device, data, size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT) {}

	template <typename Vertex>
	VertexBuffer(VulkanDevice &device, const std::vector<Vertex> &vertices)
	    : VertexBuffer(
	          device,
	          vertices.data(),
	          static_cast<VkDeviceSize>(vertices.size() * sizeof(Vertex))) {}
};

class IndexBuffer : public Buffer {
  public:
	IndexBuffer(VulkanDevice &device, const void *data, VkDeviceSize size, VkIndexType indexType)
	    : Buffer(device, data, size, VK_BUFFER_USAGE_INDEX_BUFFER_BIT), index_type_(indexType) {}

	IndexBuffer(VulkanDevice &device, const std::vector<std::uint16_t> &indices)
	    : IndexBuffer(
	          device,
	          indices.data(),
	          static_cast<VkDeviceSize>(indices.size() * sizeof(std::uint16_t)),
	          VK_INDEX_TYPE_UINT16) {}

	IndexBuffer(VulkanDevice &device, const std::vector<std::uint32_t> &indices)
	    : IndexBuffer(
	          device,
	          indices.data(),
	          static_cast<VkDeviceSize>(indices.size() * sizeof(std::uint32_t)),
	          VK_INDEX_TYPE_UINT32) {}

	void                      bind(VkCommandBuffer commandBuffer) const;
	[[nodiscard]] VkIndexType indexType() const noexcept { return index_type_; }

  private:
	VkIndexType index_type_;
};

// ============================================================================
// Mesh
//
// Pairs a vertex and index buffer for the common indexed draw. Bind the
// pipeline first, then call bind() and draw() to record the mesh.
// ============================================================================
class Mesh {
  public:
	template <typename Vertex, typename Index>
	Mesh(
	    VulkanDevice             &device,
	    const std::vector<Vertex> &vertices,
	    const std::vector<Index>  &indices)
	    : vertex_buffer_(device, vertices),
	      index_buffer_(device, indices),
	      vertex_count_(static_cast<std::uint32_t>(vertices.size())),
	      index_count_(static_cast<std::uint32_t>(indices.size())) {
		static_assert(std::is_same_v<Index, std::uint16_t> || std::is_same_v<Index, std::uint32_t>,
		              "indices must be std::uint16_t or std::uint32_t");
	}

	void bind(VkCommandBuffer commandBuffer) const;
	void draw(VkCommandBuffer commandBuffer) const;

	[[nodiscard]] const VertexBuffer &vertexBuffer() const noexcept { return vertex_buffer_; }
	[[nodiscard]] const IndexBuffer  &indexBuffer() const noexcept { return index_buffer_; }
	[[nodiscard]] std::uint32_t       vertexCount() const noexcept { return vertex_count_; }
	[[nodiscard]] std::uint32_t       indexCount() const noexcept { return index_count_; }

  private:
	VertexBuffer  vertex_buffer_;
	IndexBuffer   index_buffer_;
	std::uint32_t vertex_count_ = 0;
	std::uint32_t index_count_  = 0;
};

} // namespace oxide
