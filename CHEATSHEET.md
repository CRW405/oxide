# Oxide cheatsheet

The examples are the primary documentation. Start with `vulkan_triangle`, then
look at the example for the feature you need:

| Feature | Example |
| --- | --- |
| First triangle and frame loop | `vulkan_triangle` |
| Indexed mesh and `Mesh` | `indexed_quad` |
| Texture loading and sampling | `textured_quad` |
| Per-frame uniform buffer ring | `animated_uniform` |
| Push constants and input | `sierpinski` |
| UBO, push constants, depth, culling | `hiss` |
| Compute pass plus two graphics pipelines | `multiple_pipelines` |
| Headless render and PNG readback | `offscreen_png` |
| Sample an offscreen target on screen | `render_to_texture` |
| MSAA, MRT, and automatic resolve | `msaa_mrt_resolve` |

## Start an application

```cpp
#include <oxide/oxide.hpp>

oxide::Application app(800, 600, "My app");
auto &window = app.window();
auto &device = app.device();
auto &vulkan = app.context();
```

Pass a `WindowConfig` for window options and a `SwapchainConfig` for
presentation options:

```cpp
oxide::Application app(
    { .width       = 1280,
      .height      = 720,
      .title       = "My app",
      .resizable   = true,
      .transparent = false,
      .decorated   = true,
      .floating    = false },
    "My app",
    { .sampleCount = VK_SAMPLE_COUNT_4_BIT });
```

`SwapchainConfig` fields:

- `surfaceFormat`: `std::optional<VkSurfaceFormatKHR>`; set both `format` and
  `colorSpace`. Defaults to `B8G8R8A8_SRGB` with nonlinear sRGB.
- `presentMode`: `std::optional<VkPresentModeKHR>`; prefers `MAILBOX`, then
  `FIFO`. An unavailable request falls back to `FIFO`.
- `imageCount`: zero chooses `minImageCount + 1`, clamped to the surface limits.
- `imageUsage`: extra usage beyond color attachment, such as
  `VK_IMAGE_USAGE_TRANSFER_SRC_BIT` for screenshots. Unsupported usage falls
  back to color attachment only.
- `sampleCount`: window MSAA; unsupported values warn and fall back to 1.

Create the pieces individually when you need custom ownership. Order matters:
GLFW, then the window, then the device, then the presentation context.

```cpp
oxide::GLFWContext  glfw;
oxide::Window       window({ .width = 800, .height = 600, .title = "My app" });
oxide::VulkanDevice device(window.handle(), "My app");
oxide::VulkanContext vulkan(device, &window);
```

For compute-only or headless work, skip `Application` and create the device
without a window:

```cpp
oxide::VulkanDevice device("Headless");
```

Raw Vulkan handles remain available when you need them: `window.handle()`,
`device.device()`, `device.physicalDevice()`, `device.graphicsQueue()`,
`vulkan.renderPass()`, `pipeline.pipeline()`, and similar accessors. Call
`device.waitIdle()` to block until submitted work finishes.

## Render a frame

```cpp
while (!window.shouldClose()) {
    window.pollEvents();

    oxide::FrameScope frame = vulkan.beginFrameScope({ 0, 0, 0, 1 });
    if (!frame) continue;

    vulkan.setViewportAndScissor(frame.commandBuffer());
    // bind resources and draw
}
```

`FrameScope` ends the render pass and submits the frame when it goes out of
scope. It is inactive while the swapchain is being recreated, so `if (!frame)`
skips that iteration safely.

For compute work, offscreen passes, or custom ordering outside the window
render pass, use a raw `Frame`:

```cpp
const oxide::Frame frame = vulkan.beginFrame();
if (frame.commandBuffer == VK_NULL_HANDLE) continue;

// record compute or offscreen commands here

vulkan.beginRenderPass(frame, { 0.0f, 0.0f, 0.0f, 1.0f });
vulkan.setViewportAndScissor(frame.commandBuffer);
// draw
vulkan.endRenderPass(frame);

vulkan.endFrame(frame);
```

`Frame` exposes `commandBuffer`, `frameIndex` for per-frame resources, and
`imageIndex` for the acquired swapchain image.

## Create a pipeline

