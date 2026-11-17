#pragma once

#include <oxide/assets/mesh_data.hpp>

#include <string>
#include <vector>

namespace oxide {

class ObjReader {
  public:
	// Returns one indexed mesh for each non-empty OBJ shape. Missing normals
	// and texture coordinates are zero-filled.
	[[nodiscard]] static std::vector<MeshData> load(const std::string &path);
};

} // namespace oxide
