#pragma once

#include <oxide/vulkan/core/handle.hpp>

#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace oxide {

// Stores resources in reusable slots while keeping stale handles invalid.
template <typename T, typename Tag>
class ResourcePool {
  public:
	using ResourceHandle = Handle<Tag>;

	ResourceHandle create(T value) {
		for (std::uint32_t index = 0; index < slots_.size(); ++index) {
			auto &slot = slots_[index];
			if (!slot.value.has_value()) {
				slot.value = std::move(value);
				return ResourceHandle(makeValue(index, slot.generation));
			}
		}
		slots_.push_back(Slot{std::move(value), 1});
		return ResourceHandle(makeValue(static_cast<std::uint32_t>(slots_.size() - 1), 1));
	}

	void destroy(ResourceHandle handle) noexcept {
		Slot *slot = findSlot(handle);
		if (slot == nullptr) return;
		slot->value.reset();
		++slot->generation;
		if (slot->generation == 0) slot->generation = 1;
	}

	[[nodiscard]] T *get(ResourceHandle handle) noexcept {
		Slot *slot = findSlot(handle);
		return slot == nullptr || !slot->value.has_value() ? nullptr : &*slot->value;
	}

	[[nodiscard]] const T *get(ResourceHandle handle) const noexcept {
		const Slot *slot = findSlot(handle);
		return slot == nullptr || !slot->value.has_value() ? nullptr : &*slot->value;
	}

	template <typename Function>
	void forEach(Function &&function) noexcept {
		for (auto &slot : slots_) {
			if (slot.value.has_value()) function(*slot.value);
		}
	}

	void clear() noexcept {
		for (auto &slot : slots_) {
			slot.value.reset();
			++slot.generation;
			if (slot.generation == 0) slot.generation = 1;
		}
	}

  private:
	struct Slot {
		std::optional<T> value;
		std::uint16_t     generation = 1;
	};

	static constexpr std::uint32_t index_bits = 16;
	static constexpr std::uint32_t index_mask = (1u << index_bits) - 1;

	static constexpr std::uint32_t makeValue(
	    std::uint32_t index,
	    std::uint16_t generation) noexcept {
		return (static_cast<std::uint32_t>(generation) << index_bits) | index;
	}

	static constexpr std::uint32_t indexOf(ResourceHandle handle) noexcept {
		return handle.value() & index_mask;
	}

	static constexpr std::uint16_t generationOf(ResourceHandle handle) noexcept {
		return static_cast<std::uint16_t>(handle.value() >> index_bits);
	}

	Slot *findSlot(ResourceHandle handle) noexcept {
		if (!handle.valid()) return nullptr;
		const std::uint32_t index = indexOf(handle);
		return index < slots_.size() && slots_[index].generation == generationOf(handle) ? &slots_[index] : nullptr;
	}

	const Slot *findSlot(ResourceHandle handle) const noexcept {
		if (!handle.valid()) return nullptr;
		const std::uint32_t index = indexOf(handle);
		return index < slots_.size() && slots_[index].generation == generationOf(handle) ? &slots_[index] : nullptr;
	}

	std::vector<Slot> slots_;
};

} // namespace oxide