```cpp
auto config = oxide::Pipeline::defaultPipelineConfigInfo();
config.setVertexInput(input);
config.setPipelineLayout(oxide::BorrowedPipelineLayout(layout.handle()));

oxide::ShaderLibrary shaders(OXIDE_SHADER_DIR);
oxide::SwapchainPipeline pipeline(
    vulkan, device, shaders.graphics("program"), config);

pipeline.bind(command);
pipeline.pipeline().draw(command, 3);
```

Use `SwapchainPipeline` for window rendering: it uses the context render pass
and rebuilds automatically when a swapchain recreation replaces that render
pass. It also keeps its sample count matched to `vulkan.sampleCount()`. A plain
`Pipeline` used with the window must call
`config.setMultisampleCount(vulkan.sampleCount())` itself.

Use `oxide::Pipeline` for offscreen or other render passes that never change:

```cpp
config.setRenderPass(target.renderPass());
oxide::Pipeline pipeline(device, shaders.graphics("scene"), config);

pipeline.bind(command);
pipeline.draw(command, 3);
```

Shaders are named by their source stem:

```cpp
shaders.graphics("water");    // water.vert.spv + water.frag.spv
shaders.compute("particles"); // particles.comp.spv
```

Each example's CMake target compiles its `shaders/` directory and defines
`OXIDE_SHADER_DIR`; `OXIDE_ASSET_DIR` points at the repository assets.

### Configure pipeline state

```cpp
config.depthStencilInfo.depthTestEnable  = VK_TRUE;
config.depthStencilInfo.depthWriteEnable = VK_TRUE;
config.depthStencilInfo.depthCompareOp   = VK_COMPARE_OP_LESS;

config.rasterizationInfo.cullMode  = VK_CULL_MODE_BACK_BIT;
config.rasterizationInfo.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
config.inputAssemblyInfo.topology  = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;

config.colorBlendAttachment.blendEnable         = VK_TRUE;
config.colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
config.colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
config.colorBlendAttachment.colorBlendOp        = VK_BLEND_OP_ADD;
config.colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
config.colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
config.colorBlendAttachment.alphaBlendOp        = VK_BLEND_OP_ADD;
```

Defaults are triangle list, no culling, blending off, depth testing off, and
dynamic viewport/scissor. The window context always has a depth image; an
offscreen target has one only when `OffscreenConfig::depth` is true.

Set `config.setColorAttachmentCount(target.colorCount())` for MRT and
`config.setMultisampleCount(target.sampleCount())` for MSAA. After setting the
attachment count, each `config.colorBlendAttachments[i]` can be adjusted
independently.

## Vertex input and geometry

```cpp
struct Vertex {
    float position[2];
    float color[3];
};

oxide::VertexInputDescription input;
input.addBinding(0, sizeof(Vertex))
    .addAttribute(0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, position))
    .addAttribute(1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, color));
```

For instancing, add a second binding with `VK_VERTEX_INPUT_RATE_INSTANCE` and
pass the instance count to `draw()`. `VertexBuffer::bind(command, binding)`
selects the vertex binding index; the default is 0.

```cpp
oxide::VertexBuffer vertices(device, vertex_vector);
oxide::IndexBuffer indices(device, index_vector); // u16 or u32
oxide::Mesh mesh(device, vertex_vector, index_vector);

mesh.bind(command);   // binds the vertex and index buffers
mesh.draw(command);   // indexed draw of indexCount()
```

`Mesh::draw()` assumes the pipeline is already bound. To record the draw
yourself, bind the mesh buffers and call
`pipeline.drawIndexed(command, mesh.indexCount())`, or use the full draw
helpers:

```cpp
pipeline.draw(command, vertexCount, instanceCount, firstVertex, firstInstance);
pipeline.drawIndexed(command, indexCount, instanceCount, firstIndex,
                     vertexOffset, firstInstance);
```

The draw helpers default to one instance and the start of each buffer.

Vertex and index buffers upload through staging into device-local memory. For
host-visible uniform or storage data, use `Buffer` directly:

```cpp
oxide::Buffer data(device, bytes, size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
data.update(new_value);            // typed overload
data.update(pointer, byte_count);  // raw overload
data.read(out.data(), out.size()); // after the GPU work has completed
```

## Push constants

