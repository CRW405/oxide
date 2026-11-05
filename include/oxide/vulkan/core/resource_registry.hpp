#pragma once

#include <vulkan/vulkan.h>

#include <string>
#include <unordered_map>
#include <utility>

#include <oxide/vulkan/core/resource_pool.hpp>
#include <oxide/vulkan/core/resource_records.hpp>

namespace oxide {

// Owns registered device resources. Application-facing wrappers keep typed
// handles and resolve Vulkan objects through this registry when needed.
class ResourceRegistry {
  public:
	void setDevice(VkDevice device) noexcept { device_ = device; }

	void destroyAll(VkDevice device) noexcept {
		destroyPipelines(device);
		destroyShaders(device);
		destroyPipelineLayouts(device);
		destroyDescriptorPools(device);
		destroyDescriptorLayouts(device);
		destroyImages(device);
		destroyBuffers(device);
		buffers_.clear();
		images_.clear();
		shaders_.clear();
		pipelines_.clear();
		descriptor_layouts_.clear();
		descriptor_pools_.clear();
		pipeline_layouts_.clear();
		shader_keys_.clear();
	}

	[[nodiscard]] BufferHandle registerBuffer(BufferResource resource) {
		return buffers_.create(std::move(resource));
	}
	void unregisterBuffer(BufferHandle handle) noexcept {
		if (device_ != VK_NULL_HANDLE) {
			if (auto *resource = buffers_.get(handle); resource != nullptr &&
			    resource->buffer != VK_NULL_HANDLE) {
				vkDestroyBuffer(device_, resource->buffer, nullptr);
			}
		}
		buffers_.destroy(handle);
	}
	[[nodiscard]] BufferResource *buffer(BufferHandle handle) noexcept { return buffers_.get(handle); }
	[[nodiscard]] const BufferResource *buffer(BufferHandle handle) const noexcept {
		return buffers_.get(handle);
	}

	[[nodiscard]] ImageHandle registerImage(ImageResource resource) {
		return images_.create(std::move(resource));
	}
	void unregisterImage(ImageHandle handle) noexcept {
		if (device_ != VK_NULL_HANDLE) {
			if (auto *resource = images_.get(handle); resource != nullptr) {
				if (resource->sampler != VK_NULL_HANDLE) {
					vkDestroySampler(device_, resource->sampler, nullptr);
				}
				if (resource->view != VK_NULL_HANDLE) {
					vkDestroyImageView(device_, resource->view, nullptr);
				}
				if (resource->image != VK_NULL_HANDLE) {
					vkDestroyImage(device_, resource->image, nullptr);
				}
			}
		}
		images_.destroy(handle);
	}
	[[nodiscard]] ImageResource *image(ImageHandle handle) noexcept { return images_.get(handle); }
	[[nodiscard]] const ImageResource *image(ImageHandle handle) const noexcept {
		return images_.get(handle);
	}

	[[nodiscard]] ShaderHandle registerShader(ShaderResource resource) {
		if (!resource.key.empty()) {
			auto existing = shader_keys_.find(resource.key);
			if (existing != shader_keys_.end()) {
				auto *shader = shaders_.get(existing->second);
				if (shader != nullptr) {
					++shader->references;
					return existing->second;
				}
			}
		}
		const ShaderHandle handle = shaders_.create(std::move(resource));
		if (const auto *shader = shaders_.get(handle); shader != nullptr && !shader->key.empty()) {
			shader_keys_[shader->key] = handle;
		}
		return handle;
	}
	void unregisterShader(ShaderHandle handle) noexcept {
		auto *resource = shaders_.get(handle);
		if (resource == nullptr) return;
		if (resource->references > 1) {
			--resource->references;
			return;
		}
		if (device_ != VK_NULL_HANDLE && resource->module != VK_NULL_HANDLE) {
			vkDestroyShaderModule(device_, resource->module, nullptr);
		}
		if (!resource->key.empty()) shader_keys_.erase(resource->key);
		shaders_.destroy(handle);
	}
	[[nodiscard]] ShaderResource *shader(ShaderHandle handle) noexcept { return shaders_.get(handle); }
	[[nodiscard]] ShaderHandle findShader(const std::string &key) const noexcept {
		const auto found = shader_keys_.find(key);
		return found == shader_keys_.end() ? ShaderHandle{} : found->second;
	}
	[[nodiscard]] ShaderHandle acquireShader(const std::string &key) noexcept {
		const ShaderHandle handle = findShader(key);
		if (!handle.valid()) return {};
		auto *resource = shaders_.get(handle);
		if (resource == nullptr) return {};
		++resource->references;
		return handle;
	}

	[[nodiscard]] PipelineHandle registerPipeline(PipelineResource resource) {
		return pipelines_.create(std::move(resource));
	}
	void unregisterPipeline(PipelineHandle handle) noexcept {
		if (device_ != VK_NULL_HANDLE) {
			if (auto *resource = pipelines_.get(handle); resource != nullptr &&
			    resource->pipeline != VK_NULL_HANDLE) {
				vkDestroyPipeline(device_, resource->pipeline, nullptr);
			}
		}
		pipelines_.destroy(handle);
	}
	[[nodiscard]] PipelineResource *pipeline(PipelineHandle handle) noexcept { return pipelines_.get(handle); }

