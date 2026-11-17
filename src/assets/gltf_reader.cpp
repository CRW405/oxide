#define CGLTF_IMPLEMENTATION
#include <cgltf/cgltf.h>

#include <oxide/assets/gltf_reader.hpp>
#include <oxide/tools/log.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace oxide {
namespace {

using GltfData = std::unique_ptr<cgltf_data, decltype(&cgltf_free)>;

std::vector<float> unpackAttribute(const cgltf_accessor *accessor, cgltf_size expectedComponents, const std::string &path) {
	if (accessor == nullptr || cgltf_num_components(accessor->type) != expectedComponents) {
		throw runtimeError("glTF '" + path + "' has a missing or invalid vertex attribute");
	}
	const cgltf_size floatCount = cgltf_accessor_unpack_floats(accessor, nullptr, 0);
	if (floatCount != accessor->count * expectedComponents) {
		throw runtimeError("glTF '" + path + "' has an invalid vertex attribute size");
	}
	std::vector<float> values(floatCount);
	if (cgltf_accessor_unpack_floats(accessor, values.data(), floatCount) != floatCount) {
		throw runtimeError("failed to read a vertex attribute from glTF '" + path + "'");
	}
	return values;
}

std::array<float, 3> transformPosition(const float *matrix, const std::array<float, 3> &position) {
	return {
		matrix[0] * position[0] + matrix[4] * position[1] + matrix[8] * position[2] + matrix[12],
		matrix[1] * position[0] + matrix[5] * position[1] + matrix[9] * position[2] + matrix[13],
		matrix[2] * position[0] + matrix[6] * position[1] + matrix[10] * position[2] + matrix[14],
	};
}

std::array<float, 3> transformNormal(const float *matrix, const std::array<float, 3> &normal, const std::string &path) {
	const float a00 = matrix[0], a01 = matrix[4], a02 = matrix[8];
	const float a10 = matrix[1], a11 = matrix[5], a12 = matrix[9];
	const float a20 = matrix[2], a21 = matrix[6], a22 = matrix[10];
	const float c00         = a11 * a22 - a12 * a21;
	const float c01         = a12 * a20 - a10 * a22;
	const float c02         = a10 * a21 - a11 * a20;
	const float c10         = a02 * a21 - a01 * a22;
	const float c11         = a00 * a22 - a02 * a20;
	const float c12         = a01 * a20 - a00 * a21;
	const float c20         = a01 * a12 - a02 * a11;
	const float c21         = a02 * a10 - a00 * a12;
	const float c22         = a00 * a11 - a01 * a10;
	const float determinant = a00 * c00 + a01 * c01 + a02 * c02;
	if (std::abs(determinant) <= std::numeric_limits<float>::epsilon()) {
		throw runtimeError("glTF '" + path + "' has a singular transform for a mesh with normals");
	}

	const float          sign = determinant < 0.0f ? -1.0f : 1.0f;
	std::array<float, 3> transformed{
		sign * (c00 * normal[0] + c01 * normal[1] + c02 * normal[2]),
		sign * (c10 * normal[0] + c11 * normal[1] + c12 * normal[2]),
		sign * (c20 * normal[0] + c21 * normal[1] + c22 * normal[2]),
	};
	const float length = std::sqrt(
	    transformed[0] * transformed[0] +
	    transformed[1] * transformed[1] +
	    transformed[2] * transformed[2]);
	if (length > std::numeric_limits<float>::epsilon()) {
		for (float &component : transformed) {
			component /= length;
		}
	}
	return transformed;
}

std::string meshName(const cgltf_node *node, const cgltf_mesh *mesh, cgltf_size primitiveIndex) {
	std::string name = node->name != nullptr ? node->name : mesh->name != nullptr ? mesh->name
	                                                                              : "mesh";
	name += "/primitive_" + std::to_string(primitiveIndex);
	return name;
}

void appendPrimitive(
    const cgltf_node      *node,
    const cgltf_mesh      &sourceMesh,
    const cgltf_primitive &primitive,
    cgltf_size             primitiveIndex,
    const std::string     &path,
    std::vector<MeshData> &result) {
	if (primitive.type != cgltf_primitive_type_triangles) {
		throw runtimeError("glTF '" + path + "' contains a non-triangle primitive");
	}
	if (primitive.has_draco_mesh_compression) {
		throw runtimeError("glTF '" + path + "' uses Draco compression, which is not supported");
	}

	const cgltf_accessor *positions = nullptr;
	const cgltf_accessor *normals   = nullptr;
	const cgltf_accessor *texCoords = nullptr;
	for (cgltf_size i = 0; i < primitive.attributes_count; ++i) {
		const cgltf_attribute &attribute = primitive.attributes[i];
		if (attribute.type == cgltf_attribute_type_position && attribute.index == 0) {
			positions = attribute.data;
		} else if (attribute.type == cgltf_attribute_type_normal && attribute.index == 0) {
			normals = attribute.data;
		} else if (attribute.type == cgltf_attribute_type_texcoord && attribute.index == 0) {
			texCoords = attribute.data;
		}
	}

	const std::vector<float> positionValues = unpackAttribute(positions, 3, path);
	const cgltf_size         vertexCount    = positions->count;
	if (vertexCount == 0 || vertexCount > std::numeric_limits<std::uint32_t>::max()) {
		throw runtimeError("glTF '" + path + "' has an invalid vertex count");
	}
	if (normals != nullptr && normals->count != vertexCount) {
		throw runtimeError("glTF '" + path + "' has mismatched normal and position counts");
	}
	if (texCoords != nullptr && texCoords->count != vertexCount) {
		throw runtimeError("glTF '" + path + "' has mismatched texture-coordinate and position counts");
	}
	const std::vector<float> normalValues   = normals == nullptr ? std::vector<float>{} : unpackAttribute(normals, 3, path);
	const std::vector<float> texCoordValues = texCoords == nullptr ? std::vector<float>{} : unpackAttribute(texCoords, 2, path);

	MeshData mesh;
	mesh.name = meshName(node, &sourceMesh, primitiveIndex);
	mesh.vertices.resize(vertexCount);

	std::array<float, 16> worldTransform{};
	if (node != nullptr) {
		cgltf_node_transform_world(node, worldTransform.data());
	} else {
		worldTransform[0] = worldTransform[5] = worldTransform[10] = worldTransform[15] = 1.0f;
	}
	for (cgltf_size i = 0; i < vertexCount; ++i) {
		MeshVertex                &vertex = mesh.vertices[i];
		const std::array<float, 3> position{
			positionValues[i * 3],
			positionValues[i * 3 + 1],
			positionValues[i * 3 + 2],
		};
		vertex.position = transformPosition(worldTransform.data(), position);
		if (normals != nullptr) {
			vertex.normal = transformNormal(
			    worldTransform.data(),
			    { normalValues[i * 3], normalValues[i * 3 + 1], normalValues[i * 3 + 2] },
			    path);
		}
		if (texCoords != nullptr) {
			vertex.texCoord = { texCoordValues[i * 2], texCoordValues[i * 2 + 1] };
		}
	}

	if (primitive.indices == nullptr) {
		mesh.indices.resize(vertexCount);
		for (cgltf_size i = 0; i < vertexCount; ++i) {
			mesh.indices[i] = static_cast<std::uint32_t>(i);
		}
	} else {
		if (primitive.indices->is_sparse) {
			throw runtimeError("glTF '" + path + "' uses sparse index accessors, which are not supported");
		}
		if (primitive.indices->count > std::numeric_limits<std::uint32_t>::max()) {
			throw runtimeError("glTF '" + path + "' has too many indices");
		}
		mesh.indices.resize(primitive.indices->count);
		for (cgltf_size i = 0; i < primitive.indices->count; ++i) {
			const cgltf_size index = cgltf_accessor_read_index(primitive.indices, i);
			if (index >= vertexCount) {
				throw runtimeError("glTF '" + path + "' contains an out-of-range vertex index");
			}
			mesh.indices[i] = static_cast<std::uint32_t>(index);
		}
	}
	if (mesh.indices.size() % 3 != 0) {
		throw runtimeError("glTF '" + path + "' has a triangle primitive with an invalid index count");
	}
	result.push_back(std::move(mesh));
}

void appendNode(const cgltf_node *node, const std::string &path, std::vector<MeshData> &result) {
	if (node->mesh != nullptr) {
		for (cgltf_size i = 0; i < node->mesh->primitives_count; ++i) {
			appendPrimitive(
			    node,
			    *node->mesh,
			    node->mesh->primitives[i],
			    i,
			    path,
			    result);
		}
	}
	for (cgltf_size i = 0; i < node->children_count; ++i) {
		appendNode(node->children[i], path, result);
	}
}

} // namespace