```cpp
struct PushData { float zoom; };
auto push = oxide::pushConstant<PushData>(VK_SHADER_STAGE_FRAGMENT_BIT);
oxide::PipelineLayout layout(device, { push.range() });
config.setPipelineLayout(oxide::BorrowedPipelineLayout(layout.handle()));

layout.pushConstant(frame.commandBuffer(), push, PushData{ 1.0f });
```

The `PushConstant` value keeps its range and shader stages together. For arrays
or custom blocks, use `pushConstantRange<T>()` with the raw overload
`layout.pushConstant(command, stages, data, offset)`. Combine descriptors and
push constants with
`PipelineLayout(device, { set.layout() }, { push.range() })`.

See `sierpinski` for input-driven push constants.

## Per-frame uniforms

```cpp
MyUniforms initial{};
oxide::UniformBufferRing uniforms(
    device, oxide::MAX_FRAMES_IN_FLIGHT, initial);
oxide::PipelineLayout layout(device, { uniforms.layout() }, {});

uniforms.update(frame.frameIndex(), current_data);
uniforms.bind(frame.commandBuffer(), layout.handle(), frame.frameIndex());
```

The ring keeps one buffer and descriptor set per frame slot, so updating a slot
cannot race work the GPU is still reading. `MAX_FRAMES_IN_FLIGHT` matches the
context's frame count. `uniforms.descriptorSet(frameIndex)` exposes the raw set
when binding manually. See `animated_uniform` and `hiss`.

## Textures and descriptors

```cpp
oxide::ImageTexture texture(device, OXIDE_ASSET_DIR "/smiley_0.png", {
    .format      = VK_FORMAT_R8G8B8A8_SRGB,
    .minFilter   = VK_FILTER_LINEAR,
    .magFilter   = VK_FILTER_LINEAR,
    .addressMode = VK_SAMPLER_ADDRESS_MODE_REPEAT,
    .anisotropy  = true,
});
oxide::TextureDescriptorSet image_set(device, texture);
oxide::PipelineLayout layout(device, { image_set.layout() }, {});

image_set.bind(frame.commandBuffer(), layout.handle());
```

The descriptor set uses a combined sampler at binding `0` by default. Its set
index is its position in the `PipelineLayout` (set `0` for the example above).
Pass a different binding or stage to the constructor when the shader interface
differs. See `textured_quad`.

For a static uniform buffer, `UBODescriptorSet` owns the layout, pool, and set:

```cpp
oxide::UniformBuffer camera(device, camera_data);
oxide::UBODescriptorSet camera_set(device, camera);
oxide::PipelineLayout layout(device, { camera_set.layout() }, {});
camera_set.bind(command, layout.handle());
```

For a custom interface, build the layout and pool explicitly:

```cpp
oxide::DescriptorSetLayout set_layout(device, {
    oxide::uniformBuffer(0, VK_SHADER_STAGE_VERTEX_BIT),
    oxide::combinedImageSampler(1, VK_SHADER_STAGE_FRAGMENT_BIT),
});
oxide::DescriptorPool pool(device, {
    { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1 },
    { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1 },
});
oxide::DescriptorSet set(pool, set_layout.handle());
oxide::PipelineLayout layout(device, { set_layout.handle() }, {});

// Fill the set with vkUpdateDescriptorSets, then bind it.
set.bind(command, layout.handle(), 0);
```

`DescriptorBinding` exposes `binding`, `type`, `stageFlags`, `descriptorCount`,
and `immutableSamplers`. Helpers are `combinedImageSampler`, `uniformBuffer`,
and `storageBuffer`. Bind multiple sets in order with `firstSet`. The
`TextureDescriptorSet` and `UBODescriptorSet` wrappers expose `layout()` and
`descriptorSet()` when their set is needed directly.
`layout.bindDescriptorSet(command, rawSet, firstSet)` binds a raw set through a
`PipelineLayout`.

## Compute

```cpp
oxide::ComputePipeline compute(
    device, shaders.compute("particles"),
    oxide::BorrowedPipelineLayout(layout.handle()));

compute.bind(command);
compute.dispatch(command, groupCountX, groupCountY, groupCountZ);
```

Dispatch outside a render pass. Record compute work between `beginFrame()` and
`beginRenderPass()`, or in a one-time command buffer for headless work. Omit the
layout when the shader needs no descriptors or push constants. See
`multiple_pipelines`.

