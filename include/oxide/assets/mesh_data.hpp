#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace oxide {

struct MeshVertex {
	std::array<float, 3> position{};
	std::array<float, 3> normal{};
	std::array<float, 2> texCoord{};
};

struct MeshData {
	std::string               name;
	std::vector<MeshVertex>   vertices;
	std::vector<std::uint32_t> indices;
};

} // namespace oxide
