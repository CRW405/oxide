#pragma once

#include <oxide/assets/mesh_data.hpp>

#include <string>
#include <vector>

namespace oxide {

class GltfReader {
  public:
	// Loads triangle primitives from the default scene (or the first scene).
	// Each scene-node instance and primitive becomes a separate indexed mesh.
	[[nodiscard]] static std::vector<MeshData> load(const std::string &path);
};

} // namespace oxide
