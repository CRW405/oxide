#include <oxide/vulkan/pipeline/specialization_constants.hpp>

#include <algorithm>
#include <bit>
#include <string>

namespace oxide {

SpecializationConstants &SpecializationConstants::set(
    std::uint32_t constantId,
    const void   *data,
    std::size_t   size) {
	if (data == nullptr || size == 0) {
		throw invalidArgument("specialization constant requires a value");
	}
	return setBytes(constantId, data, size);
}

SpecializationConstants &SpecializationConstants::setBytes(
    std::uint32_t constantId,
    const void   *data,
    std::size_t   size) {
	const auto duplicate = std::find_if(
	    entries_.begin(), entries_.end(),
	    [constantId](const VkSpecializationMapEntry &entry) {
		    return entry.constantID == constantId;
	    });
	if (duplicate != entries_.end()) {
		throw invalidArgument("specialization constant " + std::to_string(constantId) +
		                      " was already set");
	}

	// Keep every value aligned to a power-of-two boundary at least as large as
	// its own size. This covers the scalar GLSL types and remains safe for any
	// trivially copyable value passed through the typed overload.
	const std::size_t alignment = std::bit_ceil(std::max<std::size_t>(size, 4));
	const std::size_t padding   = (alignment - (data_.size() % alignment)) % alignment;
	data_.insert(data_.end(), padding, std::byte{ 0 });

	const auto offset = static_cast<std::uint32_t>(data_.size());
	const auto *bytes = static_cast<const std::byte *>(data);
	data_.insert(data_.end(), bytes, bytes + size);

	entries_.push_back(VkSpecializationMapEntry{
	    constantId,
	    offset,
	    static_cast<std::uint32_t>(size),
	});
	return *this;
}

VkSpecializationInfo SpecializationConstants::info() const noexcept {
	if (entries_.empty()) {
		return {};
	}

	VkSpecializationInfo info{};
	info.mapEntryCount = static_cast<std::uint32_t>(entries_.size());
	info.pMapEntries   = entries_.data();
	info.dataSize      = data_.size();
	info.pData         = data_.data();
	return info;
}

} // namespace oxide
