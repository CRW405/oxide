
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
- Command buffers and frame synchronization
- Vertex and index buffers
- Texture loading and sampled image descriptor sets
- Uniform buffers and uniform buffer descriptor sets
- Pipeline layouts and graphics pipelines

The code keeps Vulkan objects visible and explicit. It reduces repeated setup
without hiding how Vulkan works.

While Oxide does a lot of the heavy lifting and provides some useful defaults,
manual control is still recommended. Oxide is just intended to accelerate setup
and allow for more rapid experimentation and prototyping.

Oxide is built as the `oxide` library from `src/glfw`, `src/tools`,
and `src/vulkan`, with public headers under `include/oxide`. Programs that use
it live under `examples/`, where each
example is a standalone executable with its own `main.cpp` and `shaders/`
directory:

- `examples/triangle` — rotating indexed triangle with a transform push constant
- `examples/sierpinski` — fragment-shader fractal driven by push constants
- `examples/image_texture` — textured quad with a sampled image descriptor
- `examples/uniform_buffer` — rotating quad using model-view-projection uniforms
- `examples/showcase` — all of the above combined, including input handling and FPS reporting
- `examples/compute` — a compute dispatch over a storage buffer with host readback

## Use Case

This project is useful for learning Vulkan and for starting small rendering
experiments without rewriting the same initialization code each time.

It is aimed at programs that need direct control over:

- GPU resources
- Command recording
- Shader interfaces
- Synchronization
- Swapchain behavior

It is not a complete game engine. It does not provide scenes, materials,
asset loading, cameras, or an entity system. (yet (maybe))

## Building

```sh
cmake -S . -B build
cmake --build build -j
```

This builds the `oxide` library and every example. Run an example directly:

```sh
./build/examples/showcase/showcase
```

or through its convenience target:

```sh
cmake --build build --target run_showcase
```

The same `run_` prefix works for `triangle`, `sierpinski`, `image_texture`,
`uniform_buffer`, `showcase`, and `compute`. Pass `-DOXIDE_BUILD_EXAMPLES=OFF`
to configure the library without building any examples.

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

## Diagnostics

All engine diagnostics go to stderr through the small logger in
`include/oxide/tools/log.hpp`. Each line carries a severity, the source location that
produced it, and a message:

```
[oxide][INFO] vulkan_device.cpp:337: selected Vulkan device: AMD Radeon RX 9070 XT
[oxide][WARNING] vulkan_context.cpp:113: preferred surface format B8G8R8A8_SRGB is unavailable; using format 44
[oxide][ERROR] buffer.cpp:24: create buffer failed with VK_ERROR_OUT_OF_DEVICE_MEMORY (-2)
[oxide][FATAL] main.cpp:90: unhandled exception: image_texture.cpp:143: failed to load texture image '...'
```

Severity tags are colorized when stderr is a terminal. Set `NO_COLOR=1` to
disable colors for logs captured to a file or another program.

Warnings and errors from the Vulkan validation layer use the same format with a
`Vulkan validation` source label. The debug messenger subscribes only to warning
and error severities so loader info messages do not bury real problems.
`-DOXIDE_ENABLE_VALIDATION=OFF` skips the layer and debug messenger entirely.
If validation is requested but `VK_LAYER_KHRONOS_validation` is unavailable,
startup logs a warning and continues without the layer. Validation output also
goes through the logger, so `-DOXIDE_ENABLE_LOGGING=OFF` suppresses it as well.

Exceptions thrown by the engine embed their throw site in `what()`. Example
programs catch `std::exception` and report it with `OXIDE_LOG_FATAL`, then exit
with a non-zero status. New diagnostics should use the `OXIDE_LOG_*` macros so
they automatically report the call site. With `-DOXIDE_ENABLE_LOGGING=OFF`,
those macros discard their output (their arguments are still evaluated), but
exception behavior is unchanged.

## TODO

### Rendering

- [ ] MSAA, multiple render targets, and resolve
- [ ] Offscreen and headless rendering
- [ ] Pipeline cache, shader reflection, and specialization constants

### Performance and resources

- [ ] Memory allocator instead of one `vkAllocateMemory` per resource
- [ ] Staging uploads and device-local vertex/index buffers
- [ ] Persistent mapping for host-visible buffers
- [ ] Texture mipmaps, format selection, and sampler configuration

### Device and platform

- [ ] Score physical devices instead of selecting the first suitable one
- [ ] Query and enable device features; recover from device and surface loss
- [ ] MoltenVK portability support (`VK_KHR_portability_enumeration`)
- [ ] Fall back with a warning when validation layers are unavailable

### Engine and assets

- [ ] OBJ reader
- [ ] GLTF reader
- [ ] DOD Refactor

## Technical overview

