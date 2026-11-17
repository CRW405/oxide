# Oxide Learning Guide

This document is a practical textbook-style guide for learning both Vulkan and this codebase. It is written for someone who is new to C++ and graphics programming, but wants to understand not just the API surface, but the underlying ideas and the patterns used in Oxide.

The goal is not to memorize every Vulkan function. The goal is to understand:

- what the engine is doing
- why it does it in that order
- what lifetime and synchronization rules matter
- how the Oxide library wraps those concepts

This is meant to be read alongside:

- `README.md`
- `NOTES.md`
- `src/vulkan/**`
- `include/oxide/**`
- the example programs in `examples/`

---

## 1. What this project is

Oxide is a small Vulkan helper library. It is not a full game engine.

It does the setup work that most Vulkan applications need:

- GLFW window creation
- Vulkan instance and device creation
- queue selection
- swapchain creation
- render pass creation
- depth and MSAA handling
- buffer and image creation
- shader loading and pipeline creation
- asset loading for OBJ and glTF

The README says this clearly, and the code follows that scope.

This is a good project for learning because it exposes the important Vulkan concepts without hiding them completely.

---

## 2. How to think about Vulkan

Vulkan is a low-level graphics API. It gives you a lot of control, but that means you must manage a lot of details yourself.

Compared to OpenGL, Vulkan forces you to make explicit decisions about:

- which device to use
- which queues to use
- how resources are allocated
- how memory is managed
- how images are transitioned between layouts
- how commands are synchronized
- how render passes and framebuffers are created
- how pipelines are built

In a beginner-friendly way, think of Vulkan as:

- the hardware is explicit
- the driver is less forgiving
- the application owns more responsibility
- CPU/GPU synchronization is a first-class concept

This is why the project has explicit lifetimes and cleanup stages.

---

## 3. The C++ ideas you should learn from this project

### RAII

RAII stands for Resource Acquisition Is Initialization.

In C++, a resource should be acquired in a constructor and released in a destructor.

Oxide uses this pattern heavily:

- `Window` owns a GLFW window
- `VulkanDevice` owns the Vulkan instance and device
- `VulkanContext` owns swapchain and render-pass state
- `Application` owns the whole stack

This is a very important pattern to learn because it avoids raw manual allocation and cleanup.

Example:

```cpp
class Window {
  public:
    Window(const WindowConfig &config);
    ~Window();

  private:
    GLFWwindow *window_ = nullptr;
};
```

The destructor is where cleanup happens.

### The rule: resources are not magically safe

Just because something is a class does not mean the GPU is done with it.

A Vulkan image or buffer may still be used by a command buffer that has not yet completed execution.

That is why Oxide has guard objects like:

```cpp
class DeviceIdleGuard {
  public:
    explicit DeviceIdleGuard(const VulkanDevice &device) noexcept : device_(device) {}
    ~DeviceIdleGuard() noexcept { device_.waitIdleNoThrow(); }
};
```

This is not a fancy abstraction. It is the correct pattern for Vulkan resource lifetime management.

---

## 4. The main Oxide architecture

The library is structured approximately like this:

- `include/oxide/glfw/` — GLFW wrapper
- `include/oxide/vulkan/device/` — Vulkan instance + device setup
- `include/oxide/vulkan/frame/` — swapchain, render pass, frame loop, render scopes
- `include/oxide/vulkan/pipeline/` — shaders, reflection, pipeline creation
- `include/oxide/vulkan/resources/` — buffers, textures, descriptor sets
- `include/oxide/assets/` — OBJ and glTF loading

The core ownership flow is roughly:

```cpp
Application app({ width, height, "example" });

// app owns:
// - GLFWContext
// - Window
// - VulkanDevice
// - VulkanContext
```

This makes the app lifecycle easy to reason about.

---

## 5. The Application entry point

The central startup object is `Application`:

File: `include/oxide/vulkan/application.hpp`

```cpp
class Application {
  public:
    Application(
        const WindowConfig    &window_config = {},
        const char            *app_name = "Oxide",
        const SwapchainConfig &swapchain_config = {});

    [[nodiscard]] Window           &window() noexcept { return window_; }
    [[nodiscard]] VulkanDevice     &device() noexcept { return device_; }
    [[nodiscard]] ResourceRegistry &resources() noexcept { return device_.resources(); }
    [[nodiscard]] VulkanContext    &context() noexcept { return context_; }

  private:
    GLFWContext   glfw_;
    Window        window_;
    VulkanDevice  device_;
    VulkanContext context_;
};
```

This is a good beginner example because it shows the dependency chain clearly:

- `GLFWContext` initializes GLFW
- `Window` creates the OS window
- `VulkanDevice` creates the Vulkan instance/device
- `VulkanContext` creates the swapchain and frame resources

This is the “stack” your program depends on.

---

## 6. Initial Vulkan setup: device creation

The central Vulkan bootstrap is in `VulkanDevice`.

Source: `src/vulkan/device/vulkan_device.cpp`

The setup flow is:

1. create Vulkan instance
2. create a surface from the GLFW window
3. select a physical device
4. choose queue families
5. create logical device
6. create command pool and pipeline cache

A simplified version looks like this:

```cpp
VulkanDevice::VulkanDevice(GLFWwindow *main_window, std::string app_name)
    : app_name_(std::move(app_name)) {
    if (main_window == nullptr) {
        throw invalidArgument("VulkanDevice requires a valid window");
    }

    createInstance(app_name_);

    VkSurfaceKHR selection_surface = VK_NULL_HANDLE;
    const VkResult surface_result =
        glfwCreateWindowSurface(instance_, main_window, nullptr, &selection_surface);

    if (surface_result != VK_SUCCESS) {
        throw vkError("create Vulkan surface for device selection", surface_result);
    }

    try {
        selectPhysicalDevice(selection_surface);
        createLogicalDevice(selection_surface);
        createPipelineCache();
        resources_.setDevice(device_);
    } catch (...) {
        // cleanup path here
        throw;
    }
}
```

Why this is important:

- Vulkan requires instance + device + queue setup before rendering
- a real surface is needed to present to window
- cleanup is important on failure

This is exactly the kind of code that beginners usually hide behind a large engine abstraction.

---

## 7. Queue families and why they matter

The code defines a struct for queues:

```cpp
struct QueueFamilies {
    std::uint32_t graphics    = 0;
    std::uint32_t present     = 0;
    bool          hasGraphics = false;
    bool          hasPresent  = false;
};
```

The method `findQueueFamilies()` checks which queue families support:

- graphics operations
- presentation to the surface

This is crucial because not every GPU or queue family supports both.

A beginner often thinks:

> “If the GPU supports graphics, it can present.”

That is false. Many systems require a separate present queue or separate queue family.

Example conceptually:

```cpp
if (!queues.hasGraphics || !queues.hasPresent) {
    throw runtimeError("Vulkan device cannot present to this window surface");
}
```

This is why queue family selection is one of the first “real Vulkan” concepts you need to understand.

---

## 8. The validation layer and debugging

In `src/vulkan/device/vulkan_device.cpp`, the project enables the Vulkan validation layer by default unless explicitly disabled via CMake.

This is a very good learning feature because debugging Vulkan without validation is painful.

Relevant parts:

```cpp
constexpr bool        enable_validation   = OXIDE_ENABLE_VALIDATION != 0;
constexpr const char *validation_layer    = "VK_LAYER_KHRONOS_validation";
```

And:

```cpp
if (validation_enabled && !validationAvailable()) {
    OXIDE_LOG_WARNING << "Vulkan validation layer 'VK_LAYER_KHRONOS_validation' is unavailable; continuing without validation";
    validation_enabled = false;
}
```

The code also sets up a Vulkan debug messenger:

```cpp
const VkDebugUtilsMessengerCreateInfoEXT info = debugMessengerInfo();
checkVk(create_messenger(instance_, &info, nullptr, &debug_messenger_),
        "create Vulkan debug messenger");
```

This is a very good teaching example because it shows:

- validation is not optional in development
- Vulkan debug messages are essential for understanding failures
- cleanup is part of the setup path

---

## 9. Swapchain and frame lifecycle

The render loop is managed by `VulkanContext`.

Key responsibilities:

- create a surface
- create/query swapchain support
- choose a surface format
- choose present mode
- choose image count
- create image views
- create render pass
- create framebuffers
- create command resources
- recreate on resize

The header shows the major API:

```cpp
[[nodiscard]] Frame      beginFrame();
[[nodiscard]] FrameScope beginFrameScope(std::array<float, 4> clearColor);
void                     endFrame(const Frame &frame);
void                     beginRenderPass(const Frame &frame, std::array<float, 4> clearColor);
void                     endRenderPass(const Frame &frame);
void                     recreateSwapchain();
```

This is the central part of the engine loop.

A simplified render cycle looks like:

```cpp
while (!window.shouldClose()) {
    window.pollEvents();

    auto frame = context.beginFrame();
    if (frame.commandBuffer != VK_NULL_HANDLE) {
        context.beginRenderPass(frame, { 0.1f, 0.1f, 0.2f, 1.0f });

        // record draw calls

        context.endRenderPass(frame);
        context.endFrame(frame);
    }
}
```

This is what a Vulkan app really looks like at a high level.

---

## 10. Why swapchain recreation is so important

The code in `recreateSwapchain()` is a high-value teaching section.

It does this:

- waits for idle
- destroys the old swapchain resources
- creates new swapchain
- creates new depth resources
- creates new color resources if MSAA is in use
- rebuilds render pass if format/sample count changed
- rebuilds framebuffers
- rebuilds semaphores
- notifies listeners

This matters because the surface and swapchain are not a static thing:

- window resizes
- surface capabilities may change
- presentation format may change
- multisampling settings may require different attachments

That is why Vulkan apps often rebuild many dependent structures after a resize.

---

## 11. Render pass architecture: the real Vulkan challenge

This is one of the most important educational sections of the project.

In `createRenderPass()` the project creates:

- main color attachment
- optional resolve attachment in MSAA case
- depth attachment
- a graphics subpass
- dependencies for color and depth

The relevant pattern is:

```cpp
VkAttachmentDescription color_attachment{};
color_attachment.format         = swapchain_format_;
color_attachment.samples        = sample_count_;
color_attachment.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
color_attachment.storeOp        = multisampled ? VK_ATTACHMENT_STORE_OP_DONT_CARE
                                               : VK_ATTACHMENT_STORE_OP_STORE;
color_attachment.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
color_attachment.finalLayout    = multisampled ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
                                               : VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
```

And if MSAA is enabled:

```cpp
VkAttachmentDescription resolve_attachment{};
resolve_attachment.format         = swapchain_format_;
resolve_attachment.samples        = VK_SAMPLE_COUNT_1_BIT;
resolve_attachment.loadOp         = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
resolve_attachment.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
resolve_attachment.finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
```

This is a classic Vulkan pattern:

- render to a multisampled target
- resolve into a single-sample target for presentation

This is exactly the kind of thing beginners struggle with because it is both subtle and critical.

---

## 12. Resource ownership and the registry

The class `ResourceRegistry` is a great teaching point.

File: `include/oxide/vulkan/core/resource_registry.hpp`

It owns a per-device registry of many Vulkan resources:

- buffers
- images
- shaders
- pipelines
- pipeline layouts
- render passes
- framebuffers
- descriptor layouts
- descriptor pools

The registry is useful because Vulkan objects are often created in wrappers like `Buffer`, `ImageTexture`, `Pipeline`, etc., but the actual destruction is centralized.

This avoids forgetting to destroy something at the right time.

There is also a nice pattern for shader deduplication:

```cpp
[[nodiscard]] ShaderHandle registerShader(ShaderResource resource) {
    if (!resource.key.empty()) {
        auto existing = shader_keys_.find(resource.key);
        if (existing != shader_keys_.end()) {
            auto *shader = shaders_.get(existing->second);
            if (shader != nullptr) {
                ++shader->references;
                return existing->second;
            }
        }
    }
    // ... create if needed
}
```

This shows a good use of resource reuse in a graphics engine.

---

## 13. ResourcePool and stale handles

The file `include/oxide/vulkan/core/resource_pool.hpp` is an excellent C++ teaching example.

It uses a slot-based pool with generation tracking. The idea is:

- resources live in slots
- each slot has a generation
- stale handles become invalid because generation numbers no longer match

Example:

```cpp
static constexpr std::uint32_t makeValue(
    std::uint32_t index,
    std::uint16_t generation) noexcept {
    return (static_cast<std::uint32_t>(generation) << index_bits) | index;
}
```

This is a classic safe-handle pattern.

Why it matters:

- avoid invalid pointer use
- avoid use-after-free bugs
- avoid stale references

This is a strong C++ pattern to learn.

---

## 14. The memory allocator

The allocator logic in `src/vulkan/device/vulkan_memory.cpp` is also a very good educational example.

It manages `VkDeviceMemory` blocks and keeps free ranges.

Key concept:

- Vulkan memory is not like heap memory
- memory is allocated in blocks by type
- the code aligns allocations and tracks free ranges
- it reuses memory blocks for efficiency

A simplified snippet:

```cpp
const std::uint32_t memory_type = findMemoryType(requirements.memoryTypeBits, properties);
const VkDeviceSize alignment = requirements.alignment == 0 ? 1 : requirements.alignment;

for (auto &block : memory_blocks_) {
    if (block.type != memory_type) {
        continue;
    }

    for (auto range = block.free_ranges.begin(); range != block.free_ranges.end(); ++range) {
        const VkDeviceSize aligned_offset = (range->offset + alignment - 1) & ~(alignment - 1);
        if (padding + requirements.size > range->size) {
            continue;
        }
        // allocate here
    }
}
```

This teaches a key lesson:

- memory allocation in Vulkan is not automatic
- you must allocate and manage memory intelligently
- alignment and block reuse matter

This is not beginner-level trivial C++; it is exactly real graphics-engine work.

---

## 15. Common beginner mistakes (and how this project handles them)

### Mistake 1: forgetting to wait for GPU idle before destructor cleanup
This project explicitly uses `DeviceIdleGuard` and `waitIdleNoThrow`.

### Mistake 2: assuming a single render pass works forever
`VulkanContext::recreateSwapchain()` rebuilds render passes as needed.

### Mistake 3: not checking queue families
`findQueueFamilies()` exists to prevent this mistake.

### Mistake 4: mixing up image layouts
The project transitions layouts carefully for textures and rendering targets.

### Mistake 5: ignoring the relationship between framebuffer size and swapchain
The project explicitly listens for resize callbacks and recreates the swapchain.

### Mistake 6: destroying static global callback mappings too late
`Window::release()` removes callbacks before destroying the window.

### Mistake 7: trying to hide Vulkan complexity behind too much magic
The project keeps Vulkan objects visible and explicit, which is educational and honest.

---

## 16. Typical render loop pattern in Oxide

This is the pattern you’ll see in the examples.

```cpp
oxide::Application app({ 1280, 720, "Example" });

// create resources
// create pipeline/layout/shaders
// create buffers/textures

while (!app.window().shouldClose()) {
    app.window().pollEvents();

    auto scope = app.context().beginFrameScope({ 0.02f, 0.03f, 0.05f, 1.0f });
    if (scope) {
        app.context().setViewportAndScissor(scope.commandBuffer());

        // bind pipeline
        // bind vertex/index buffers
        // issue draw calls
    }
}
```

