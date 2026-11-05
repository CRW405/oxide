#include <oxide/vulkan/core/resource_registry.hpp>

#include <cassert>

int main() {
	oxide::ResourceRegistry registry;

	const auto first = registry.registerBuffer({});
	assert(registry.buffer(first) != nullptr);

	registry.unregisterBuffer(first);
	assert(registry.buffer(first) == nullptr);

	const auto replacement = registry.registerBuffer({});
	assert(registry.buffer(replacement) != nullptr);
	assert(first != replacement);

	registry.unregisterBuffer(replacement);
	return 0;
}