## Offscreen rendering

```cpp
oxide::OffscreenTarget target(device, {
    .width        = 800,
    .height       = 600,
    .colorFormat  = VK_FORMAT_R8G8B8A8_SRGB,
    .colorFormats = {},   // non-empty selects MRT
    .imageUsage   = 0,    // extra usage beyond render/sample/readback
    .depth        = false,
    .filter       = VK_FILTER_LINEAR,
    .addressMode  = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
    .sampleCount  = VK_SAMPLE_COUNT_1_BIT,
});

oxide::PipelineConfigInfo config = oxide::Pipeline::defaultPipelineConfigInfo();
config.setRenderPass(target.renderPass());
oxide::Pipeline scene(device, shaders.graphics("scene"), config);

VkCommandBuffer command = device.beginSingleTimeCommands();
target.beginRenderPass(command);
target.setViewportAndScissor(command);
scene.bind(command);
scene.draw(command, 3);
target.endRenderPass(command);
device.endSingleTimeCommands(command);

target.savePng("frame.png");
```

The single clear-color overload applies the same value to every color
attachment. The color attachment ends in `SHADER_READ_ONLY_OPTIMAL`, so another
pass can sample it directly:

```cpp
oxide::TextureDescriptorSet image_set(
    device, target.colorView(), target.sampler());
oxide::PipelineLayout layout(device, { image_set.layout() }, {});
image_set.bind(command, layout.handle());
```

Useful target accessors:

```cpp
target.colorCount();        // number of color attachments
target.hasDepth();
target.isMultisampled();
target.sampleCount();
target.extent();
target.renderPass();
target.config();            // the resolved OffscreenConfig
target.colorFormat(0);      // format of one attachment

target.colorImage(0);       // resolved, sampleable image
target.colorView(0);
target.sampler(0);
target.resolveImage(0);     // explicit resolve target
target.resolveView(0);
target.attachmentImage(0);  // raw framebuffer attachment (multisampled)
target.attachmentView(0);
```

Use `readPixels(attachment)` for tightly packed RGBA8 bytes and
`savePng(path, attachment)` for files. Both default to attachment 0. Construct
`VulkanDevice` without a window for fully headless rendering. See
`offscreen_png` and `render_to_texture`.

## MSAA, MRT, and resolve

```cpp
oxide::OffscreenTarget target(device, {
    .width        = 1100,
    .height       = 700,
    .colorFormats = { VK_FORMAT_R8G8B8A8_SRGB, VK_FORMAT_R8G8B8A8_UNORM },
    .depth        = true,
    .sampleCount  = VK_SAMPLE_COUNT_4_BIT,
});

auto config = oxide::Pipeline::defaultPipelineConfigInfo();
config.setRenderPass(target.renderPass());
config.setColorAttachmentCount(target.colorCount());
config.setMultisampleCount(target.sampleCount());

// One clear color per attachment; both attachments resolve automatically.
target.beginRenderPass(command,
    { { 0.0f, 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f, 0.0f, 1.0f } });
```

`colorView(i)` and `sampler(i)` return the resolved, sampleable images.
`resolveView(i)` exposes the explicit resolve image, and `attachmentView(i)`
returns the multisampled attachment. Without MSAA these are all the same image.

An offscreen target throws when the requested sample count is unsupported.
Check a runtime-selected count first:

```cpp
if (device.supportsSampleCount(vulkan.swapchainFormat(),
                               VK_SAMPLE_COUNT_4_BIT,
                               VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)) {
    // safe to request 4x MSAA
}
```

Enable windowed MSAA by passing
`oxide::SwapchainConfig{ .sampleCount = VK_SAMPLE_COUNT_4_BIT }` to
`Application`. Unsupported window counts log a warning and fall back to 1.
See `msaa_mrt_resolve`.

## Window input and timing

```cpp
window.pollEvents();
if (window.closeRequested()) break; // also consumes Escape/Q

if (window.keyPressed(GLFW_KEY_SPACE)) { /* edge-triggered, consumes */ }
const bool held  = !window.keyReleased(GLFW_KEY_W);
const bool mouse = window.mouseButtonDown(GLFW_MOUSE_BUTTON_LEFT);

oxide::MousePosition cursor = window.cursorPosition();
oxide::MousePosition scroll = window.consumeScrollDelta();
const double seconds = window.time();

const int content_width  = window.width();            // screen coordinates
const int pixel_width    = window.framebufferWidth(); // drawable pixels

oxide::WindowPosition position = window.position();
window.setPosition(position);
```