This is the rough shape of any Vulkan app using Oxide.

The important point is not exactly which functions are called; the important point is the pattern:

- create app and resources
- begin frame
- render to swapchain
- end frame
- keep resize handling in mind

---

## 17. How to study this codebase well

Read in this order:

1. `README.md`
2. `include/oxide/vulkan/application.hpp`
3. `include/oxide/vulkan/device/vulkan_device.hpp`
4. `src/vulkan/device/vulkan_device.cpp`
5. `include/oxide/vulkan/frame/vulkan_context.hpp`
6. `src/vulkan/frame/vulkan_context.cpp`
7. `include/oxide/vulkan/core/resource_registry.hpp`
8. `include/oxide/vulkan/core/resource_pool.hpp`
9. one example like `examples/vulkan_triangle`
10. one more advanced example like `examples/render_to_texture` or `examples/msaa_mrt_resolve`

Ask these questions while reading:

- what does this object own?
- what does it depend on?
- what is the destructor doing?
- what must happen before destruction?
- what resources change on resize?
- what synchronization is required?
- what kind of Vulkan object is being created here?

---

## 18. Vulkan concepts explained in plain English

This is the most important section for a beginner.

### 18.1. Instance

The Vulkan instance is the top-level connection to the Vulkan API.

It is like a global context for the application.

In Oxide, this is created in `VulkanDevice::createInstance()`.

Why it matters:

- it owns global Vulkan state
- it is used to enumerate devices and extensions
- it is what validation layers are attached to

### 18.2. Physical device

A physical device is the GPU itself.

It is not the runtime device used for rendering; it is the actual hardware object the driver exposes.

Oxide selects a physical device by scoring candidates and checking:

- graphics support
- present support
- swapchain support
- features like anisotropy

### 18.3. Logical device

A logical device is the handle you use to create resources and submit work.

This is the object you actually use in code after selecting a physical device.

It is where you create:

- command pools
- buffers
- images
- shader modules
- pipelines

### 18.4. Queue families

A queue family is a group of queues with the same capabilities.

Typical examples:

- graphics queue
- present queue
- transfer queue
- compute queue

The important point is:

- a queue family can do some things but not others
- your app must choose the right family for the job

This is exactly why `findQueueFamilies()` is in the project.

### 18.5. Swapchain

The swapchain is what turns the final frame into something the window can display.

It owns the images that are presented to the screen.

In practice, this includes:

- choosing a surface format
- choosing present mode
- managing image count
- handling resize

This is the bridge between rendering and the OS window.

### 18.6. Render pass

A render pass defines what render operations happen and what attachments are used.

It describes:

- which images are used as attachments
- what loads/stores happen
- what layout transitions occur
- which subpasses exist

This is why render pass creation is so central in Vulkan.

### 18.7. Framebuffer

A framebuffer ties a render pass to actual image views.

Conceptually:

- render pass = description of rendering
- framebuffer = actual images the render pass renders into

### 18.8. Pipeline

A pipeline bundles the shader stages and fixed-function state.

It includes:

- vertex input state
- rasterization settings
- blend state
- depth/stencil state
- shader modules
- pipeline layout

This is what lets Vulkan render a specific object or effect.

### 18.9. Command buffer

A command buffer records GPU work.

Think of it as a list of instructions for the GPU.

The sequence is often:

- begin command buffer
- bind pipelines
- bind buffers
- issue draw calls
- end command buffer
- submit to queue

### 18.10. Synchronization

This is the most important thing to learn in Vulkan.

The GPU runs commands asynchronously relative to the CPU.

So you need synchronization for:

- waiting for image availability
- waiting for render completion
- making sure a resource is not still in use when destroyed

This is what semaphores and fences do.

### 18.11. Memory and buffers

Buffers and images are stored in device memory.

This memory is not automatically managed like STL memory.

This is why Oxide has a custom memory allocator and a `DeviceMemoryAllocation` structure.

### 18.12. Image layouts

A Vulkan image can have different layouts depending on what it is being used for.

Examples:

- `VK_IMAGE_LAYOUT_UNDEFINED`
- `VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL`
- `VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL`
- `VK_IMAGE_LAYOUT_PRESENT_SRC_KHR`
- `VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL`

This is one of the most confusing Vulkan topics for beginners, but it is critical.

---

## 19. Oxide file-by-file walkthrough

Here is the practical order to read the code if you want to learn the project itself.

### 19.1. `include/oxide/glfw/glfw_oxide.hpp`

This gives the window wrapper around GLFW.

What to learn:

- how OS window lifetime is managed
- how input events are captured
- how window state is exposed to the rest of the engine

### 19.2. `src/glfw/glfw_oxide.cpp`

This is a good file to read to understand how the engine manages the native OS window and callbacks.

Key patterns:

- callback registration
- callback removal before destruction
- ownership of the `GLFWwindow *`
- `Window` movement semantics

### 19.3. `include/oxide/vulkan/application.hpp`

This is the “composition root” of the library.

It shows how the engine composes the pieces:

- `GLFWContext`
- `Window`
- `VulkanDevice`
- `VulkanContext`

This is a clean example of top-level engine assembly.

### 19.4. `src/vulkan/device/vulkan_device.cpp`

This is the heart of the Vulkan bootstrap.

Important sections:

- `createInstance()`
- `setupDebugMessenger()`
- `findQueueFamilies()`
- `selectPhysicalDevice()`
- `createLogicalDevice()`

This file teaches the real setup order of a Vulkan app.

### 19.5. `src/vulkan/device/vulkan_memory.cpp`

This is where memory management is handled.

Learn:

- memory pools
- block allocation
- alignment
- free-range tracking

This is a very important part of real graphics engine design.

### 19.6. `include/oxide/vulkan/frame/vulkan_context.hpp`

This file defines the swapchain and frame lifecycle.

It introduces:

- `SwapchainConfig`
- `VulkanContext`
- `FrameScope`
- swapchain recreation callbacks

This is a big conceptual file that you want to read carefully.

### 19.7. `src/vulkan/frame/vulkan_context.cpp`

This is the most important practical file after device setup.

It shows:

- surface creation
- swapchain creation and selection
- render pass creation
- framebuffer creation
- MSAA logic
- depth attachment logic
- resize handling

This file is the closest thing to a real Vulkan rendering loop bootstrapping layer in the project.

### 19.8. `include/oxide/vulkan/core/resource_registry.hpp`

This is a big design lesson.

It centralizes resource destruction and prevents object leaks and stale cleanup paths.

This is a strong example of how to organize Vulkan resources in a system.

### 19.9. `include/oxide/vulkan/core/resource_pool.hpp`

This is a C++ correctness lesson.

It demonstrates safe handle management using generations and slots.

Very worthwhile if you are learning C++ design around opaque handles.

### 19.10. `examples/`

The examples are the best way to connect the abstractions to real usage.

Start with:

- `examples/vulkan_triangle`
- `examples/textured_quad`
- `examples/animated_uniform`
- `examples/offscreen_png`

Then move to:

- `examples/render_to_texture`
- `examples/msaa_mrt_resolve`
- `examples/multiple_pipelines`

This progression builds skill in the same order production graphics code usually evolves.

---

## 20. Beginner exercises tied to this project

Here are exercises you can do to learn from Oxide instead of just reading it.

### Exercise 1: rewrite the startup path in plain C++

Take the startup flow from `Application` and rewrite it as a mini program with comments.

Goal:

- create a GLFW window
- create Vulkan instance
- choose a physical device
- create logical device
- print the selected GPU name

This teaches the core startup pipeline.

### Exercise 2: explain why `DeviceIdleGuard` exists