Oxide uses RAII for both Vulkan and GLFW objects: objects are created in
dependency order and destroyed in reverse, and the public API lives in the
`oxide` namespace.

`VulkanDevice` owns the Vulkan instance, selected physical device, logical
device, debug messenger, shared command pool, and graphics/present queues.
`VulkanContext` owns everything tied to one window: surface, swapchain, color
image views, a depth image and view, render pass, framebuffers, command buffers,
semaphores, and fences. The depth format is selected from the formats the
physical device supports for depth-stencil attachments, preferring
`VK_FORMAT_D32_SFLOAT`. It also handles resize and swapchain recreation. The
frame loop keeps two frames in flight; `beginFrame()` waits on the current
slot's fence and returns an empty frame while the swapchain is rebuilt.
`SwapchainConfig` can request a surface format/color space, present mode, extra
image usage, or image count; unsupported requests fall back with a warning.
`swapchainGeneration()` and `setSwapchainRecreatedCallback()` report each
rebuild, and `SwapchainRecreationInfo::renderPassChanged` tells an application
when pipelines must be recreated. Minimized-window waits stop when the window is
closed instead of blocking forever.

Drawing resources follow the same pattern, with one wrapper per Vulkan concept:
buffers and meshes, images and textures, descriptor layouts/pools/sets, pipeline
layouts with typed push constants, and graphics pipelines with configurable
fixed-function state. The descriptor helpers default to the two common
single-resource interfaces — one combined image sampler or one uniform buffer at
binding `0`. For data rewritten every frame, `UniformBufferRing` provides one
buffer and descriptor set per frame slot, so the CPU never overwrites memory the
GPU may still be reading. `ComputePipeline` creates a compute pipeline from one
`.comp` SPIR-V file and dispatches after binding its descriptor sets.

Declare a `DeviceIdleGuard` after the GPU resources in a scope. It waits for the
device when destroyed, so resources cannot be released while submitted work is
still using them, including during exception unwinding.

Buffers currently use host-visible, host-coherent memory, which keeps uploads
simple; `Buffer::read()` copies results back after the device is idle. A larger
renderer would stage into device-local memory instead.

The project uses C++20, GLFW, Vulkan, and `glslc` or `glslangValidator`. Each
example keeps its GLSL in `examples/<name>/shaders`; CMake compiles it to SPIR-V
next to the binary and defines `OXIDE_SHADER_DIR` and `OXIDE_ASSET_DIR`, so
binaries can run from any working directory.

### File map

- **Build and configuration** — `CMakeLists.txt` builds `oxide` as a static
  library, publishes its include directories and dependencies, defines the
  `OXIDE_ENABLE_VALIDATION` and `OXIDE_ENABLE_LOGGING` options, and installs a
  CMake package for `find_package(oxide)`.
  `examples/CMakeLists.txt` declares the example executables, compiles their
  shaders, and adds the `run_<example>` targets.
  `include/oxide/oxide_config.hpp` holds the compile-time defaults for those
  switches.
- **GLFW layer** — `include/oxide/glfw/glfw_oxide.hpp` and
  `src/glfw/glfw_oxide.cpp` declare `WindowConfig`, `MousePosition`,
  `GLFWContext`, and `Window`, and implement initialization, window lifecycle,
  callback dispatch, input tracking, and the GLFW error callback.
- **Utilities** — `include/oxide/tools/log.hpp` and `src/tools/log.cpp`
  provide log levels, `LogLine`, located exception factories, `OXIDE_LOG_*`
  macros, and `NO_COLOR` handling. `include/oxide/tools/fps.hpp` and
  `src/tools/fps.cpp` provide `FPSCounter`.
- **Vulkan core** — `include/oxide/vulkan/vulkan_device.hpp` and
  `src/vulkan/vulkan_device.cpp` declare `QueueFamilies` and `VulkanDevice` and
  implement instance, device, queue, and memory-type setup.
  `include/oxide/vulkan/vulkan_context.hpp` and
  `src/vulkan/vulkan_context.cpp` declare `Frame`, `MAX_FRAMES_IN_FLIGHT`,
  `SwapchainConfig`, `SwapchainRecreationInfo`, and `VulkanContext` and
  implement per-window presentation, frame lifecycle, configurable swapchain
  creation, and recreation notifications. `DeviceIdleGuard` lives beside
  `VulkanDevice`. `include/oxide/vulkan/vk_error.hpp` maps `VkResult` values to
  names and provides `checkVk`, `vkError`, and `logVkFailure`.
- **Buffers** — `include/oxide/vulkan/buffer.hpp` and `src/vulkan/buffer.cpp`
  declare `Buffer`, `VertexBuffer`, `IndexBuffer`, `UniformBuffer`, and
  `Mesh`, and implement allocation, mapping, upload/update, binding, and
  indexed draws.
