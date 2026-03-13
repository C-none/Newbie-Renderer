# Current RHI Support Analysis for a Vulkan Shadow-Mapping Renderer

## Scope

This document evaluates whether the current `src/rhi` layer can serve as the base for implementing the following renderer pipeline:

1. Vulkan instance, debug messenger, surface, physical device, logical device, queues
2. Swapchain and swapchain image views
3. Shadow-map depth image, sampler, and main-scene depth image
4. Shadow pass and main pass setup
5. Framebuffer-style attachment binding
6. Descriptor layouts, descriptor sets, and pipeline layouts
7. Graphics pipeline creation
8. Vertex, index, and uniform buffers
9. Descriptor pool allocation and descriptor writes
10. Command pools, command buffers, fences, and semaphores
11. Two-pass command recording for shadow + main scene
12. Frame loop with acquire, update, submit, and present
13. Cleanup

The main question is not only whether the RHI has low-level Vulkan wrappers, but whether its current architecture matches the requested implementation model.

## Executive Summary

Conclusion: the current RHI is already strong enough to implement a shadow-mapping renderer, but not in the exact traditional Vulkan shape described in the task.

The most important architectural fact is that this RHI is built around Vulkan 1.3 `dynamicRendering` and `synchronization2`, not around classic `VkRenderPass` plus `VkFramebuffer`. That means:

- the renderer can implement the requested two-pass shadow workflow;
- depth images, swapchain images, image views, samplers, descriptor writes, command recording, and synchronization are all present;
- but phases 4 and 5 must be reinterpreted as `vkCmdBeginRendering` attachment binding rather than explicit `VkRenderPass` and `VkFramebuffer` objects.

There are also several concrete capability gaps or architectural risks:

- physical-device selection does not score against surface present support or requested depth-format capability;
- present queue selection is currently hardwired to the compute-family fallback rather than a dedicated present-capable family;
- there is no helper to query and choose a supported depth format for shadow maps / scene depth;
- there is no classic vertex-input layout abstraction in the graphics pipeline API;
- swapchain recreation does not automatically recreate dependent depth targets or shadow-map attachments.

So the answer is: yes, the current RHI can be the base, but some targeted extensions are still needed before this becomes a clean, production-ready foundation for the requested renderer.

## Reference Architecture Observed in the Current RHI

The current RHI is organized around a few major services:

- `Device` owns instance, debug messenger, physical device, logical device, memory allocators, queues, frame manager, presentation context, and pipeline service: [src/rhi/nrDevice.ixx](../src/rhi/nrDevice.ixx)
- `PresentationContext` owns the GLFW window surface and swapchain lifecycle: [src/rhi/nrSwapchain.ixx](../src/rhi/nrSwapchain.ixx)
- `Buffer` and `Image` are RAII GPU resources backed by the allocator layer: [src/rhi/nrResource.ixx](../src/rhi/nrResource.ixx)
- synchronization and layout transitions are implemented with `VkImageMemoryBarrier2`/`vkCmdPipelineBarrier2` style helpers: [src/rhi/nrResourceOps.ixx](../src/rhi/nrResourceOps.ixx)
- descriptor reflection, allocation, and update are implemented through Slang reflection plus `ShaderResourceWriter`: [src/rhi/nrDescriptor.ixx](../src/rhi/nrDescriptor.ixx)
- graphics pipelines are created with `VkPipelineRenderingCreateInfo`, so the backend is explicitly using dynamic rendering: [src/rhi/nrPipeline.ixx](../src/rhi/nrPipeline.ixx)

This means the RHI is already fairly modern, but it is opinionated. It does not model every Vulkan concept 1:1.

## Vulkan Documentation Baseline

The analysis below was cross-checked against the Vulkan reference pages:

- `vkCmdBeginRendering`: dynamic rendering requires the `dynamicRendering` feature and attachments must already be in the declared layouts.
- `VkPhysicalDeviceVulkan13Features`: `dynamicRendering`, `synchronization2`, and `inlineUniformBlock` must be enabled before use.
- `VkSamplerCreateInfo`: `compareEnable` and `compareOp` are the standard mechanism for depth comparison samplers used by shadow mapping.
- `vkGetPhysicalDeviceFormatProperties`: depth-format support must be queried per physical device.
- `vkUpdateDescriptorSets`: descriptor updates are valid, but updating sets already used by recorded/pending command buffers is restricted unless update-after-bind rules apply.
- `VkImageMemoryBarrier2`: image layout transitions and queue-family ownership transfers are expressed through synchronization2 barriers.
- `vkQueuePresentKHR`: presentation must happen on a queue that supports presentation to the target surface.

