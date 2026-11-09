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
| Reflection and specialization constants | `sierpinski`, `hiss`, `multiple_pipelines` |

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
oxide::ShaderLibrary shaders(OXIDE_SHADER_DIR);
const oxide::GraphicsShaderFiles files = shaders.graphics("program");
const oxide::ShaderReflection reflection =
    oxide::ShaderReflection::fromGraphics(files);
oxide::PipelineLayout layout(device, reflection);

auto config = oxide::Pipeline::defaultPipelineConfigInfo();
config.setVertexInput(input);
config.setPipelineLayout(oxide::BorrowedPipelineLayout(layout.handle()));

oxide::SwapchainPipeline pipeline(vulkan, device, files, config);

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

## Shader reflection and specialization

`ShaderReflection` reads the SPIR-V that the pipeline will use and exposes the
interface declared by the shaders. Reflection is the recommended way to build a
`PipelineLayout`, because the descriptor sets and push-constant ranges cannot
drift away from the shader source.

```cpp
oxide::ShaderLibrary shaders(OXIDE_SHADER_DIR);
const oxide::GraphicsShaderFiles files = shaders.graphics("scene");

// Vertex and fragment interfaces are merged, including shared bindings.
const oxide::ShaderReflection reflection =
    oxide::ShaderReflection::fromGraphics(files);
oxide::PipelineLayout layout(device, reflection);

// Use the reflected layouts when allocating descriptor sets.
oxide::TextureDescriptorSet texture_set(
    device, texture, layout.descriptorSetLayout(0));

// Use the reflected push range when recording.
const VkPushConstantRange push = reflection.pushConstantRanges().front();
layout.pushConstant(command, push.stageFlags, push_data, push.offset);
```

Compute shaders use `ShaderReflection::fromCompute(shaders.compute("name"))`.
`fromFile(path)` and `fromFiles({paths...})` cover custom combinations.

Reflection exposes:

```cpp
reflection.descriptors();               // ReflectedDescriptor entries
reflection.descriptorBindings(set);     // VkDescriptorSetLayoutBinding list
reflection.descriptorSetCount();        // highest set + 1, gaps included
reflection.pushConstants();             // ReflectedPushConstant entries
reflection.pushConstantRanges();        // ready for PipelineLayout
reflection.specializationConstants();   // ReflectedSpecializationConstant list

reflection.findDescriptor("name");
reflection.findSpecializationConstant("name"); // nullptr when absent
```

Overlapping push-constant ranges are merged and their stage flags are combined.
Descriptor bindings used by several stages are merged the same way. Bindings are
sorted by set and binding, so `descriptorBindings(set)` can be used directly.
Empty descriptor set layouts are created for unused set indices; binding such a
set is not needed, but the pipeline layout still declares the gap.

`TextureDescriptorSet`, `UBODescriptorSet`, and `UniformBufferRing` accept a
`VkDescriptorSetLayout` as an alternative to creating their own. Pass
`layout.descriptorSetLayout(set)` so the wrapper allocates from the reflected
layout:

```cpp
oxide::TextureDescriptorSet image_set(
    device, target.colorView(0), target.sampler(0),
    layout.descriptorSetLayout(0));

oxide::UniformBufferRing uniforms(
    device, oxide::MAX_FRAMES_IN_FLIGHT, initial_data,
    layout.descriptorSetLayout(0));

oxide::UBODescriptorSet camera_set(
    device, camera_buffer, layout.descriptorSetLayout(0));
```

Declare the reflected `PipelineLayout` before the wrappers that use its layouts,
and keep it alive until those wrappers are destroyed.

### Specialization constants

GLSL declares a specialization constant with `layout(constant_id = N)`:

```glsl
layout(constant_id = 0) const int FBM_OCTAVES = 5;
```

Reflection reports its name, ID, size, and stage. Override it on the pipeline
config before creating the pipeline:

```cpp
oxide::PipelineConfigInfo config = oxide::Pipeline::defaultPipelineConfigInfo();
if (const auto *octaves = reflection.findSpecializationConstant("FBM_OCTAVES")) {
    config.fragmentSpecialization.set(*octaves, 6);
}
// Also available: config.vertexSpecialization.
```

`SpecializationConstants::set()` copies the value, so the source variable can go
out of scope. Values must be trivially copyable. A stage with no overrides gets
no `VkSpecializationInfo`, leaving the shader defaults active. Use the same
object for a compute pipeline:

```cpp
oxide::SpecializationConstants constants;
constants.set(0, 8);
oxide::ComputePipeline pipeline(
    device, shaders.compute("particles"),
    oxide::BorrowedPipelineLayout(layout.handle()), constants);
```

The shader default stays active when a named constant is not found, so guard
`findSpecializationConstant()` and only override values the shader declares.
See `sierpinski` (`ITERATION_LIMIT`), `hiss` (`FBM_OCTAVES`), and
`multiple_pipelines` (`OVERLAY_SCALE`, vertex stage).

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

Reflection usually supplies the range. Declare the matching block in GLSL and
let the shader tell the CPU its size, offset, and stages:

```cpp
struct PushData { float zoom; };

const VkPushConstantRange range = reflection.pushConstantRanges().front();
oxide::PipelineLayout layout(device, reflection);

layout.pushConstant(
    frame.commandBuffer(), range.stageFlags, PushData{ 1.0f }, range.offset);
```