- **Descriptors** — `include/oxide/vulkan/descriptor_set.hpp` and
  `src/vulkan/descriptor_set.cpp` declare `DescriptorBinding`, the
  `combinedImageSampler()`, `uniformBuffer()`, and `storageBuffer()` factories,
  and the `DescriptorSetLayout`, `DescriptorPool`, and `DescriptorSet` wrappers.
  `ubo_descriptor` and `uniform_buffer_ring` add the single and per-frame UBO
  helpers; `texture_descriptor` owns the layout, pool, and set for an
  `ImageTexture`.
- **Textures** — `include/oxide/vulkan/image_texture.hpp` and
  `src/vulkan/image_texture.cpp` own a `VkImage`, memory, view, and sampler. It
  loads with stb_image, uploads through a staging buffer, transitions layout,
  and cleans up partial resources on failure.
- **Pipelines** — `include/oxide/vulkan/pipeline_layout.hpp` and
  `src/vulkan/pipeline_layout.cpp` declare `PushConstant`,
  `pushConstant<T>()`, `pushConstantRange<T>()`, and `PipelineLayout`, including
  descriptor-set binding. `include/oxide/vulkan/render_pipeline.hpp` and
  `src/vulkan/render_pipeline.cpp` declare `VertexInputDescription`,
  `PipelineConfigInfo`, `Pipeline`, and `ComputePipeline`, with pointer repair
  for copied configs, `defaultPipelineConfigInfo()`, SPIR-V loading, graphics
  pipeline creation, and compute pipeline creation/dispatch.
- **Assets and examples** — `assets/smiley_0.png` is shared by the image and
  showcase examples. Each directory under `examples/` is a standalone program
  with its own `main.cpp` and `shaders/`; the Description above summarizes what
  each one demonstrates.

## Workflow

A program using Oxide normally follows this order:

1. Create a `GLFWContext`.
2. Create a `Window`.
3. Create a `VulkanDevice` using the window.
4. Create a `VulkanContext`.
5. Create meshes, textures, uniform buffers, descriptor sets, pipeline layouts, and pipelines.
6. Start the render loop.
7. Poll window events and read input.
8. Call `beginFrame()`.
9. Begin a render pass and set the viewport and scissor.
10. Bind pipelines and buffers, then issue draw commands.
11. End the render pass and call `endFrame()`.
12. Declare a `DeviceIdleGuard` after the GPU resources so the device becomes
    idle before those resources are destroyed.

If the window changes size, the context recreates the swapchain and the
resources that depend on it. `beginFrame()` returns an empty frame while that
work is being done, so the application can skip that iteration. If the surface
format changes, `SwapchainRecreationInfo::renderPassChanged` is true and graphics
pipelines built against the old render pass must be recreated; see the showcase
example's `setSwapchainRecreatedCallback()` handler.

## Theory

### General graphics programming

The CPU prepares data and commands. The GPU processes those commands later.
Creating or recording a command does not mean that the GPU has finished it.

A basic draw needs:

1. Vertex data
2. A description of the vertex data layout
3. A graphics pipeline
4. A render target
5. Recorded draw commands
6. Synchronization between the CPU, GPU, and display system

A graphics pipeline turns vertex data into pixels. The vertex shader processes
vertices and produces positions. Rasterization turns primitives into fragments.
The fragment shader determines the color of each fragment. The resulting
pixels are written to a framebuffer.

Shader inputs and CPU data must agree. In the triangle example, vertex
location `0` is a two-component position and location `1` is a three-component
color. The C++ structure, Vulkan vertex descriptions, and shader declarations
all describe the same data.

### Vulkan

Vulkan is an explicit API. The application must state which resources it
uses, how they are connected, when work is submitted, and when resources can
be reused or destroyed.

The main Vulkan objects used here follow this order:

1. An instance connects the application to the Vulkan implementation.
2. A surface connects Vulkan to the window system.
3. A physical device is selected based on queue and swapchain support.
4. A logical device provides the queues used by the application.
5. A swapchain provides images that can be presented to the window.
6. Image views, a render pass, and framebuffers describe the render targets.
7. Command buffers record GPU work.
8. Semaphores and fences coordinate work.
9. The presentation queue displays completed swapchain images.

Semaphores coordinate operations submitted to queues. Fences let the CPU
check whether submitted work has completed. The context uses two frames in
flight so the CPU can prepare another frame while the GPU works on the
previous one.

Swapchain images change layout as they move through the frame. They are
acquired for rendering, used as color attachments, and then transitioned for
presentation. Render pass dependencies and submission synchronization describe
these transitions to Vulkan.

Window resizing can invalidate the swapchain. The context waits for the
device, rebuilds the swapchain-dependent resources, and continues rendering
with the new extent.