Official links:

- <https://docs.vulkan.org/refpages/latest/refpages/source/vkCmdBeginRendering.html>
- <https://docs.vulkan.org/refpages/latest/refpages/source/VkPhysicalDeviceVulkan13Features.html>
- <https://docs.vulkan.org/refpages/latest/refpages/source/VkSamplerCreateInfo.html>
- <https://docs.vulkan.org/refpages/latest/refpages/source/vkGetPhysicalDeviceFormatProperties.html>
- <https://docs.vulkan.org/refpages/latest/refpages/source/vkUpdateDescriptorSets.html>
- <https://docs.vulkan.org/refpages/latest/refpages/source/VkImageMemoryBarrier2.html>
- <https://docs.vulkan.org/refpages/latest/refpages/source/vkQueuePresentKHR.html>

## Phase-by-Phase Support Matrix

### Phase 1: Vulkan basic environment and device initialization

Status: mostly supported, with two important selection/present-queue weaknesses.

Already supported:

- instance creation is handled by `Device::makeInstance(...)`: [src/rhi/nrDevice.ixx](../src/rhi/nrDevice.ixx)
- GLFW-required window-system extensions are collected in `Device::setupInitialFlags()`: [src/rhi/nrDevice.ixx](../src/rhi/nrDevice.ixx)
- validation layer and debug utils are enabled in debug mode: [src/rhi/nrDevice.ixx](../src/rhi/nrDevice.ixx), [src/rhi/vkrhi.ixx](../src/rhi/vkrhi.ixx)
- window and Vulkan surface creation are wrapped by `Surface::create(...)`: [src/rhi/nrSurface.ixx](../src/rhi/nrSurface.ixx)
- logical device and queue-family selection are implemented in `Device::makeDevice()`: [src/rhi/nrDevice.ixx](../src/rhi/nrDevice.ixx)
- device extensions include `VK_KHR_swapchain`: [src/rhi/nrDevice.ixx](../src/rhi/nrDevice.ixx)
- device feature validation includes `dynamicRendering`, `synchronization2`, and `inlineUniformBlock`: [src/rhi/nrDevice.ixx](../src/rhi/nrDevice.ixx)

Support caveats:

- `selectPhysicalDevice(...)` only prefers discrete GPUs and larger `maxImageDimension2D`; it does not score by surface present support, required depth-format support, or user-requested optional features like geometry shader: [src/rhi/vkrhi.ixx](../src/rhi/vkrhi.ixx)
- present support is only checked later in `PresentationContext::ensurePresentSupport(...)`, after the device choice has already been made: [src/rhi/nrSwapchain.ixx](../src/rhi/nrSwapchain.ixx)
- `presentQueueFamilyIndex()` currently returns the compute queue fallback rather than discovering a dedicated present queue: [src/rhi/nrDevice.ixx](../src/rhi/nrDevice.ixx)
- `PresentationContext::present(...)` presents on `queueManager.compute()`: [src/rhi/nrSwapchain.ixx](../src/rhi/nrSwapchain.ixx)

Assessment:

- For a simple single-GPU desktop machine this will often work.
- For a robust Vulkan renderer this is not enough. Physical-device evaluation should explicitly include:
  - `vkGetPhysicalDeviceSurfaceSupportKHR`
  - required depth format support
  - optional shader-stage features actually needed by the selected shaders

### Phase 2: Swapchain and base image resources

Status: supported.

Already supported:

- swapchain creation, recreation, and image extraction: `SwapChain::create(...)`, `SwapChain::recreate(...)`, `acquireNextImage(...)`, `present(...)`: [src/rhi/nrSwapchain.ixx](../src/rhi/nrSwapchain.ixx)
- swapchain images are cached in `swapChainImages`
- swapchain image views are automatically created in `imageViews`
- surface format and extent are stored in the presentation state

Assessment:

- Phase 2 is already covered cleanly by the current RHI.

### Phase 3: Shadow-map depth resources and main-scene depth resources