Write a short paragraph explaining:

- what the GPU is doing asynchronously
- why you cannot safely destroy resources immediately
- why the guard matters

This is a perfect learning exercise.

### Exercise 3: compare render pass creation to a simple pipeline

Take the `createRenderPass()` function and explain:

- what the attachments are
- why the resolve attachment is optional
- why the depth attachment is needed
- why the subpass dependencies matter

### Exercise 4: trace a resize event

Follow the path:

- GLFW resize callback
- `notifyFramebufferResized()`
- `recreateSwapchain()`
- render pass rebuild
- framebuffer rebuild

This teaches how resize events propagate in a graphics engine.

### Exercise 5: build a tiny triangle outside Oxide

Take one example and replace Oxide-specific abstractions with plain Vulkan calls.

This is the fastest way to understand what the wrapper is doing for you.

### Exercise 6: inspect `ResourcePool`

Try writing a simple custom handle pool in a standalone C++ file.

Then compare your version to the Oxide one.

This is great for learning safe handle patterns.

---

## 21. Suggested final study flow

A strong learning sequence looks like this:

1. Read this guide and the README.
2. Read `VulkanDevice` setup and `Application`.
3. Study the swapchain and render pass logic.
4. Read one triangle example.
5. Read a more advanced example like MSAA or offscreen rendering.
6. Compare how the examples use the engine and how the engine internally works.
7. Rebuild a tiny version yourself without Oxide.

This is the fastest and highest-quality path to understanding the project.

---

## 22. Good learning references

### Official Vulkan docs

- Vulkan Tutorial: https://vulkan-tutorial.com/
- Khronos Vulkan docs: https://registry.khronos.org/vulkan/
- Vulkan specification: https://www.khronos.org/registry/vulkan/specs/1.3-extensions/html/

### GLFW

- GLFW docs: https://www.glfw.org/docs/latest/

### C++ fundamentals

- C++ reference: https://en.cppreference.com/
- C++ Core Guidelines: https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines

### Good general graphics learning

- Learn OpenGL or Vulkan from a practical perspective
- Study a minimal triangle app and then compare it to this library

---

## 23. Final takeaways

Oxide is a good project for learning because it makes the right things explicit.

It teaches:

- resource ownership
- Vulkan device setup
- swapchain lifecycle
- render pass architecture
- synchronization and cleanup patterns
- the difference between a renderer bootstrap library and a full engine

It is not a “magic” engine layer. It exposes the important parts of Vulkan instead of hiding them.

That is exactly what a beginner should want.

If you study the layout of the code and learn the lifecycle rules, you are learning how modern Vulkan engines think about rendering resources and GPU ownership.

---

## 24. Short version

If you remember just a few things from this guide, remember these:

- Vulkan is explicit, not magical
- GPU resources outlive CPU objects
- cleanup and synchronization matter
- swapchain rebuilds are normal
- render passes and framebuffer attachments are a central design concept
- Oxide is an educational Vulkan helper library, not a full engine

That is the core mental model you need.

Oxide is a good project for learning because it makes the right things explicit.

It teaches:

- resource ownership
- Vulkan device setup
- swapchain lifecycle
- render pass architecture
- synchronization and cleanup patterns
- the difference between a renderer bootstrap library and a full engine

It is not a “magic” engine layer. It exposes the important parts of Vulkan instead of hiding them.

That is exactly what a beginner should want.

If you study the layout of the code and learn the lifecycle rules, you are learning how modern Vulkan engines think about rendering resources and GPU ownership.

---

## 20. How to read Vulkan code like a pro

When reading Vulkan code, especially code like Oxide, there are a few things you should look for immediately.

### 20.1. Ask: what owns this resource?

Every `VkHandle` object should correspond to some owner that knows when it is safe to destroy it.

Examples:

- `VulkanDevice` owns the instance and logical device
- `VulkanContext` owns the swapchain, render pass, and framebuffers
- `ResourceRegistry` owns pooled Vulkan objects

If a resource’s owner is unclear, the code is probably hard to reason about.

### 20.2. Ask: does this code wait for the GPU before destroying?

This is the most important beginner question.

If the answer is no, something is probably unsafe.

In Vulkan, destruction is often not “just delete the object”; it is “wait until outstanding work is done first.”

### 20.3. Ask: does this resource depend on another resource?

Examples:

- framebuffer depends on render pass
- render pass depends on attachment formats
- pipeline depends on layout
- image view depends on image

This is a very common place for mistakes.

### 20.4. Ask: what is the current Vulkan state for this object?

Vulkan objects go through state transitions:

- image layout changes
- render pass attachment usage
- framebuffer recreation
- swapchain rebuild

If you do not know the current state, you cannot reason about correctness.

### 20.5. Ask: what is being synchronized?

Every time you see semaphores, fences, or queue waits, ask:

- what is waiting?
- what is being protected?
- what operation must complete before the next step?

This is one of the biggest leaps from “graphics programming” to “real Vulkan engineering.”

### 20.6. Ask: what would break on resize?

This is a great mental model for Vulkan codebases.

A huge fraction of Vulkan bugs happen when a window resizes or a swapchain is recreated.

Every object depending on the swapchain may need rebuild:

- render pass
- framebuffer
- depth buffer
- multisample color buffer
- pipeline state

This is why Oxide has `recreateSwapchain()` and callback notifications.

---

## 21. Suggested exercise plan

If you want to learn by doing, try this sequence:

1. Read `Application` and `VulkanDevice`
2. Write your own tiny window with GLFW and Vulkan instance creation
3. Add a surface and device selection
4. Create a basic swapchain and render pass
5. Draw a triangle with a simple shader
6. Add a depth buffer
7. Add MSAA
8. Add texture loading and descriptor sets
9. Compare your code to Oxide

This is the fastest path from “I read the code” to “I understand the system.”

---

## 22. Vulkan dictionary: types, structs, handles, and constants

This is the cheat-sheet section for the confusing Vulkan vocabulary.

The goal is not to memorize every enum value. The goal is to understand what each thing is, what it is used for, and how it fits into the engine lifecycle.

---

### 22.1. `VkInstance`

Type: handle

What it is:
- the top-level Vulkan instance
- the connection between your app and Vulkan

Used for:
- enumerating physical devices
- creating surfaces
- enabling validation layers
- creating device objects

In Oxide:
- created in `VulkanDevice::createInstance()`

Typical pattern:
```cpp
VkInstance instance;
vkCreateInstance(&create_info, nullptr, &instance);
```

Why it matters:
- this is the first Vulkan object you create
- it is one of the most global objects in the API

---

### 22.2. `VkPhysicalDevice`

Type: handle

What it is:
- a GPU (or virtual GPU) exposed by Vulkan

Used for:
- checking device capabilities
- checking queue families
- enumerating supported features
- selecting the GPU to render with

In Oxide:
- `VulkanDevice::selectPhysicalDevice()` chooses it

Typical pattern:
```cpp
uint32_t count = 0;
vkEnumeratePhysicalDevices(instance, &count, nullptr);
```

Why it matters:
- you do not render with the instance directly
- you render with a logical device created from a physical device

---

### 22.3. `VkDevice`

Type: handle

What it is:
- the logical device created from a physical device

Used for:
- creating images, buffers, pipelines, command pools
- submitting work
- destroying resources

In Oxide:
- `VulkanDevice::device_`

Typical pattern:
```cpp
vkCreateDevice(physical_device, &device_info, nullptr, &device);
```

Why it matters:
- most Vulkan objects are created from a logical device
- the logical device is your main API connection for resource creation and work submission

---

### 22.4. `VkQueue`

Type: handle

What it is:
- a queue of work submitted to a device

Used for:
- graphics operations
- presentation
- transfer
- compute

