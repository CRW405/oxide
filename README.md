
# Oxide

## Description

Oxide is a small C++ engine layer for Vulkan.

It handles the setup that most Vulkan programs need:

- GLFW initialization and window creation
- Vulkan instance and validation layers
- Physical and logical device selection
- Graphics and presentation queues
- Swapchain creation and recreation
- Render passes and framebuffers with a depth attachment
- MSAA render targets with automatic resolve
- Multiple render targets (MRT) with per-attachment clears
- Command buffers and frame synchronization
- Vertex and index buffers
- OBJ and glTF triangle-mesh loading
- Texture loading and sampled image descriptor sets
- Uniform buffers and uniform buffer descriptor sets
- Pipeline layouts and graphics pipelines
- Shader reflection for descriptor sets, push constants, and specialization constants
- Offscreen and headless render targets with sampling and PNG readback

The code keeps Vulkan objects visible and explicit. It reduces repeated setup
without hiding how Vulkan works.

While Oxide does a lot of the heavy lifting and provides some useful defaults,
manual control is still recommended. Oxide is just intended to accelerate setup
and allow for more rapid experimentation and prototyping.

Oxide is built as the `oxide` library from `src/assets`, `src/glfw`, `src/tools`,
and `src/vulkan`, with public headers under `include/oxide`. Programs that use
it live under `examples/`, where each
example is a standalone executable with its own `main.cpp` and `shaders/`
directory:

- `examples/vulkan_triangle` — The classic Vulkan triangle
- `examples/indexed_quad` — Indexed meshes and the `Mesh` wrapper
- `examples/textured_quad` — Texture loading and sampled-image descriptors
- `examples/animated_uniform` — Per-frame uniform-buffer updates
- `examples/sierpinski` — A fragment-shader fractal driven by push constants
- `examples/hiss` — Animated glTF object using a per-frame UBO and push constants for the hiss shader
- `examples/model_loading` — Load and render OBJ and glTF triangle meshes side by side
- `examples/multiple_pipelines` — Two graphics shader pairs plus a compute pass sharing one device and frame
- `examples/offscreen_png` — Headless rendering into an offscreen image and writing it to a PNG
- `examples/render_to_texture` — Sampling an offscreen render target in a second pass
- `examples/msaa_mrt_resolve` — 4x MSAA with two render targets, resolved and composited on screen

## Use Case

This project is useful for learning Vulkan and for starting small rendering
experiments without rewriting the same initialization code each time.

It is aimed at programs that need direct control over:

- GPU resources
- Command recording
- Shader interfaces
- Synchronization
- Swapchain behavior

It is not a complete game engine. The OBJ and glTF readers load static triangle
geometry only; Oxide does not provide scenes, materials, cameras, or an entity
system.

Detailed diagnostics, backend architecture, file mapping, and graphics/Vulkan background are in [NOTES.md](NOTES.md).
Cheatsheet available in [CHEATSHEET.md](CHEATSHEET.md).

## Building

```sh
cmake -S . -B build
cmake --build build -j
```

The build requires Vulkan, GLFW, `stb_image`, `stb_image_write`, `tinyobjloader`,
and `cgltf`; building the examples also requires `cglm` and a GLSL-to-SPIR-V
compiler.

This builds the `oxide` library and every example. Run an example directly:

```sh
./build/examples/vulkan_triangle/vulkan_triangle
```

or through its convenience target:

```sh
cmake --build build --target run_vulkan_triangle
```

The same `run_` prefix works for all active examples, including
`multiple_pipelines`.
Pass `-DOXIDE_BUILD_EXAMPLES=OFF`
to configure the library without building any examples.

Run every example sequentially, with a separator printed before and after
each program:

```sh
make -C build runall
```

Vulkan validation layers and Oxide diagnostics are both enabled by default.
For a lean release build, disable either or both:

```sh
cmake -S . -B build \
    -DOXIDE_ENABLE_VALIDATION=OFF \
    -DOXIDE_ENABLE_LOGGING=OFF
cmake --build build -j
```

Disabling validation skips the validation layer and debug messenger. Disabling
logging makes the `OXIDE_LOG_*` statements discard their output; their
arguments are still evaluated.

### Future Work

- Ray Tracing
- Engine abstraction layer for things like Scene, Sprite, Material, Camera, etc. Kinda like raylib
- C API for Oxide, so that it can be used in C programs and other languages. Maybe refactor core into C and just have C++ wrappers
- Finish the Control hiss smoke effect

## Workflow

A program using Oxide normally follows this order:

1. Include `<oxide/oxide.hpp>` and create an `Application` (or create the GLFW, window, device, and context
   objects individually when custom ownership is needed).
2. Load static geometry with `ObjReader::load(path)` or `GltfReader::load(path)`, then upload each
   returned `MeshData` with `Mesh(device, data.vertices, data.indices)`. Readers return one mesh per
   OBJ shape or glTF scene-node primitive; use the `MeshVertex` position, normal, and texture-coordinate
   fields to configure vertex input.
3. Create textures, uniform buffers, descriptor sets, pipeline layouts, and pipelines.
4. Start the render loop.
5. Poll window events and read input.
6. Call `beginFrameScope(clearColor)`.
7. Set the viewport and scissor, bind pipelines and buffers, then issue draw
   commands. The frame scope ends the render pass and submits on scope exit.
8. Declare a `DeviceIdleGuard` after the GPU resources when using the
    lower-level ownership API so the device becomes
    idle before those resources are destroyed.

If the window changes size, the context recreates the swapchain and the
resources that depend on it. `beginFrameScope()` returns an inactive scope while that
work is being done, so the application can skip that iteration. If the surface
format changes, `SwapchainPipeline` rebuilds its pipeline automatically.

Shader reflection is available through `ShaderReflection::fromGraphics()` and
`fromCompute()`. Pass the result to `PipelineLayout` to create matching
descriptor set layouts and push-constant ranges instead of declaring them by
hand. The same reflection object reports specialization constants, which
`PipelineConfigInfo::vertexSpecialization` and `fragmentSpecialization` can
override before pipeline creation; `ComputePipeline` takes an optional
`SpecializationConstants` argument.

For offscreen work, create an `OffscreenTarget` and record its pass with
`beginRenderPass()`/`endRenderPass()`. The color image finishes in a sampled
layout, so a later pass can bind it through `TextureDescriptorSet`, or
`readPixels()`/`savePng()` can copy it back. Set `OffscreenConfig::colorFormats`
for multiple render targets and `sampleCount` for MSAA; each multisampled
attachment is resolved automatically into the sampleable `colorImage(index)`.
Constructing `VulkanDevice` without a window makes the same path fully headless.
`SwapchainConfig::sampleCount` enables the same resolve path for window
rendering.