Status: mostly supported, but a depth-format selection helper is missing.

Already supported:

- generic image creation through `Image::create(...)`: [src/rhi/nrResource.ixx](../src/rhi/nrResource.ixx)
- image memory allocation is handled by the allocator layer underneath `Image`
- default image view creation and custom auxiliary views are supported: `addView`, `addMipView`, `addLayerView`, `addAspectView`: [src/rhi/nrResource.ixx](../src/rhi/nrResource.ixx)
- aspect inference for depth/stencil formats is implemented in `inferAspectFlags(...)`: [src/rhi/vkrhi.ixx](../src/rhi/vkrhi.ixx)
- samplers support `compareEnable`, `compareOp`, address modes, filtering, anisotropy, and LOD settings through `SlangSamplerDesc` and `SlangSampler::create(...)`: [src/rhi/nrSlang.ixx](../src/rhi/nrSlang.ixx)

Missing or weak support:

- there is no helper that queries the physical device and chooses a supported depth format for shadow mapping or scene depth
- there is no RHI-level helper like `findSupportedDepthFormat(...)` built on `vkGetPhysicalDeviceFormatProperties` / `getFormatProperties`
- there is no convenience constructor for a depth image configured specifically for `DepthAttachment | Sampled`

Assessment:

- the low-level primitives needed for both shadow depth and scene depth are present;
- but the selection logic the user asked for in this phase still has to be added.

Recommended missing API:

```cpp
[[nodiscard]] vk::Format findSupportedDepthFormat(
    const vk::raii::PhysicalDevice& physicalDevice,
    std::span<const vk::Format> candidates,
    vk::ImageTiling tiling,
    vk::FormatFeatureFlags requiredFeatures);
```

### Phase 4: Render pass design

Status: supported in spirit, but not with explicit `VkRenderPass` objects.

Current architecture:

- `GraphicsPipeline::create(...)` uses `vk::PipelineRenderingCreateInfo` and sets `createInfo.renderPass = nullptr`: [src/rhi/nrPipeline.ixx](../src/rhi/nrPipeline.ixx)
- `ScopedRendering` wraps `commandBuffer.beginRendering(...)` / `endRendering(...)`: [src/rhi/nrResourceOps.ixx](../src/rhi/nrResourceOps.ixx)
- `RenderingScopeDesc` and attachment descriptors provide the equivalent attachment information at command-recording time: [src/rhi/nrResourceOps.ixx](../src/rhi/nrResourceOps.ixx)

What this means for the requested task:

- there is no `Shadow Render Pass` object;
- there is no `Main Render Pass` object;
- there are no Vulkan subpasses or subpass dependencies;
- instead, each pass is described inline when recording commands.

Assessment:

- The renderer can absolutely do the requested two-pass pipeline.
- But the implementation must be translated from classic render-pass vocabulary to dynamic-rendering vocabulary.

Practical reinterpretation:

- `Shadow Render Pass` becomes one `ScopedRendering` block with only a depth attachment
- `Main Render Pass` becomes one `ScopedRendering` block with color attachment(s) plus a depth attachment
- `Subpass Dependencies` become explicit `BarrierBatch` / `transitionImage(...)` calls between passes

### Phase 5: Framebuffer binding

Status: not supported as explicit framebuffer objects, but functionally supported through attachment descriptors.

Already supported:

- dynamic rendering attachment binding via `RenderingAttachmentDesc`, `RenderingDepthStencilAttachmentDesc`, and `ScopedRendering`: [src/rhi/nrResourceOps.ixx](../src/rhi/nrResourceOps.ixx)

Not present by design:

- no `VkFramebuffer` wrapper
- no cached framebuffer object per swapchain image

Assessment:

- This is not a missing low-level capability.
- It is an architectural mismatch between the requested design and the actual RHI design.

### Phase 6: Descriptor sets and pipeline layouts

Status: supported.

Already supported:

- Slang reflection to Vulkan descriptor layout mapping through `ShaderDescriptorLayout::create(...)`: [src/rhi/nrDescriptor.ixx](../src/rhi/nrDescriptor.ixx)
- descriptor set layout creation through `CursorPipelineLayout::create(...)`: [src/rhi/nrPipeline.ixx](../src/rhi/nrPipeline.ixx)
- pipeline layout creation, including push constant ranges: [src/rhi/nrPipeline.ixx](../src/rhi/nrPipeline.ixx)
- descriptor pool creation and descriptor set allocation: [src/rhi/nrDescriptor.ixx](../src/rhi/nrDescriptor.ixx)
- descriptor write preparation through `ShaderResourceWriter`: [src/rhi/nrDescriptor.ixx](../src/rhi/nrDescriptor.ixx)
- descriptor writes through `ShaderBindingPool::update(...)`: [src/rhi/nrDescriptor.ixx](../src/rhi/nrDescriptor.ixx)
- immutable samplers are supported in pipeline layout creation: [src/rhi/nrPipeline.ixx](../src/rhi/nrPipeline.ixx)

Assessment:

- Phase 6 is already one of the strongest parts of the RHI.
- The descriptor path is more advanced than a minimal Vulkan sample because it is reflection-driven.

### Phase 7: Graphics pipeline compilation

Status: mostly supported, with one important gap for classic vertex input and one gap for depth bias exposure.

Already supported:

- Slang module/program loading and SPIR-V compilation: [src/rhi/nrSlang.ixx](../src/rhi/nrSlang.ixx)
- Vulkan shader module creation via `VkShaderProgram::create(...)`: [src/rhi/nrPipeline.ixx](../src/rhi/nrPipeline.ixx)
- graphics pipeline creation with color/depth attachment formats, culling, topology, compare op, and dynamic states: [src/rhi/nrPipeline.ixx](../src/rhi/nrPipeline.ixx)
- pipelines can be created for depth-only rendering by supplying no color formats and a depth attachment format: [src/rhi/nrPipeline.ixx](../src/rhi/nrPipeline.ixx)

Potentially missing for the requested renderer:

- `GraphicsPipelineDesc` does not expose vertex binding descriptions or vertex attribute descriptions
- `GraphicsPipeline::create(...)` builds an empty `vk::PipelineVertexInputStateCreateInfo`
- there is no exposed depth-bias configuration field in `GraphicsPipelineDesc`
- the rasterization state hardcodes `depthBiasEnable = vk::False`

Assessment:

- If the renderer uses vertex pulling, buffer device address, or a fullscreen/procedural path, current pipeline abstraction is sufficient.
- If the renderer uses classic `vkCmdBindVertexBuffers` + fixed-function vertex layout, the current pipeline abstraction is incomplete.
- For shadow mapping specifically, lack of depth bias exposure is a real missing feature because slope-scaled depth bias is a standard anti-acne tool.

Recommended missing API additions:

```cpp
std::vector<vk::VertexInputBindingDescription> vertexBindings;
std::vector<vk::VertexInputAttributeDescription> vertexAttributes;
bool depthBiasEnable = false;
float depthBiasConstantFactor = 0.0f;
float depthBiasClamp = 0.0f;
float depthBiasSlopeFactor = 0.0f;
```

### Phase 8: Vertex, index, and uniform buffers

Status: mostly supported.

Already supported:

- generic buffer creation with different memory placements: [src/rhi/nrResource.ixx](../src/rhi/nrResource.ixx)
- host-visible and device-local memory usage categories: [src/rhi/nrType.ixx](../src/rhi/nrType.ixx)
- staging-style upload/readback helpers in `UploadReadbackContext`: [src/rhi/nrResourceOps.ixx](../src/rhi/nrResourceOps.ixx)
- persistent mapped writing for CPU-visible buffers: `Buffer::write(...)`, `flush(...)`, `invalidate(...)`: [src/rhi/nrResource.ixx](../src/rhi/nrResource.ixx)

Weak spot:

- there is no high-level `createVertexBuffer(...)`, `createIndexBuffer(...)`, or `createUniformBufferPerFrame(...)` convenience layer

Assessment:

- The underlying capability exists.
- Application-side orchestration is still needed.

### Phase 9: Descriptor pool and descriptor set allocation

Status: supported.

Already supported:

- descriptor pool sizing based on reflected descriptor usage: [src/rhi/nrDescriptor.ixx](../src/rhi/nrDescriptor.ixx)
- variable descriptor count support and update-after-bind flags: [src/rhi/nrDescriptor.ixx](../src/rhi/nrDescriptor.ixx)
- descriptor-set allocation and update path: [src/rhi/nrDescriptor.ixx](../src/rhi/nrDescriptor.ixx)