	[[nodiscard]] DescriptorLayoutHandle registerDescriptorLayout(DescriptorLayoutResource resource) {
		return descriptor_layouts_.create(std::move(resource));
	}
	void unregisterDescriptorLayout(DescriptorLayoutHandle handle) noexcept {
		if (device_ != VK_NULL_HANDLE) {
			if (auto *resource = descriptor_layouts_.get(handle); resource != nullptr &&
			    resource->layout != VK_NULL_HANDLE) {
				vkDestroyDescriptorSetLayout(device_, resource->layout, nullptr);
			}
		}
		descriptor_layouts_.destroy(handle);
	}

	[[nodiscard]] DescriptorPoolHandle registerDescriptorPool(DescriptorPoolResource resource) {
		return descriptor_pools_.create(std::move(resource));
	}
	void unregisterDescriptorPool(DescriptorPoolHandle handle) noexcept {
		if (device_ != VK_NULL_HANDLE) {
			if (auto *resource = descriptor_pools_.get(handle); resource != nullptr &&
			    resource->pool != VK_NULL_HANDLE) {
				vkDestroyDescriptorPool(device_, resource->pool, nullptr);
			}
		}
		descriptor_pools_.destroy(handle);
	}

	[[nodiscard]] PipelineLayoutHandle registerPipelineLayout(PipelineLayoutResource resource) {
		return pipeline_layouts_.create(std::move(resource));
	}
	void unregisterPipelineLayout(PipelineLayoutHandle handle) noexcept {
		if (device_ != VK_NULL_HANDLE) {
			if (auto *resource = pipeline_layouts_.get(handle); resource != nullptr &&
			    resource->layout != VK_NULL_HANDLE) {
				vkDestroyPipelineLayout(device_, resource->layout, nullptr);
			}
		}
		pipeline_layouts_.destroy(handle);
	}
	[[nodiscard]] PipelineLayoutResource *pipelineLayout(PipelineLayoutHandle handle) noexcept {
		return pipeline_layouts_.get(handle);
	}
	[[nodiscard]] const PipelineLayoutResource *pipelineLayout(PipelineLayoutHandle handle) const noexcept {
		return pipeline_layouts_.get(handle);
	}

  private:
	void destroyPipelines(VkDevice device) noexcept {
		pipelines_.forEach([device](PipelineResource &resource) {
			if (resource.pipeline != VK_NULL_HANDLE) {
				vkDestroyPipeline(device, resource.pipeline, nullptr);
			}
			resource.pipeline = VK_NULL_HANDLE;
		});
	}

	void destroyShaders(VkDevice device) noexcept {
		shaders_.forEach([device](ShaderResource &resource) {
			if (resource.module != VK_NULL_HANDLE) {
				vkDestroyShaderModule(device, resource.module, nullptr);
			}
			resource.module = VK_NULL_HANDLE;
		});
	}

	void destroyPipelineLayouts(VkDevice device) noexcept {
		pipeline_layouts_.forEach([device](PipelineLayoutResource &resource) {
			if (resource.layout != VK_NULL_HANDLE) {
				vkDestroyPipelineLayout(device, resource.layout, nullptr);
			}
			resource.layout = VK_NULL_HANDLE;
		});
	}

	void destroyDescriptorPools(VkDevice device) noexcept {
		descriptor_pools_.forEach([device](DescriptorPoolResource &resource) {
			if (resource.pool != VK_NULL_HANDLE) {
				vkDestroyDescriptorPool(device, resource.pool, nullptr);
			}
			resource.pool = VK_NULL_HANDLE;
		});
	}

	void destroyDescriptorLayouts(VkDevice device) noexcept {
		descriptor_layouts_.forEach([device](DescriptorLayoutResource &resource) {
			if (resource.layout != VK_NULL_HANDLE) {
				vkDestroyDescriptorSetLayout(device, resource.layout, nullptr);
			}
			resource.layout = VK_NULL_HANDLE;
		});
	}

	void destroyImages(VkDevice device) noexcept {
		images_.forEach([device](ImageResource &resource) {
			if (resource.sampler != VK_NULL_HANDLE) {
				vkDestroySampler(device, resource.sampler, nullptr);
			}
			if (resource.view != VK_NULL_HANDLE) {
				vkDestroyImageView(device, resource.view, nullptr);
			}
			if (resource.image != VK_NULL_HANDLE) {
				vkDestroyImage(device, resource.image, nullptr);
			}
			resource.sampler = VK_NULL_HANDLE;
			resource.view = VK_NULL_HANDLE;
			resource.image = VK_NULL_HANDLE;
		});
	}

	void destroyBuffers(VkDevice device) noexcept {
		buffers_.forEach([device](BufferResource &resource) {
			if (resource.buffer != VK_NULL_HANDLE) {
				vkDestroyBuffer(device, resource.buffer, nullptr);
			}
			resource.buffer = VK_NULL_HANDLE;
			resource.memory = VK_NULL_HANDLE;
		});
	}

	VkDevice device_ = VK_NULL_HANDLE;
	ResourcePool<BufferResource, BufferTag>     buffers_;
	ResourcePool<ImageResource, ImageTag>       images_;
	ResourcePool<ShaderResource, ShaderTag>     shaders_;
	ResourcePool<PipelineResource, PipelineTag> pipelines_;
	ResourcePool<DescriptorLayoutResource, DescriptorLayoutTag> descriptor_layouts_;
	ResourcePool<DescriptorPoolResource, DescriptorPoolTag> descriptor_pools_;
	ResourcePool<PipelineLayoutResource, PipelineLayoutTag> pipeline_layouts_;
	std::unordered_map<std::string, ShaderHandle> shader_keys_;
};

} // namespace oxide
