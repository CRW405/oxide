# Oxide notes

This file contains the detailed diagnostics, backend architecture notes, and
graphics/Vulkan background that support the shorter project README.

## Diagnostics

All engine diagnostics go to stderr through the logger in
`include/oxide/tools/log.hpp`. Each line carries a severity, source location,
and message:

```text
[oxide][INFO] vulkan_device.cpp:337: selected Vulkan device: AMD Radeon RX 9070 XT
[oxide][WARNING] vulkan_context.cpp:113: preferred surface format is unavailable; using a fallback
[oxide][ERROR] buffer.cpp:24: create buffer failed with VK_ERROR_OUT_OF_DEVICE_MEMORY (-2)
```

Severity tags are colorized when stderr is a terminal. Set `NO_COLOR=1` to
disable colors.

Vulkan validation warnings and errors use the same logger with a
`Vulkan validation` source label. The debug messenger reports warning and error
severities only. `-DOXIDE_ENABLE_VALIDATION=OFF` disables the validation layer
and debug messenger. If the validation layer is unavailable, startup logs a
warning and continues without it.

Exceptions include their throw site in `what()`. Example programs catch
`std::exception`, report it with `OXIDE_LOG_FATAL`, and exit non-zero. New
diagnostics should use `OXIDE_LOG_*` so the call site is included
automatically. With `-DOXIDE_ENABLE_LOGGING=OFF`, logging output is discarded
but exception behavior is unchanged.

## Technical overview

Oxide uses RAII for Vulkan and GLFW objects. Objects are created in dependency
order and destroyed in reverse. The public API lives in the `oxide` namespace.

`VulkanDevice` owns the Vulkan instance, selected physical device, logical
device, debug messenger, shared command pool, and graphics/present queues.
`VulkanContext` owns the surface, swapchain, image views, depth resources,
render pass, framebuffers, command buffers, semaphores, and fences for one
window. It also handles resize and swapchain recreation.

The backend uses a data-oriented resource core. `ResourceRegistry` stores
Vulkan records behind typed generational handles, so stale handles fail lookup
instead of silently referring to reused slots. It owns buffers, images, shader
modules, pipelines, descriptor layouts/pools, and pipeline layouts. Swapchain
and frame synchronization objects remain owned by `VulkanContext` because
their lifetime is tied to one presentation surface.

Shader modules are cached by file path and reference-counted. Pooled memory
returns freed ranges to the allocator and coalesces adjacent ranges for reuse.
`UniformBufferRing` provides one buffer and descriptor set per frame slot so
the CPU does not overwrite memory the GPU may still be reading.

`Application` owns the GLFW context, window, Vulkan device, and presentation
context in teardown order. `FrameScope` begins a render pass and submits the
frame when the scope ends. `SwapchainPipeline` rebuilds a graphics pipeline
when a swapchain format change replaces its render pass.

`ShaderLibrary` keeps shader selection readable:
`shaders.graphics("water")` resolves `water.vert.spv` and `water.frag.spv`,
while `shaders.compute("particles")` resolves `particles.comp.spv`.
Compute-only programs can construct `VulkanDevice("Particles")` without GLFW
or a presentation context.

`DeviceIdleGuard` should be declared after GPU resources when using the
lower-level ownership API. It waits for the device before those resources are
destroyed, including during exception unwinding.

Physical-device selection rejects devices without the required queues or
swapchain support, then ranks compatible devices by device type, maximum image
size, sampler anisotropy, and geometry-shader support.

Uniform and storage buffers use host-visible memory for simple updates and
readback. Vertex and index buffers use staging uploads into device-local
memory. `Buffer::read()` copies results back after the device is idle.

The project uses C++20, GLFW, Vulkan, and `glslc` or `glslangValidator`.
Examples keep GLSL in `examples/<name>/shaders`; CMake compiles it to SPIR-V
next to the binary and defines `OXIDE_SHADER_DIR` and `OXIDE_ASSET_DIR`.

### File map

- **Core** — `include/oxide/vulkan/core/` contains typed handles, generational
  pools, resource records, the registry, and Vulkan error helpers.
- **Device** — `include/oxide/vulkan/device/` and `src/vulkan/device/` contain
  instance/device setup, queue selection, device ranking, and pooled memory.
- **Frame** — `include/oxide/vulkan/frame/` and `src/vulkan/frame/` contain
  surface, swapchain, frame synchronization, and frame recording.
- **Pipeline** — `include/oxide/vulkan/pipeline/` and `src/vulkan/pipeline/`
  contain pipeline layouts, shader lookup, pipeline configuration, and graphics
  and compute pipelines.
- **Resources** — `include/oxide/vulkan/resources/` and `src/vulkan/resources/`
  contain buffers, textures, descriptors, and uniform-buffer helpers.
- **Application** — `include/oxide/vulkan/application.hpp` and
  `src/vulkan/application.cpp` provide the high-level bootstrap.
- **Examples** — each directory under `examples/` is a standalone program
  with its own `main.cpp` and `shaders/`.

`BorrowedPipelineLayout` marks a layout supplied by another owner. Pipelines
use it without destroying the layout. `PipelineLayout` remains the owning
wrapper for layouts created by Oxide.

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
The fragment shader determines each fragment's color. The resulting pixels are
written to a framebuffer.

Shader inputs and CPU data must agree. In the triangle example, vertex location
`0` is a two-component position and location `1` is a three-component color.
The C++ structure, Vulkan vertex descriptions, and shader declarations all
describe the same data.

### Vulkan

Vulkan is explicit. The application states which resources it uses, how they
are connected, when work is submitted, and when resources can be reused or
destroyed.

The main Vulkan objects used here follow this order:

1. An instance connects the application to the Vulkan implementation.
2. A surface connects Vulkan to the window system.
3. A physical device is selected based on queue and swapchain support.
4. A logical device provides the application queues.
5. A swapchain provides images that can be presented.
6. Image views, a render pass, and framebuffers describe render targets.
7. Command buffers record GPU work.
8. Semaphores and fences coordinate work.
9. The presentation queue displays completed swapchain images.

Semaphores coordinate operations submitted to queues. Fences let the CPU check
whether submitted work has completed. The context uses two frames in flight so
the CPU can prepare another frame while the GPU works on the previous one.

Swapchain images change layout as they move through a frame: they are acquired
for rendering, used as color attachments, and transitioned for presentation.
Render-pass dependencies and submission synchronization describe these
transitions to Vulkan.

Window resizing can invalidate the swapchain. The context waits for the
device, rebuilds swapchain-dependent resources, and continues with the new
extent.