Assessment:

- Phase 9 is covered.

### Phase 10: Commands and synchronization primitives

Status: supported.

Already supported:

- command pools and allocation of primary/secondary command buffers: [src/rhi/nrCommandPool.ixx](../src/rhi/nrCommandPool.ixx)
- RAII begin/end recording helpers: [src/rhi/nrCommand.ixx](../src/rhi/nrCommand.ixx)
- submit batching with `vk::SubmitInfo2`: [src/rhi/nrCommandBatch.ixx](../src/rhi/nrCommandBatch.ixx)
- queue wrappers and submission: [src/rhi/nrQueue.ixx](../src/rhi/nrQueue.ixx)
- per-frame fence and binary semaphores: [src/rhi/nrFrameContext.ixx](../src/rhi/nrFrameContext.ixx)
- image/buffer barriers and ownership transfer helpers: [src/rhi/nrResourceOps.ixx](../src/rhi/nrResourceOps.ixx)

Assessment:

- Phase 10 is already strong.

### Phase 11: Command recording for shadow pass and main pass

Status: supported, but must be implemented with dynamic rendering and explicit barriers.

Already supported:

- begin/end rendering scopes for one-pass depth-only and one-pass color+depth rendering: [src/rhi/nrResourceOps.ixx](../src/rhi/nrResourceOps.ixx)
- descriptor binding through `CursorPipelineLayout::bindDescriptorSet(...)`: [src/rhi/nrPipeline.ixx](../src/rhi/nrPipeline.ixx)
- explicit image transitions with `transitionImage(...)` and `BarrierBatch`: [src/rhi/nrResourceOps.ixx](../src/rhi/nrResourceOps.ixx)

What the renderer can do now:

- begin shadow rendering with depth attachment only
- end shadow rendering
- transition the shadow image from attachment layout to shader-read layout
- begin main rendering with swapchain color attachment plus scene depth attachment
- sample the shadow map in the main pass through a comparison sampler descriptor

Assessment:

- Phase 11 is implementable with the current RHI.

### Phase 12: Main loop and presentation

Status: mostly supported, but resize-dependent resources must be handled outside `PresentationContext`.

Already supported:

- frame begin fence wait/reset and frame-pool reset: `Device::beginFrame(...)`: [src/rhi/nrDevice.ixx](../src/rhi/nrDevice.ixx)
- acquire next image and swapchain recreation on `OutOfDate/Suboptimal`: [src/rhi/nrDevice.ixx](../src/rhi/nrDevice.ixx), [src/rhi/nrSwapchain.ixx](../src/rhi/nrSwapchain.ixx)
- submit and present flow: `submitFrame(...)`, `presentFrame(...)`, `endFrame(...)`: [src/rhi/nrDevice.ixx](../src/rhi/nrDevice.ixx)

Missing orchestration:

- there is no built-in callback or hook to recreate application-owned depth images, shadow frame resources, or per-swapchain attachments when the swapchain changes size

Assessment:

- The frame loop exists.
- The renderer still needs a higher-level resize path that rebuilds scene depth targets and any size-dependent render resources.

### Phase 13: Cleanup

Status: supported.

Already supported:

- RAII is used nearly everywhere in the RHI
- `Device::~Device()` waits for idle if the device exists: [src/rhi/nrDevice.ixx](../src/rhi/nrDevice.ixx)
- swapchain, image views, buffers, images, pipeline objects, descriptor pools, samplers, fences, semaphores, and command pools are all owned by RAII wrappers

Assessment:

- Phase 13 matches the architecture well.

## Key Functional Gaps and Risks

This section lists the most important issues that should be addressed before implementing the requested renderer on top of the current RHI.

### 1. Physical-device selection is not renderer-aware

Severity: high.

Problem:

- `selectPhysicalDevice(...)` does not evaluate present support, depth-format support, or shader-stage feature support.

Impact:

- the wrong GPU can be selected on multi-adapter systems;
- device creation or presentation can fail later than necessary;
- renderer requirements are not encoded in the selection phase.

### 2. Present queue selection is currently too rigid

Severity: high.

Problem:

- `presentQueueFamilyIndex()` returns the compute-family fallback;
- presentation is issued through `queueManager.compute()`.

Impact:

- initialization will fail on systems where graphics supports present but compute does not;
- the RHI does not currently model the common graphics/present split explicitly.

Recommended fix:

- discover a dedicated present queue family using `vkGetPhysicalDeviceSurfaceSupportKHR`
- store it separately from graphics/compute/transfer families
- present on a real present-capable queue

### 3. No helper for supported depth-format discovery

Severity: high.

Problem:

- the RHI can classify depth formats, but it cannot yet query whether a given device actually supports a candidate format for the intended image usage.

Impact:

- shadow map and scene depth setup remains partially ad hoc;
- the phase “query and choose a supported depth format” from the task is not yet implemented in the RHI.

### 4. No classic render pass / framebuffer abstraction

Severity: medium.

Problem:

- the requested phase list assumes traditional Vulkan render passes and framebuffers.
- the current RHI does not expose them.

Impact:

- existing task steps 4 and 5 must be rephrased in terms of dynamic rendering;
- if some future subsystem depends on actual `VkRenderPass` compatibility objects, new abstractions will be needed.

This is not a low-level blocker for shadow mapping, but it is an architectural translation issue.

### 5. Graphics pipeline abstraction lacks fixed-function vertex-input description

Severity: medium to high, depending on renderer style.

Problem:

- `GraphicsPipelineDesc` does not expose vertex bindings/attributes.
- the vertex-input state is always empty.

Impact:

- a classic mesh pipeline using vertex buffers and vertex attributes cannot be fully expressed through the current high-level pipeline API.

If the renderer intends to use vertex pulling or device-address-based fetch in shaders, this is less urgent.

### 6. Graphics pipeline abstraction does not expose depth bias

Severity: medium.

Problem:

- shadow mapping usually needs depth bias or slope-scaled depth bias.
- current graphics pipeline creation hardcodes `depthBiasEnable = false`.

Impact:

- higher risk of shadow acne and peter-panning problems unless pipeline creation is extended.

### 7. Resize propagation stops at the swapchain boundary

Severity: medium.

Problem:

- `PresentationContext::recreate(...)` rebuilds surface extent and swapchain,
- but application-owned depth resources and pass attachments must be manually recreated elsewhere.

Impact:

- easy source of stale image-view extent mismatches after resize.

## What Does Not Need Fundamental Change

Several parts already line up very well with the requested renderer:

- RAII resource ownership is good enough for Vulkan lifetime management.
- Synchronization2 barrier helpers are already a good fit for the shadow-image layout transition between passes.
- Sampler comparison state for hardware PCF is already supported.
- Descriptor reflection and update path are already strong enough for uniform-buffer plus combined-image-sampler workflows.
- Swapchain acquire/present and per-frame synchronization are already implemented.

## Recommended Minimal Extension Plan

To implement the requested renderer cleanly on top of the current RHI, the following additions are recommended.

### Must-have

1. Add a physical-device capability evaluator that checks:
   - surface present support
   - required device extensions
   - depth format support
   - optional shader-stage features actually used by the shader set

2. Add a real present-queue discovery path instead of reusing the compute fallback.

3. Add a helper for supported depth format selection.

4. Add depth-bias controls to `GraphicsPipelineDesc`.

### Strongly recommended

5. Add a high-level helper to recreate swapchain-dependent depth resources.

6. Add optional vertex-input layout fields to `GraphicsPipelineDesc` if the renderer will use classic vertex attributes.

### Nice to have

7. Add convenience builders for:
   - shadow depth image creation
   - scene depth image creation
   - per-frame uniform-buffer allocation
   - frame graph style pass assembly over `ScopedRendering`

## Final Verdict

The current `src/rhi` layer is already capable of serving as the base for a Vulkan shadow-mapping implementation, provided the renderer is written in the engine's current architectural style:

- dynamic rendering instead of explicit render passes/framebuffers;
- explicit synchronization2 barriers instead of subpass dependencies;
- reflection-driven descriptor handling instead of manual descriptor metadata duplication.

However, the current RHI is not yet complete enough to say it fully covers every item in the requested phase list out of the box. The most important missing pieces are:

- renderer-aware physical-device/present-queue selection,
- supported depth-format discovery,
- depth-bias exposure for the shadow pipeline,
- and possibly fixed-function vertex-input declarations if classic mesh input is desired.

If those additions are made, the current RHI should be a solid base for the requested renderer.