In Oxide:
- `graphics_queue_`
- `present_queue_`

Typical pattern:
```cpp
vkGetDeviceQueue(device, queue_family_index, 0, &graphics_queue);
```

Why it matters:
- pipelines and render commands are not executed directly by the CPU
- commands are submitted to a queue

---

### 22.5. `VkSurfaceKHR`

Type: handle

What it is:
- connection between Vulkan and the windowing system

Used for:
- presenting rendered images to a window
- querying surface capabilities
- creating a swapchain

In Oxide:
- created by `glfwCreateWindowSurface()`

Typical pattern:
```cpp
glfwCreateWindowSurface(instance, window, nullptr, &surface);
```

Why it matters:
- without a surface, a Vulkan app cannot display frames to a window

---

### 22.6. `VkSwapchainKHR`

Type: handle

What it is:
- the set of images used for presentation to the screen

Used for:
- presenting rendered frames
- holding the current backbuffer images

In Oxide:
- `VulkanContext::swapchain_`

Typical pattern:
```cpp
vkCreateSwapchainKHR(device, &swapchain_create_info, nullptr, &swapchain);
```

Why it matters:
- the swapchain is a core part of the render loop
- it is recreated when the window size changes

---

### 22.7. `VkRenderPass`

Type: handle

What it is:
- describes the render target setup and attachment transitions

Used for:
- defining color/depth attachments
- describing load/store operations
- describing subpasses

In Oxide:
- `VulkanContext::render_pass_`

Typical pattern:
```cpp
VkRenderPassCreateInfo info{};
vkCreateRenderPass(device, &info, nullptr, &render_pass);
```

Why it matters:
- framebuffers reference a render pass
- pipelines often assume a render pass layout compatible with their output

---

### 22.8. `VkFramebuffer`

Type: handle

What it is:
- an actual collection of image views used for a render pass

Used for:
- rendering into a set of attachments
- representing a final image target for a pass

In Oxide:
- `swapchain_framebuffers_`

Typical pattern:
```cpp
VkFramebufferCreateInfo info{};
vkCreateFramebuffer(device, &info, nullptr, &framebuffer);
```

Why it matters:
- without a framebuffer, the render pass cannot render to actual images

---

### 22.9. `VkCommandPool`

Type: handle

What it is:
- the pool from which command buffers are allocated

Used for:
- allocating command buffers for rendering and transfers

In Oxide:
- `VulkanDevice::command_pool_`

Typical pattern:
```cpp
VkCommandPoolCreateInfo pool_info{};
vkCreateCommandPool(device, &pool_info, nullptr, &command_pool);
```

Why it matters:
- command pools manage the lifetime of command buffers
- they are often reused across many draw calls

---

### 22.10. `VkCommandBuffer`

Type: handle

What it is:
- an object that records GPU commands

Used for:
- begin/end recording
- bind pipelines
- bind buffers
- draw calls
- barriers
- image transitions

In Oxide:
- `beginFrameScope()` uses command buffers from the frame object
- `beginSingleTimeCommands()` creates a short temporary one

Typical pattern:
```cpp
vkBeginCommandBuffer(cmd, &begin_info);
// record commands
vkEndCommandBuffer(cmd);
```

Why it matters:
- this is where nearly all GPU work gets recorded

---

### 22.11. `VkBuffer`

Type: handle

What it is:
- a GPU buffer storing raw data

Used for:
- vertex buffers
- index buffers
- uniform buffers
- storage buffers

In Oxide:
- used across render resources and memory management

Typical pattern:
```cpp
vkCreateBuffer(device, &buffer_info, nullptr, &buffer);
```

Why it matters:
- this is how vertex/index/uniform data is uploaded to the GPU

---

### 22.12. `VkImage`

Type: handle

What it is:
- a GPU image resource

Used for:
- textures
- depth buffers
- swapchain color images
- offscreen render targets

In Oxide:
- swapchain images and depth attachments are images

Typical pattern:
```cpp
vkCreateImage(device, &image_info, nullptr, &image);
```

Why it matters:
- images are not just textures; they can be render targets, depth buffers, and sampling resources

---

### 22.13. `VkImageView`

Type: handle

What it is:
- a view into an image with a specific format/usage interpretation

Used for:
- rendering attachments
- sampling textures
- representing a subrange of an image

In Oxide:
- swapchain image views and depth/color image views are created here

Typical pattern:
```cpp
VkImageViewCreateInfo view_info{};
vkCreateImageView(device, &view_info, nullptr, &image_view);
```

Why it matters:
- an image may be viewed in many ways; the view controls how it is accessed

---

### 22.14. `VkSampler`

Type: handle

What it is:
- how a texture is sampled in shader code

Used for:
- filtering
- wrapping
- anisotropy
- address modes

In Oxide:
- used for texture resources

Typical pattern:
```cpp
VkSamplerCreateInfo sampler_info{};
vkCreateSampler(device, &sampler_info, nullptr, &sampler);
```

Why it matters:
- sampling behavior is defined by the sampler, not the image alone

---

### 22.15. `VkShaderModule`

Type: handle

What it is:
- compiled SPIR-V shader code

Used for:
- vertex shader
- fragment shader
- geometry shader
- compute shader

In Oxide:
- managed by `ResourceRegistry` and `ShaderLibrary`

Typical pattern:
```cpp
vkCreateShaderModule(device, &module_info, nullptr, &module);
```

Why it matters:
- the shader module is the compiled GPU program object

---

### 22.16. `VkPipeline`

Type: handle

What it is:
- compiled graphics or compute pipeline state

Used for:
- rendering objects with a specific shader and fixed-function configuration

In Oxide:
- `Pipeline` wraps it

Typical pattern:
```cpp
vkCreateGraphicsPipelines(device, pipeline_cache, 1, &pipeline_info, nullptr, &pipeline);
```

Why it matters:
- a pipeline is the GPU state machine for drawing

---

### 22.17. `VkPipelineLayout`

Type: handle

What it is:
- layout describing descriptor set layouts and push constant ranges

Used for:
- binding resources to shaders

In Oxide:
- created via `PipelineLayout`

Typical pattern:
```cpp
VkPipelineLayoutCreateInfo info{};
vkCreatePipelineLayout(device, &info, nullptr, &layout);
```

Why it matters:
- pipeline layout determines how shader resources are bound and accessed

---

### 22.18. `VkDescriptorSetLayout`

Type: handle

What it is:
- layout for a descriptor set

Used for:
- describing uniform buffers, samplers, combined images, etc.

In Oxide:
- used through descriptor set wrappers

Typical pattern:
```cpp
vkCreateDescriptorSetLayout(device, &layout_info, nullptr, &descriptor_set_layout);
```

Why it matters:
- a descriptor set is only valid if it matches a layout expected by the shader

---

### 22.19. `VkDescriptorSet`

Type: handle

What it is:
- a collection of descriptors that bind resources to a pipeline

Used for:
- uniform buffers
- samplers
- sampled images
- storage resources

In Oxide:
- wrappers exist in `resources/descriptor_set.hpp` and related files

Typical pattern:
```cpp
vkAllocateDescriptorSets(device, &alloc_info, &descriptor_set);
```

Why it matters:
- this is how shader resources are actually supplied to the GPU

---

### 22.20. `VkSemaphore`

Type: handle

What it is:
- synchronization primitive between queue operations

Used for:
- waiting for image availability
- signaling render completion

In Oxide:
- `image_available_` and `render_finished_`

Typical pattern:
```cpp
vkCreateSemaphore(device, &info, nullptr, &semaphore);
```

Why it matters:
- semaphores coordinate GPU-GPU and GPU-CPU synchronization in the present loop

---

### 22.21. `VkFence`

Type: handle

