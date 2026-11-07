# Oxide cheatsheet

The examples are the primary documentation. Start with `vulkan_triangle`, then
look at the example for the feature you need.

## Start an application

```cpp
#include <oxide/oxide.hpp>

oxide::Application app(800, 600, "My app");
auto &window = app.window();
auto &device = app.device();
auto &vulkan = app.context();
```

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

Use `beginFrame()`/`beginRenderPass()`/`endRenderPass()`/`endFrame()` when
compute or custom command ordering must happen outside the normal render pass.
See `multiple_pipelines`.

## Create a pipeline

```cpp
auto config = oxide::Pipeline::defaultPipelineConfigInfo();
config.setVertexInput(input);

oxide::ShaderLibrary shaders(OXIDE_SHADER_DIR);
oxide::SwapchainPipeline pipeline(
    vulkan, device, shaders.graphics("program"), config);
```

Shaders are named by their source stem:

```cpp
shaders.graphics("water");   // water.vert.spv + water.frag.spv
shaders.compute("particles"); // particles.comp.spv
```

## Vertex and index data

```cpp
oxide::VertexBuffer vertices(device, vertex_vector);
oxide::IndexBuffer indices(device, index_vector);
oxide::Mesh mesh(device, vertex_vector, index_vector);

mesh.draw(frame.commandBuffer());
```

Use `Mesh` for the common indexed draw. Use separate buffers when the command
sequence or vertex layout needs more control.

## Push constants

```cpp
struct PushData { float zoom; };
auto push = oxide::pushConstant<PushData>(VK_SHADER_STAGE_FRAGMENT_BIT);
oxide::PipelineLayout layout(device, { push.range() });
config.setPipelineLayout(oxide::BorrowedPipelineLayout(layout.handle()));

layout.pushConstant(frame.commandBuffer(), push, PushData{ 1.0f });
```

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

See `animated_uniform` and `hiss`.

## Textures

```cpp
oxide::ImageTexture texture(device, OXIDE_ASSET_DIR "/smiley_0.png");
oxide::TextureDescriptorSet image_set(device, texture);
oxide::PipelineLayout layout(device, { image.layout() }, {});

image_set.bind(frame.commandBuffer(), layout.handle());
```

The shader uses a combined sampler at set `0`, binding `0` by default. See
`textured_quad`.

## Offscreen rendering

```cpp
oxide::OffscreenTarget target(device, {
    .width  = 800,
    .height = 600,
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

The color attachment ends in `SHADER_READ_ONLY_OPTIMAL`, so another pass can
sample it directly:

```cpp
oxide::TextureDescriptorSet image_set(
    device, target.colorView(), target.sampler());
oxide::PipelineLayout layout(device, { image_set.layout() }, {});
image_set.bind(command, layout.handle());
```

Use `readPixels()` to get tightly packed RGBA8 bytes instead of writing a file.
Construct `VulkanDevice` without a window for headless rendering. See
`offscreen_png` and `render_to_texture`.

## Input and timing

```cpp
window.pollEvents();
if (window.keyPressed(GLFW_KEY_ESCAPE)) {
    glfwSetWindowShouldClose(window.handle(), GLFW_TRUE);
}

double seconds = window.time();
oxide::MousePosition scroll = window.consumeScrollDelta();
```

## Ownership and cleanup

Declare `DeviceIdleGuard` after GPU resources:

```cpp
oxide::VertexBuffer vertices(device, data);
oxide::DeviceIdleGuard idle_guard(device);
```

`PipelineLayout` owns layouts it creates. `BorrowedPipelineLayout` tells a
pipeline to use an externally owned layout without destroying it. Vulkan
resources must not be destroyed while submitted work still references them.
