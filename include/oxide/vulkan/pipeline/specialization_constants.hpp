#pragma once

#include <vulkan/vulkan.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <vector>

#include <oxide/tools/log.hpp>
#include <oxide/vulkan/pipeline/shader_reflection.hpp>

namespace oxide {

// ============================================================================
// Specialization constant overrides for one shader stage
//
// Values are copied into an owned byte buffer, so the object can outlive the
// variable that supplied them. Use shader reflection to discover constant IDs
// and pass the reflected entry to set() to keep names and IDs in sync.
//
// PipelineConfigInfo holds one of these for the vertex and fragment stages.
// A stage that declares no overrides receives no VkSpecializationInfo.
// ============================================================================
class SpecializationConstants {
  public:
	template <typename T>
	SpecializationConstants &set(std::uint32_t constantId, const T &value) {
		static_assert(std::is_trivially_copyable_v<T>,
		              "specialization constants must be trivially copyable");
		return setBytes(constantId, &value, sizeof(T));
	}

	SpecializationConstants &set(
	    std::uint32_t constantId,
	    const void   *data,
	    std::size_t   size);

	template <typename T>
	SpecializationConstants &set(
	    const ReflectedSpecializationConstant &constant,
	    const T                               &value) {
		return set(constant.constantId, value);
	}

	[[nodiscard]] bool empty() const noexcept { return entries_.empty(); }
	[[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }

	// Build the Vulkan description. The returned value points into this object
	// and stays valid as long as no further set() call is made.
	[[nodiscard]] VkSpecializationInfo info() const noexcept;
	[[nodiscard]] const std::vector<VkSpecializationMapEntry> &entries() const noexcept {
		return entries_;
	}
	[[nodiscard]] const std::vector<std::byte> &data() const noexcept { return data_; }

  private:
	SpecializationConstants &setBytes(
	    std::uint32_t constantId,
	    const void   *data,
	    std::size_t   size);

	std::vector<VkSpecializationMapEntry> entries_;
	std::vector<std::byte>                data_;
};

} // namespace oxide