`width()`/`height()` are content coordinates suitable for normalizing cursor
positions. `framebufferWidth()`/`framebufferHeight()` are drawable pixels and
can differ on HiDPI displays. Wayland does not expose global window positions;
check `hasGlobalPosition()` before relying on `position()`. For transparent
windows, set `WindowConfig::transparent` and use a clear color with alpha 0.

Frame-rate reporting:

```cpp
tools::FPSCounter fps(window.time());
// each frame
fps.update(window.time());
// at exit
fps.finish();
```

See `sierpinski`.

## Swapchain and resize

```cpp
const VkExtent2D extent   = vulkan.extent();
const VkFormat   format   = vulkan.swapchainFormat();
const VkFormat   depth    = vulkan.depthFormat();
const VkRenderPass pass   = vulkan.renderPass();
const VkSampleCountFlagBits samples = vulkan.sampleCount();
const VkPresentModeKHR present = vulkan.swapchainPresentMode();
const VkImageUsageFlags usage  = vulkan.swapchainImageUsage();

// In the frame loop:
const oxide::Frame frame = vulkan.beginFrame();
if (frame.commandBuffer == VK_NULL_HANDLE) continue; // swapchain recreating

const VkImage      image = vulkan.swapchainImages()[frame.imageIndex];
const VkImageView  view  = vulkan.swapchainImageViews()[frame.imageIndex];
const VkFramebuffer fb   = vulkan.framebuffer(frame.imageIndex);
```

`SwapchainPipeline` rebuilds itself automatically, including after a sample
count fallback. For plain `Pipeline` objects or custom render-pass resources,
register a callback:

```cpp
vulkan.addSwapchainRecreatedCallback(
    [](const oxide::SwapchainRecreationInfo &info) {
        if (!info.renderPassChanged) return;
        // rebuild objects created with the old render pass
    });
```

`SwapchainRecreationInfo` carries `renderPass`, `format`, `extent`,
`renderPassChanged`, and `generation`. `setSwapchainRecreatedCallback()`
replaces the callback; `addSwapchainRecreatedCallback()` appends one.
`swapchainGeneration()` changes after every successful recreation, so storing
it alongside long-lived objects helps detect stale pipelines. The context calls
`recoverSurface()` automatically after surface loss; it rebuilds the surface and
swapchain but does not invoke recreation callbacks, so rebuild render-pass
dependent resources yourself if you manage them directly.

## Diagnostics

```cpp
OXIDE_LOG_INFO << "loaded " << count << " meshes";
OXIDE_LOG_WARNING << "falling back to FIFO";

throw oxide::runtimeError("failed to create render pass");
throw oxide::invalidArgument("width must be non-zero");
throw oxide::outOfRange("attachment index out of range");

oxide::checkVk(result, "create buffer");        // throws with call site
oxide::logVkFailure(result, "destroy buffer");  // logs, does not throw
```

`OXIDE_LOG_*` writes severity and source location to stderr. Set `NO_COLOR=1`
to disable terminal colors. Build with `-DOXIDE_ENABLE_LOGGING=OFF` to discard
log output (arguments are still evaluated) and
`-DOXIDE_ENABLE_VALIDATION=OFF` to skip the validation layer. Exception
messages include the throw site. `vkResultName()` and `vkFailureMessage()`
format `VkResult` values.

## Ownership and cleanup

Declare `DeviceIdleGuard` after GPU resources:

```cpp
oxide::VertexBuffer vertices(device, data);
oxide::DeviceIdleGuard idle_guard(device);
```

`PipelineLayout` owns layouts it creates. `BorrowedPipelineLayout` tells a
pipeline to use an externally owned layout without destroying it. Vulkan
resources must not be destroyed while submitted work still references them.
`Application` owns the GLFW context, window, device, and presentation context,
so it must outlive every wrapper created from them. Resource wrappers are
non-copyable (most are move-only); declare them in dependency order and let
destruction run in reverse.