`ShaderReflection` merges identical ranges used by several stages into one range
with the union of their stage bits. If a program declares its layout by hand,
`PushConstant` keeps a typed range and its stages together:

```cpp
auto push = oxide::pushConstant<PushData>(VK_SHADER_STAGE_FRAGMENT_BIT);
oxide::PipelineLayout layout(device, { push.range() });
config.setPipelineLayout(oxide::BorrowedPipelineLayout(layout.handle()));

layout.pushConstant(frame.commandBuffer(), push, PushData{ 1.0f });
```

For arrays or custom blocks, use `pushConstantRange<T>()` with the raw overload
`layout.pushConstant(command, stages, data, offset)`. Combine descriptors and
push constants with
`PipelineLayout(device, { set.layout() }, { push.range() })`.

See `sierpinski` for input-driven push constants and `hiss` for a reflected
range shared by the vertex and fragment stages.

## Per-frame uniforms

```cpp
MyUniforms initial{};
oxide::UniformBufferRing uniforms(
    device, oxide::MAX_FRAMES_IN_FLIGHT, initial,
    layout.descriptorSetLayout(0)); // reflected layout

uniforms.update(frame.frameIndex(), current_data);
uniforms.bind(frame.commandBuffer(), layout.handle(), frame.frameIndex());
```

The ring keeps one buffer and descriptor set per frame slot, so updating a slot
cannot race work the GPU is still reading. `MAX_FRAMES_IN_FLIGHT` matches the
context's frame count. `uniforms.descriptorSet(frameIndex)` exposes the raw set
when binding manually. Pass `layout.descriptorSetLayout(0)` to allocate from a
reflected layout, or omit it to let the ring create a binding-0 layout. See
`animated_uniform`, `hiss`, and `render_to_texture`.

## Textures and descriptors

```cpp
oxide::ImageTexture texture(device, OXIDE_ASSET_DIR "/smiley_0.png", {
    .format      = VK_FORMAT_R8G8B8A8_SRGB,
    .minFilter   = VK_FILTER_LINEAR,
    .magFilter   = VK_FILTER_LINEAR,
    .addressMode = VK_SAMPLER_ADDRESS_MODE_REPEAT,
    .anisotropy  = true,
});
oxide::TextureDescriptorSet image_set(
    device, texture, layout.descriptorSetLayout(0)); // reflected layout

image_set.bind(frame.commandBuffer(), layout.handle());
```

The constructor without a `VkDescriptorSetLayout` creates its own layout with a
combined sampler at binding `0`. Pass a different binding or stage in that form
when the shader interface differs. A reflected layout supplies the layout
object, but the binding and set index remain explicit: pass the reflected
binding to the constructor (default 0) and the set index as `firstSet` when
binding. For a sampler at binding 1 in set 2:

```cpp
oxide::TextureDescriptorSet image_set(
    device, texture, layout.descriptorSetLayout(2), 1);
image_set.bind(frame.commandBuffer(), layout.handle(), 2);
```

See `textured_quad`, `render_to_texture`, and `msaa_mrt_resolve`.

For a static uniform buffer, `UBODescriptorSet` also accepts a reflected layout:

```cpp
oxide::UniformBuffer camera(device, camera_data);
oxide::UBODescriptorSet camera_set(
    device, camera, layout.descriptorSetLayout(0));
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
if (window.keyReleased(GLFW_KEY_SPACE)) { /* edge-triggered, consumes */ }
const bool held  = window.keyDown(GLFW_KEY_W);
const bool mouse = window.mouseButtonDown(GLFW_MOUSE_BUTTON_LEFT);

oxide::MousePosition cursor = window.cursorPosition();
oxide::MousePosition scroll = window.consumeScrollDelta();
const double seconds = window.time();

const int content_width  = window.width();            // screen coordinates
const int pixel_width    = window.framebufferWidth(); // drawable pixels

oxide::WindowPosition position = window.position();
window.setPosition(position);
```

`keyPressed()` and `keyReleased()` report edge events and consume them on read;
`keyDown()` reports whether the key is currently held. `width()`/`height()` are
content coordinates suitable for normalizing cursor positions.
`framebufferWidth()`/`framebufferHeight()` are drawable pixels and can differ on
HiDPI displays. Wayland does not expose global window positions; check
`hasGlobalPosition()` before relying on `position()`. For transparent windows,
set `WindowConfig::transparent` and use a clear color with alpha 0.

Frame-rate reporting:

```cpp
oxide::FPSCounter fps(window.time());
// each frame
fps.update(window.time());
// at exit
fps.finish();
```

FPS output updates one terminal line when stdout is a TTY and `NO_COLOR` is
unset; otherwise it prints one line per report. See `sierpinski`.

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
replaces the callback. `addSwapchainRecreatedCallback()` appends one and returns
an id; pass that id to `removeSwapchainRecreatedCallback()` to unregister it.
`SwapchainPipeline` does this automatically when it is destroyed.
`swapchainGeneration()` changes after every successful recreation, so storing it
alongside long-lived objects helps detect stale pipelines. The context also
invokes the callbacks after `recoverSurface()` rebuilds a lost surface, with
`renderPassChanged` set to true, so render-pass-dependent resources are rebuilt
after surface loss as well.

```cpp
const auto callback_id = vulkan.addSwapchainRecreatedCallback(
    [](const oxide::SwapchainRecreationInfo &info) { /* rebuild */ });
// Later, before the context and captured objects are destroyed:
vulkan.removeSwapchainRecreatedCallback(callback_id);
```

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
