# Oxide notes

This file contains the detailed diagnostics, backend architecture notes, and
graphics/Vulkan background that support the shorter project README.

## Philosophy

- DOD core
- Wrappers for simplicity and RAII
- Keep it simple

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
modules, pipelines, descriptor layouts/pools, pipeline layouts, render passes,
and framebuffers. Swapchain images and frame synchronization objects remain
owned by `VulkanContext` because their lifetime is tied to one presentation
surface; swapchain-specific render passes and framebuffers stay with it for the
same reason.

Shader modules are cached by file path and reference-counted. Pooled memory
returns freed ranges to the allocator and coalesces adjacent ranges for reuse.
`UniformBufferRing` provides one buffer and descriptor set per frame slot so
the CPU does not overwrite memory the GPU may still be reading.

`Application` owns the GLFW context, window, Vulkan device, and presentation
context in teardown order. `FrameScope` begins a render pass and submits the
frame when the scope ends. `SwapchainPipeline` rebuilds a graphics pipeline
when a swapchain format change replaces its render pass.

`OffscreenTarget` owns one or more color images, an optional depth image,
render pass, framebuffer, and one sampler per resolved color image. Like the
other wrappers, it registers those device objects with `ResourceRegistry` and
stores typed handles instead of raw Vulkan objects. Its color attachments finish
every pass in `SHADER_READ_ONLY_OPTIMAL`, so a later `TextureDescriptorSet` can
sample them without a manual layout transition. `readPixels()` copies a resolved
image into tightly packed RGBA8 bytes and `savePng()` writes that data with
`stb_image_write`, which the build requires. Because the target depends only on
`VulkanDevice`, the same path works headless when the device is constructed
without a window.

`OffscreenConfig::colorFormats` turns the target into an MRT render pass, with
one fragment shader output and color blend attachment per format. Setting
`sampleCount` above `VK_SAMPLE_COUNT_1_BIT` makes the color and depth images
multisampled and adds a single-sample resolve attachment for every color target.
The render pass resolves automatically at subpass end; `colorImage()`,
`colorView()`, and `sampler()` refer to the resolved images, while
`attachmentImage()` and `attachmentView()` expose the multisampled attachments.
`beginRenderPass()` accepts either one clear color for all attachments or one
per color attachment.

`VulkanContext` applies the same pattern to window rendering through
`SwapchainConfig::sampleCount`: it allocates a multisampled color image and a
depth image for every swapchain image, renders into the pair owned by the
acquired image, and resolves into that swapchain image before presentation.
Per-image attachments keep frames in flight from writing the same depth or MSAA
color image concurrently. Unsupported sample counts fall back to
`VK_SAMPLE_COUNT_1_BIT` with a warning. `VulkanDevice::supportsSampleCount()`
probes format and usage combinations so callers can choose a valid count.

`ShaderLibrary` keeps shader selection readable:
`shaders.graphics("water")` resolves `water.vert.spv` and `water.frag.spv`,
while `shaders.compute("particles")` resolves `particles.comp.spv`.
`ShaderReflection` reads those modules with SPIRV-Reflect and merges the
interface of every stage: descriptor bindings, push-constant ranges, and
specialization constants. `PipelineLayout(device, reflection)` turns the merged
bindings into owned descriptor set layouts, including empty layouts for gaps in
set indices, and copies the reflected push ranges. Descriptor wrappers can then
allocate from `PipelineLayout::descriptorSetLayout(set)` so the shader and the
CPU-side layout share one source of truth. `SpecializationConstants` stores
per-stage overrides for `PipelineConfigInfo` (or the optional compute-pipeline
argument) and builds the `VkSpecializationInfo` used at pipeline creation.
Compute-only and offscreen programs can construct `VulkanDevice("Particles")`
without GLFW or a presentation context. `beginSingleTimeCommands()` and
`endSingleTimeCommands()` cover one-shot work such as uploads, readback, and
standalone headless frames.

`DeviceIdleGuard` should be declared after GPU resources when using the
lower-level ownership API. It waits for the device before those resources are
destroyed, including during exception unwinding.

Physical-device selection rejects devices without the required queues or
swapchain support, then ranks compatible devices by device type, maximum image
size, sampler anisotropy, and geometry-shader support.

Uniform and storage buffers use host-visible memory for simple updates and
readback. Vertex and index buffers use staging uploads into device-local
memory. `Buffer::read()` copies results back after the device is idle.

The project uses C++20, GLFW, Vulkan, `stb_image`/`stb_image_write`,
`glslc` or `glslangValidator`, and a vendored copy of SPIRV-Reflect under
`third_party/spirv-reflect/`.
Examples keep GLSL in `examples/<name>/shaders`; CMake compiles it to SPIR-V
next to the binary and defines `OXIDE_SHADER_DIR` and `OXIDE_ASSET_DIR`.

### File map

- **Core** — `include/oxide/vulkan/core/` contains typed handles, generational
  pools, resource records, the registry, and Vulkan error helpers.
- **Device** — `include/oxide/vulkan/device/` and `src/vulkan/device/` contain
  instance/device setup, queue selection, device ranking, and pooled memory.
- **Frame** — `include/oxide/vulkan/frame/` and `src/vulkan/frame/` contain
  surface, swapchain, offscreen targets, frame synchronization, and frame
  recording.
- **Pipeline** — `include/oxide/vulkan/pipeline/` and `src/vulkan/pipeline/`
  contain pipeline layouts, shader lookup, SPIR-V reflection, specialization
  constants, pipeline configuration, and graphics and compute pipelines.
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

### MSAA, MRT, and resolve

MSAA takes several color and depth samples per pixel and combines them at
coverage edges, which smooths polygon boundaries without rendering at a higher
resolution. A multisampled image cannot be sampled like an ordinary texture, so
a render pass resolves it into a separate single-sample image. In Vulkan the
subpass lists the resolve attachment in `pResolveAttachments`; the resolve
happens automatically when the subpass ends.

Multiple render targets let one fragment shader write several outputs in a
single pass. Each output has its own color attachment, format, and blend state.
The render pass and pipeline must agree on the number of color attachments, and
the pipeline's `rasterizationSamples` must match the target's sample count. A
common use is writing scene color plus auxiliary data such as a mask, normal, or
material ID, then combining the resolved images in a later pass.

The sample example renders a rotating triangle into two 4x MSAA attachments. The
first output is scene color; the second is a glow mask. The offscreen target
resolves both attachments, and a swapchain pass samples the two resolved images
through separate descriptor sets. See `examples/msaa_mrt_resolve`.

Swapchain images change layout as they move through a frame: they are acquired
for rendering, used as color attachments, and transitioned for presentation.
Render-pass dependencies and submission synchronization describe these
transitions to Vulkan.

Window resizing can invalidate the swapchain. The context waits for the
device, rebuilds swapchain-dependent resources, and continues with the new
extent.
