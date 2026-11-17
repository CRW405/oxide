#define TINYOBJLOADER_IMPLEMENTATION
#include <tiny_obj_loader.h>

#include <oxide/assets/obj_reader.hpp>
#include <oxide/tools/log.hpp>

#include <array>
#include <filesystem>
#include <limits>
#include <string>
#include <unordered_map>

namespace oxide {
namespace {

struct ObjVertexKey {
	int position;
	int normal;
	int texCoord;

	bool operator==(const ObjVertexKey &) const = default;
};

struct ObjVertexKeyHash {
	std::size_t operator()(const ObjVertexKey &key) const noexcept {
		std::size_t hash = std::hash<int>{}(key.position);
		hash ^= std::hash<int>{}(key.normal) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
		hash ^= std::hash<int>{}(key.texCoord) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
		return hash;
	}
};

template <std::size_t N>
void readObjAttribute(
    const std::vector<tinyobj::real_t> &source,
    int                                 index,
    std::array<float, N>               &destination,
    const char                         *attribute,
    const std::string                  &path) {
	if (index < 0 || static_cast<std::size_t>(index) >= source.size() / N) {
		throw runtimeError(
		    "OBJ '" + path + "' has an invalid " + attribute + " index " + std::to_string(index));
	}
	for (std::size_t component = 0; component < N; ++component) {
		destination[component] = static_cast<float>(source[static_cast<std::size_t>(index) * N + component]);
	}
}

} // namespace

std::vector<MeshData> ObjReader::load(const std::string &path) {
	tinyobj::attrib_t                attributes;
	std::vector<tinyobj::shape_t>    shapes;
	std::vector<tinyobj::material_t> materials;
	std::string                      warning;
	std::string                      error;
	const std::string                materialDirectory = std::filesystem::path(path).parent_path().string();

	if (!tinyobj::LoadObj(
	        &attributes,
	        &shapes,
	        &materials,
	        &warning,
	        &error,
	        path.c_str(),
	        materialDirectory.empty() ? nullptr : materialDirectory.c_str(),
	        true)) {
		throw runtimeError("failed to load OBJ '" + path + "'" +
		                   (error.empty() ? "" : ": " + error));
	}
	if (!warning.empty()) {
		OXIDE_LOG_WARNING << "OBJ '" << path << "': " << warning;
	}

	std::vector<MeshData> result;
	for (const tinyobj::shape_t &shape : shapes) {
		if (shape.mesh.indices.empty()) {
			continue;
		}
		if (shape.mesh.indices.size() > std::numeric_limits<std::uint32_t>::max()) {
			throw runtimeError("OBJ shape '" + shape.name + "' has too many indices");
		}

		MeshData mesh;
		mesh.name = shape.name;
		mesh.indices.reserve(shape.mesh.indices.size());
		std::unordered_map<ObjVertexKey, std::uint32_t, ObjVertexKeyHash> vertexIndices;

		for (const tinyobj::index_t &index : shape.mesh.indices) {
			const ObjVertexKey key{ index.vertex_index, index.normal_index, index.texcoord_index };
			auto [it, inserted] = vertexIndices.try_emplace(
			    key, static_cast<std::uint32_t>(mesh.vertices.size()));
			if (inserted) {
				if (mesh.vertices.size() >= std::numeric_limits<std::uint32_t>::max()) {
					throw runtimeError("OBJ shape '" + shape.name + "' has too many vertices");
				}
				MeshVertex vertex{};
				readObjAttribute(attributes.vertices, index.vertex_index, vertex.position, "position", path);
				if (index.normal_index >= 0) {
					readObjAttribute(attributes.normals, index.normal_index, vertex.normal, "normal", path);
				} else if (index.normal_index != -1) {
					throw runtimeError("OBJ '" + path + "' has an invalid normal index");
				}
				if (index.texcoord_index >= 0) {
					readObjAttribute(attributes.texcoords, index.texcoord_index, vertex.texCoord, "texture", path);
				} else if (index.texcoord_index != -1) {
					throw runtimeError("OBJ '" + path + "' has an invalid texture-coordinate index");
				}
				mesh.vertices.push_back(vertex);
			}
			mesh.indices.push_back(it->second);
		}
		result.push_back(std::move(mesh));
	}
	return result;
}

} // namespace oxide