What it is:
- a synchronization primitive used to wait for a queue submission to complete

Used for:
- ensuring frame resources are available before reuse

In Oxide:
- `in_flight_` and `images_in_flight_`

Typical pattern:
```cpp
vkCreateFence(device, &fence_info, nullptr, &fence);
```

Why it matters:
- fences are used to ensure CPU waits do not run into resource reuse conflicts

---

### 22.22. `VkFormat`

Type: enum value

What it is:
- image/texture format type

Examples:
- `VK_FORMAT_R8G8B8A8_SRGB`
- `VK_FORMAT_B8G8R8A8_SRGB`
- `VK_FORMAT_D32_SFLOAT`
- `VK_FORMAT_R32G32_SFLOAT`

Used for:
- describing the memory layout of pixel data
- deciding how data is interpreted in shaders

In Oxide:
- swapchain and depth formats are chosen by `VulkanContext`

Why it matters:
- format mismatch is one of the easiest ways to get invalid rendering or validation errors

---

### 22.23. `VkSampleCountFlagBits`

Type: enum value

What it is:
- sample count used for multisampling

Examples:
- `VK_SAMPLE_COUNT_1_BIT`
- `VK_SAMPLE_COUNT_2_BIT`
- `VK_SAMPLE_COUNT_4_BIT`
- `VK_SAMPLE_COUNT_8_BIT`

Used for:
- MSAA settings for rendering

In Oxide:
- `SwapchainConfig::sampleCount`
- `VulkanContext::sample_count_`

Why it matters:
- multisampling changes render pass setup and attachment behavior

---

### 22.24. `VkExtent2D`

Type: struct

What it is:
- width and height of a 2D image or swapchain extent

Used for:
- output resolution
- framebuffer size
- image size

Typical pattern:
```cpp
VkExtent2D extent{ width, height };
```

Why it matters:
- it describes the resolution of render targets and swapchain images

---

### 22.25. `VkViewport`

Type: struct

What it is:
- viewport mapping for rasterization output

Used for:
- setting the region of the framebuffer the rasterizer outputs to

Typical pattern:
```cpp
VkViewport viewport{};
viewport.x = 0.0f;
viewport.y = 0.0f;
viewport.width = width;
viewport.height = height;
viewport.minDepth = 0.0f;
viewport.maxDepth = 1.0f;
```

Why it matters:
- defines how normalized device coordinates map to framebuffer pixels

---

### 22.26. `VkRect2D`

Type: struct

What it is:
- scissor rectangle

Used for:
- limiting rendering to a region of the framebuffer

Typical pattern:
```cpp
VkRect2D scissor{{0,0}, {width, height}};
```

Why it matters:
- it restricts rendering to a subset of the framebuffer

---

### 22.27. `VkAttachmentDescription`

Type: struct

What it is:
- describes one attachment in a render pass

Used for:
- color image
- depth image
- resolve image

In Oxide:
- used in `createRenderPass()`

Why it matters:
- render pass correctness depends on attaching the proper color/depth/resolve info

---

### 22.28. `VkSubpassDescription`

Type: struct

What it is:
- describes a render subpass

Used for:
- picking the attachments that are used during rendering
- setting the pipeline bind point

Why it matters:
- it defines how the render pass is executed and how attachments are read/written

---

### 22.29. `VkSubpassDependency`

Type: struct

What it is:
- describes ordering and access dependencies between subpasses or external operations

Used for:
- color attachment writes before later reads
- depth/stencil writes before later use

In Oxide:
- used in `createRenderPass()` to orient the dependency chain

Why it matters:
- incorrect dependencies cause validation errors and broken rendering

---

### 22.30. `VkMemoryRequirements`

Type: struct

What it is:
- information about required memory size and alignment for an allocation

Used for:
- choosing a device memory allocation for a resource

Typical pattern:
```cpp
VkMemoryRequirements requirements{};
vkGetBufferMemoryRequirements(device, buffer, &requirements);
```

Why it matters:
- memory allocation is not just “any size”; alignment and size constraints matter

---

### 22.31. `VkDeviceMemory`

Type: handle

What it is:
- actual allocated GPU memory object

Used for:
- backing resources like buffers/images

In Oxide:
- managed by `DeviceMemoryAllocation` and `VulkanDevice::allocateMemory()`

Why it matters:
- this is the real underlying memory backing your Vulkan resources

---

### 22.32. `VkAllocationCallbacks`

Type: pointer-like struct

What it is:
- custom allocator hooks for Vulkan allocation routines

Used for:
- custom memory management

Why it matters:
- usually left as `nullptr` for standard allocations

---

### 22.33. `VkClearValue`

Type: union/struct

What it is:
- data used when clearing an attachment during render pass begin

Used for:
- clear color value
- clear depth and stencil value

Typical pattern:
```cpp
VkClearValue clear_color = {{0.1f, 0.2f, 0.3f, 1.0f}};
```

Why it matters:
- render pass begins often clear attachments before drawing

---

### 22.34. `VkPresentInfoKHR`

Type: struct

What it is:
- describes how a rendered image is presented to the screen

Used for:
- the final stage of the frame loop

Why it matters:
- this is how the GPU swaps the image to the window

---

### 22.35. `VkPipelineStageFlagBits`

Type: enum value

What it is:
- stage of the pipeline when a pipeline operation happens

Examples:
- `VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT`
- `VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT`
- `VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT`

Used for:
- synchronization dependency stage masks

Why it matters:
- they tell Vulkan when memory and commands are ready or available

---

### 22.36. `VkAccessFlagBits`

Type: enum value

What it is:
- the memory access type of an operation

Examples:
- `VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT`
- `VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT`
- `VK_ACCESS_SHADER_READ_BIT`

Used for:
- specifying read/write access in memory barriers and subpass dependencies

Why it matters:
- access masks define what exactly was done to memory and when it is safe to reuse it

---

### 22.37. `VkImageLayout`

Type: enum value

What it is:
- the current layout of an image

Examples:
- `VK_IMAGE_LAYOUT_UNDEFINED`
- `VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL`
- `VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL`
- `VK_IMAGE_LAYOUT_PRESENT_SRC_KHR`
- `VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL`

Used for:
- transitioning resources between stages of rendering and presentation

Why it matters:
- this is one of the most common Vulkan gotchas for beginners

---

### 22.38. `VkDescriptorType`

Type: enum value

What it is:
- the kind of resource described by a descriptor

Examples:
- `VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER`
- `VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER`
- `VK_DESCRIPTOR_TYPE_SAMPLER`
- `VK_DESCRIPTOR_TYPE_STORAGE_BUFFER`

Used for:
- descriptor set creation and binding

Why it matters:
- shaders and descriptor layouts must agree on descriptor types

---

### 22.39. `VkVertexInputAttributeDescription`

Type: struct

What it is:
- describes one input attribute for a vertex shader

Used for:
- positions
- normals
- UVs
- colors
- custom data

Typical pattern:
```cpp
VkVertexInputAttributeDescription attribute{};
attribute.location = 0;
attribute.binding = 0;
attribute.format = VK_FORMAT_R32G32_SFLOAT;
attribute.offset = offsetof(Vertex, position);
```

Why it matters:
- this tells Vulkan how vertex data is laid out in memory

---

### 22.40. `VkVertexInputBindingDescription`

Type: struct

What it is:
- describes how vertex data is packed in a binding

Used for:
- binding stride and input rate

Typical pattern:
```cpp
VkVertexInputBindingDescription binding{};
binding.binding = 0;
binding.stride = sizeof(Vertex);
binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
```

Why it matters:
- this matches the vertex buffer format with the shader’s input layout

---

### 22.41. `VkClearColorValue`

Type: union

What it is:
- a color value used when clearing render targets

