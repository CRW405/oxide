#pragma once

#include <vulkan/vulkan.h>

#include <string>
#include <cstdint>

#include <oxide/vulkan/device/vulkan_device.hpp>

namespace oxide {

struct TextureConfig {
	VkFormat format = VK_FORMAT_R8G8B8A8_SRGB;
	VkFilter minFilter = VK_FILTER_LINEAR;
	VkFilter magFilter = VK_FILTER_LINEAR;
	VkSamplerAddressMode addressMode = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	bool anisotropy = true;
};

class ImageTexture {
  public:
	ImageTexture(VulkanDevice &device, const std::string &imagePath, TextureConfig config = {});
	~ImageTexture();

	// disable copying
	ImageTexture(const ImageTexture &)            = delete;
	ImageTexture &operator=(const ImageTexture &) = delete;

	// wrappers
	[[nodiscard]] VkImageView imageView() const {
		const auto *resource = device_.resources().image(handle_);
		return resource == nullptr ? VK_NULL_HANDLE : resource->view;
	}
	[[nodiscard]] VkSampler sampler() const {
		const auto *resource = device_.resources().image(handle_);
		return resource == nullptr ? VK_NULL_HANDLE : resource->sampler;
	}
	[[nodiscard]] ImageHandle handle() const noexcept { return handle_; }

  private:
	void destroyResources() noexcept;

	VulkanDevice  &device_;
	std::uint32_t  mip_levels_ = 1;
	ImageHandle    handle_{};
};

} // namespace oxide