std::vector<MeshData> GltfReader::load(const std::string &path) {
	cgltf_options options{};
	cgltf_data   *rawData = nullptr;
	cgltf_result  result  = cgltf_parse_file(&options, path.c_str(), &rawData);
	if (result != cgltf_result_success) {
		throw runtimeError("failed to parse glTF '" + path + "' (error " + std::to_string(result) + ")");
	}
	GltfData data(rawData, &cgltf_free);
	result = cgltf_load_buffers(&options, data.get(), path.c_str());
	if (result != cgltf_result_success) {
		throw runtimeError("failed to load glTF buffers from '" + path + "' (error " + std::to_string(result) + ")");
	}
	result = cgltf_validate(data.get());
	if (result != cgltf_result_success) {
		throw runtimeError("glTF '" + path + "' is invalid (error " + std::to_string(result) + ")");
	}

	std::vector<MeshData> meshes;
	const cgltf_scene    *scene = data->scene;
	if (scene == nullptr && data->scenes_count != 0) {
		scene = &data->scenes[0];
	}
	if (scene != nullptr) {
		for (cgltf_size i = 0; i < scene->nodes_count; ++i) {
			appendNode(scene->nodes[i], path, meshes);
		}
	} else {
		for (cgltf_size i = 0; i < data->nodes_count; ++i) {
			if (data->nodes[i].parent == nullptr) {
				appendNode(&data->nodes[i], path, meshes);
			}
		}
	}
	return meshes;
}

} // namespace oxide