Used for:
- clearing the framebuffer color to a specific RGBA color

Typical pattern:
```cpp
VkClearValue clear_color = {{ 0.05f, 0.08f, 0.10f, 1.0f }};
```

Why it matters:
- this is how the render pass begins with a clear background color

---

### 22.42. `VkClearDepthStencilValue`

Type: struct

What it is:
- depth/stencil clear values for depth attachments

Used for:
- clearing depth and stencil buffers before rendering

Why it matters:
- depth attachments must be initialized properly for correct visibility testing

---

### 22.43. `VkBool32`

Type: typedef of integer-like type

What it is:
- Vulkan boolean represented by 32-bit integer

Used for:
- booleans returned by Vulkan APIs

Typical pattern:
```cpp
VkBool32 present = VK_FALSE;
```

Why it matters:
- this is a Vulkan-specific boolean representation, not `bool`

---

### 22.44. `VkResult`

Type: enum value

What it is:
- return code for Vulkan calls

Examples:
- `VK_SUCCESS`
- `VK_ERROR_OUT_OF_DEVICE_MEMORY`
- `VK_ERROR_SURFACE_LOST_KHR`
- `VK_ERROR_EXTENSION_NOT_PRESENT`

Used for:
- checking if a Vulkan function succeeded

Why it matters:
- it is how Vulkan reports errors and resource issues

---

### 22.45. `VK_NULL_HANDLE`

Type: macro constant

What it is:
- null handle for Vulkan objects

Used for:
- unset handles
- obvious “no object yet” state

Typical pattern:
```cpp
VkDevice device = VK_NULL_HANDLE;
```

Why it matters:
- this is the Vulkan equivalent of `nullptr` for handles

---

### 22.46. Structures in Oxide specifically

Oxide introduces some wrappers and structs around these Vulkan concepts. These are extremely useful to recognize because they indicate what the library is abstracting.

#### `QueueFamilies`
- contains `graphics`, `present`, and status booleans
- used to determine if a device can render and present to a window

#### `SwapchainConfig`
- configures surface format, present mode, image count, image usage, and sample count
- used by `VulkanContext`

#### `SwapchainRecreationInfo`
- contains the generation and whether the render pass changed
- useful for pipelines that depend on render pass state

#### `DeviceMemoryAllocation`
- stores memory, offset, size, type, and mapped pointer
- used by the custom memory allocator

#### `ResourceRegistry`
- wraps all the object pools for Vulkan resources
- important because it centralizes ownership

#### `Frame`
- combines command buffer, image index, and frame index

#### `WindowConfig`
- config for width, height, title, transparency, resizing, etc.

---

## 23. Vulkan function dictionary: common `vk*` calls and what they do

This section covers the most important function names you are likely to see in Vulkan code, especially in Oxide and in future engine work.

The important idea is that these functions are not arbitrary; they match the life cycle of Vulkan objects:

- create
- query
- allocate
- bind
- record
- submit
- destroy

### 23.1. `vkCreateInstance`

What it does:
- creates a Vulkan instance

Used for:
- base Vulkan API setup

Typical purpose:
- before any Vulkan device or queue is created

### 23.2. `vkEnumerateInstanceExtensionProperties`

What it does:
- lists instance-level extensions supported by the Vulkan loader

Used for:
- checking whether an extension is available before enabling it

Example in Oxide:
```cpp
instanceExtensionAvailable(debug_extension);
```

### 23.3. `vkEnumerateInstanceLayerProperties`

What it does:
- lists available Vulkan layers

Used for:
- checking if validation layers exist

Example in Oxide:
```cpp
validationAvailable();
```

### 23.4. `vkGetInstanceProcAddr`

What it does:
- loads a function pointer for a Vulkan extension entry point

Used for:
- extension functions like debug messenger creation or debug callbacks

Example in Oxide:
```cpp
const auto create_messenger = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
    vkGetInstanceProcAddr(instance_, "vkCreateDebugUtilsMessengerEXT"));
```

Why it matters:
- many Vulkan extensions are not loaded by default and must be queried this way

### 23.5. `vkCreateDebugUtilsMessengerEXT`

What it does:
- creates a debug callback messenger

Used for:
- validation layer logging

Why it matters:
- this is how validation messages are routed to the application

### 23.6. `vkDestroyDebugUtilsMessengerEXT`

What it does:
- destroys the debug messenger

Used for:
- cleanup before instance destruction

### 23.7. `vkEnumeratePhysicalDevices`

What it does:
- lists all GPUs available to Vulkan

Used for:
- selecting a usable GPU

### 23.8. `vkGetPhysicalDeviceProperties`

What it does:
- queries physical device capabilities and metadata

Used for:
- device name
- type (integrated/discrete)
- limits

### 23.9. `vkGetPhysicalDeviceFeatures`

What it does:
- queries feature support

Used for:
- enabling pipeline features such as anisotropy and geometry shaders

### 23.10. `vkGetPhysicalDeviceQueueFamilyProperties`

What it does:
- lists queue families supported by the GPU

Used for:
- determining graphics and present-capable families

### 23.11. `vkGetPhysicalDeviceSurfaceSupportKHR`

What it does:
- checks whether a queue family supports presentation to a given surface

Used for:
- queue family selection

### 23.12. `vkGetPhysicalDeviceSurfaceCapabilitiesKHR`

What it does:
- queries surface capabilities

Used for:
- min/max image count
- min/max extent
- supported transforms and alpha modes

### 23.13. `vkGetPhysicalDeviceSurfaceFormatsKHR`

What it does:
- queries supported color formats for a surface

Used for:
- choosing a swapchain format

### 23.14. `vkGetPhysicalDeviceSurfacePresentModesKHR`

What it does:
- queries supported present modes

Used for:
- choosing mailbox/FIFO/etc.

### 23.15. `vkCreateDevice`

What it does:
- creates the logical device

Used for:
- enabling queue families and device features
- creating most Vulkan resources

### 23.16. `vkGetDeviceQueue`

What it does:
- gets a queue handle from the device and queue family

Used for:
- graphics queue
- present queue
- transfer queue

### 23.17. `vkCreateCommandPool`

What it does:
- allocates a command pool

Used for:
- later allocation of command buffers

### 23.18. `vkAllocateCommandBuffers`

What it does:
- allocates command buffers from a command pool

Used for:
- recording work for a frame or short upload operation

### 23.19. `vkBeginCommandBuffer`

What it does:
- starts recording into a command buffer

Used for:
- all command recording work

### 23.20. `vkEndCommandBuffer`

What it does:
- finishes recording a command buffer

Used for:
- readying it for submission

### 23.21. `vkQueueSubmit`

What it does:
- submits recorded commands to a queue

Used for:
- executing work on the GPU

### 23.22. `vkQueueWaitIdle`

What it does:
- waits until a queue has completed all submitted work

Used for:
- cleanup and synchronization

### 23.23. `vkCreateSwapchainKHR`

What it does:
- creates a swapchain

Used for:
- managing presentable images

### 23.24. `vkGetSwapchainImagesKHR`

What it does:
- gets the images owned by the swapchain

Used for:
- creating image views and framebuffers

### 23.25. `vkCreateImageView`

What it does:
- creates a view into an image

Used for:
- rendering to or sampling from an image

### 23.26. `vkCreateRenderPass`

What it does:
- creates a render pass

Used for:
- describing render target attachments and layout transitions

### 23.27. `vkCreateFramebuffer`

What it does:
- creates a framebuffer bound to a render pass and actual image views

Used for:
- rendering into a set of attachments

### 23.28. `vkCreateImage`

What it does:
- creates an actual image object

Used for:
- textures, depth attachments, MSAA color attachments, render targets

### 23.29. `vkGetImageMemoryRequirements`

