#pragma once

#include <cstdint>
#include <limits>

namespace oxide {

template <typename Tag>
class Handle {
  public:
	using Value = std::uint32_t;
	static constexpr Value invalid_value = std::numeric_limits<Value>::max();

	constexpr Handle() noexcept = default;
	explicit constexpr Handle(Value value) noexcept : value_(value) {}

	[[nodiscard]] constexpr bool valid() const noexcept { return value_ != invalid_value; }
	[[nodiscard]] constexpr Value value() const noexcept { return value_; }
	friend constexpr bool operator==(Handle, Handle) noexcept = default;

  private:
	Value value_ = invalid_value;
};

struct BufferTag;
struct ImageTag;
struct ShaderTag;
struct PipelineTag;
struct DescriptorLayoutTag;
struct DescriptorPoolTag;
struct PipelineLayoutTag;
struct RenderPassTag;
struct FramebufferTag;

using BufferHandle = Handle<BufferTag>;
using ImageHandle = Handle<ImageTag>;
using ShaderHandle = Handle<ShaderTag>;
using PipelineHandle = Handle<PipelineTag>;
using DescriptorLayoutHandle = Handle<DescriptorLayoutTag>;
using DescriptorPoolHandle = Handle<DescriptorPoolTag>;
using PipelineLayoutHandle = Handle<PipelineLayoutTag>;
using RenderPassHandle = Handle<RenderPassTag>;
using FramebufferHandle = Handle<FramebufferTag>;

} // namespace oxide
