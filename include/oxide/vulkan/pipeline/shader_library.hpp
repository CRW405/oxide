#pragma once

#include <string>
#include <utility>

namespace oxide {

struct GraphicsShaderFiles {
	std::string vertex;
	std::string fragment;
};

struct ComputeShaderFile {
	std::string compute;
};

// Resolves named compiled shaders from one directory.
class ShaderLibrary {
  public:
	explicit ShaderLibrary(std::string directory) : directory_(std::move(directory)) {}

	[[nodiscard]] GraphicsShaderFiles graphics(const std::string &name) const;
	[[nodiscard]] ComputeShaderFile compute(const std::string &name) const;

  private:
	std::string directory_;
};

} // namespace oxide