What it does:
- queries memory requirements for an image

Used for:
- choosing a proper device memory allocation

### 23.30. `vkAllocateMemory`

What it does:
- allocates device memory

Used for:
- backing buffers or images

### 23.31. `vkBindImageMemory`

What it does:
- binds an allocated memory block to an image

Used for:
- making image memory usable

### 23.32. `vkBindBufferMemory`

What it does:
- binds allocated memory to a buffer

Used for:
- making buffer data usable

### 23.33. `vkMapMemory`

What it does:
- maps device memory into host-visible memory so CPU writes are possible

Used for:
- uploading vertex data, index data, uniforms, textures

### 23.34. `vkUnmapMemory`

What it does:
- unmaps memory after CPU writes are complete

Used for:
- finalizing host writes before GPU use

### 23.35. `vkCreateBuffer`

What it does:
- creates a buffer object

Used for:
- vertex/index/uniform buffers

### 23.36. `vkGetBufferMemoryRequirements`

What it does:
- queries memory requirements for a buffer

Used for:
- choosing the correct memory to bind

### 23.37. `vkCreateShaderModule`

What it does:
- creates a shader module from SPIR-V code

Used for:
- graphics and compute shaders

### 23.38. `vkCreatePipelineLayout`

What it does:
- creates a pipeline layout

Used for:
- descriptor sets and push constants used by shaders

### 23.39. `vkCreateGraphicsPipelines`

What it does:
- creates a graphics pipeline

Used for:
- rendering geometry with shaders and fixed-function state

### 23.40. `vkDestroyPipeline`

What it does:
- destroys a pipeline

Used for:
- cleanup and swapchain rebuilds

### 23.41. `vkCreateDescriptorSetLayout`

What it does:
- creates a descriptor set layout

Used for:
- defining shader resource bindings

### 23.42. `vkCreateDescriptorPool`

What it does:
- creates a descriptor pool

Used for:
- allocating descriptor sets

### 23.43. `vkAllocateDescriptorSets`

What it does:
- allocates descriptor sets from a descriptor pool

Used for:
- binding resources like UBOs and samplers

### 23.44. `vkUpdateDescriptorSets`

What it does:
- writes resources into a descriptor set

Used for:
- uniform buffers and textures in shaders

### 23.45. `vkCmdBeginRenderPass`

What it does:
- begins a render pass recording on a command buffer

Used for:
- starting drawing to framebuffer attachments

### 23.46. `vkCmdEndRenderPass`

What it does:
- ends the render pass recording

Used for:
- finalizing the framebuffer pass

### 23.47. `vkCmdBindPipeline`

What it does:
- binds a pipeline to the command buffer

Used for:
- selecting the graphics pipeline for subsequent draws

### 23.48. `vkCmdBindVertexBuffers`

What it does:
- binds vertex buffers to the pipeline

Used for:
- giving shader input vertex data

### 23.49. `vkCmdBindIndexBuffer`

What it does:
- binds an index buffer

Used for:
- indexed drawing

### 23.50. `vkCmdDraw`

What it does:
- issues a draw call

Used for:
- rendering geometry

### 23.51. `vkCmdDrawIndexed`

What it does:
- issues an indexed draw call

Used for:
- drawing indexed meshes

### 23.52. `vkCmdSetViewport`

What it does:
- sets viewport state for rasterization

Used for:
- mapping NDC to framebuffer pixels

### 23.53. `vkCmdSetScissor`

What it does:
- sets the scissor rectangle

Used for:
- restricting drawing to a sub-region

### 23.54. `vkCmdPipelineBarrier`

What it does:
- inserts a pipeline barrier for image/buffer layout transitions or synchronization

Used for:
- moving images from `UNDEFINED` to `TRANSFER_DST_OPTIMAL`, etc.

### 23.55. `vkCreateSampler`

What it does:
- creates a texture sampler

Used for:
- texture sampling parameters in shaders

### 23.56. `vkCreateSemaphore`

What it does:
- creates a semaphore for queue synchronization

Used for:
- frame synchronization and image presentation

### 23.57. `vkCreateFence`

What it does:
- creates a fence

Used for:
- waiting for GPU work to finish before reusing resources

### 23.58. `vkWaitForFences`

What it does:
- waits for a fence to signal

Used for:
- CPU/GPU synchronization before reusing frame resources

### 23.59. `vkResetFences`

What it does:
- resets a fence to unsignaled state

Used for:
- frame loop reuse

### 23.60. `vkAcquireNextImageKHR`

What it does:
- gets the next swapchain image index to render to

Used for:
- the render loop frame stepping

### 23.61. `vkQueuePresentKHR`

What it does:
- presents the rendered image on the screen

Used for:
- final frame delivery to the window

### 23.62. `vkDeviceWaitIdle`

What it does:
- waits for all work submitted to the logical device to finish

Used for:
- teardown and safe resource cleanup

### 23.63. `vkDestroySwapchainKHR`

What it does:
- destroys a swapchain

Used for:
- recreation or teardown

### 23.64. `vkDestroyFramebuffer`

What it does:
- destroys a framebuffer

Used for:
- cleanup and swapchain recreation

### 23.65. `vkDestroyRenderPass`

What it does:
- destroys a render pass

Used for:
- cleanup and render-pass replacement on swapchain changes

### 23.66. `vkDestroyImageView`

What it does:
- destroys an image view

Used for:
- cleanup and swapchain recreation

### 23.67. `vkDestroyImage`

What it does:
- destroys an image

Used for:
- cleanup of images, textures, render targets, and depth attachments

### 23.68. `vkDestroyBuffer`

What it does:
- destroys a buffer

Used for:
- cleanup and resource teardown

### 23.69. `vkDestroyShaderModule`

What it does:
- destroys a shader module

Used for:
- cleanup of compiled shader programs

### 23.70. `vkDestroyDescriptorSetLayout`

What it does:
- destroys a descriptor set layout

Used for:
- cleanup and resource teardown

### 23.71. `vkDestroyDescriptorPool`

What it does:
- destroys a descriptor pool

Used for:
- cleanup of descriptor sets

### 23.72. `vkFreeMemory`

What it does:
- frees device memory that was previously allocated

Used for:
- resource release and memory management

### 23.73. `vkDestroySurfaceKHR`

What it does:
- destroys a Vulkan surface

Used for:
- cleanup before tearing down the instance

### 23.74. `vkDestroyInstance`

What it does:
- destroys the Vulkan instance

Used for:
- final shutdown

---

## 24. The biggest mental model for Vulkan

If you memorize only one thing, remember this:

Vulkan is not just “draw triangle with a GPU”.

Vulkan is:

- choosing devices and queues
- allocating and managing memory
- creating and validating many handles
- recording work into command buffers
- synchronizing work across queues and resources
- recreating a lot of state when the swapchain changes
- carefully handling object lifetimes and access patterns

The types and structs are a language for describing this state machine.

When you understand that, the API starts to make sense.

---

## 25. Short version

If you remember just a few things from this guide, remember these:

- Vulkan is explicit, not magical
- GPU resources outlive CPU objects
- cleanup and synchronization matter
- swapchain rebuilds are normal
- render passes and framebuffer attachments are a central design concept
- Oxide is an educational Vulkan helper library, not a full engine
- every Vulkan handle is a piece of a bigger state machine

That is the core mental model you need.

If you memorize only one thing, remember this:

Vulkan is not just “draw triangle with a GPU”.

Vulkan is:

- choosing devices and queues
- allocating and managing memory
- creating and validating many handles
- recording work into command buffers
- synchronizing work across queues and resources
- recreating a lot of state when the swapchain changes
- carefully handling object lifetimes and access patterns

The types and structs are a language for describing this state machine.

When you understand that, the API starts to make sense.
