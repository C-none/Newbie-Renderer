# RHI Vulkan 1.4 Modernization Audit

This document covers investigation item 1 only: which Vulkan 1.4-era features
or modern baseline paths should shape `nr.rhi`, and where the current RHI still
uses an older Vulkan path.

The audit assumes the project direction is a single Vulkan backend with no
legacy graphics API abstraction. Vulkan 1.4 is already the build/runtime target
in `CMakeLists.txt` and `Device::makeInstance()`.

## Official Baseline

Primary references:

- Vulkan Guide, release summary and porting notes:
  <https://docs.vulkan.org/guide/latest/vulkan_release_summary.html>
  and <https://docs.vulkan.org/guide/latest/versions.html>
- Vulkan 1.4 reference page:
  <https://docs.vulkan.org/refpages/latest/refpages/source/VK_VERSION_1_4.html>
- Vulkan Roadmap 2024 profile:
  <https://docs.vulkan.org/spec/latest/appendices/roadmap.html>
- Dynamic rendering tutorial note:
  <https://docs.vulkan.org/tutorial/latest/courses/18_Ray_tracing/01_Dynamic_rendering.html>
- Dynamic rendering local read proposal:
  <https://docs.vulkan.org/features/latest/features/proposals/VK_KHR_dynamic_rendering_local_read.html>
- Host image copy proposal and guide:
  <https://docs.vulkan.org/features/latest/features/proposals/VK_EXT_host_image_copy.html>
  and <https://docs.vulkan.org/guide/latest/image_copies.html>
- Map memory 2 proposal:
  <https://docs.vulkan.org/features/latest/features/proposals/VK_KHR_map_memory2.html>
- Load/store op none proposal:
  <https://docs.vulkan.org/features/latest/features/proposals/VK_KHR_load_store_op_none.html>

Important baseline facts:

- Vulkan 1.4 promotes `VK_KHR_dynamic_rendering_local_read`,
  `VK_KHR_load_store_op_none`, `VK_KHR_maintenance5`,
  `VK_KHR_maintenance6`, `VK_KHR_map_memory2`, `VK_KHR_push_descriptor`,
  `VK_KHR_index_type_uint8`, `VK_KHR_line_rasterization`,
  `VK_KHR_vertex_attribute_divisor`, `VK_EXT_host_image_copy`,
  `VK_EXT_pipeline_protected_access`, and `VK_EXT_pipeline_robustness` into
  core API.
- Vulkan 1.4 introduces `VkPhysicalDeviceVulkan14Features` and
  `VkPhysicalDeviceVulkan14Properties` to expose the promoted feature/property
  surface.
- Vulkan 1.4 does not make every promoted capability equally mandatory for
  every engine path. For example, host image copy is core but optional, and
  dynamic rendering local read has optional depth/stencil and multisampled
  attachment support.
- Vulkan 1.3-era features are part of the practical Vulkan 1.4 baseline:
  dynamic rendering, synchronization2, copy commands 2, inline uniform blocks,
  and extended dynamic state should be treated as the minimum modern path unless
  there is a specific reason not to.

## Current RHI Findings

### 1. Render pass PSO: already modern

Current status: compliant.

Evidence:

- `GraphicsPipeline::create(...)` uses `vk::PipelineRenderingCreateInfo`,
  sets `createInfo.renderPass = nullptr`, and chains rendering formats through
  `pNext` in `src/rhi/nrPipeline.ixx`.
- Render recording uses `ops::ScopedRendering`, which calls
  `beginRendering(...)` / `endRendering(...)` in `src/rhi/nrResourceOps.ixx`.
- Repository search finds no RHI-owned `vk::raii::RenderPass`,
  `vk::raii::Framebuffer`, `beginRenderPass`, or render-pass-bound pipeline
  creation path.

Policy:

- Do not add a legacy `VkRenderPass` / `VkFramebuffer` pipeline path.
- If a future feature needs subpass-like behavior, use dynamic rendering local
  read instead of reintroducing render pass objects.

### 2. Vulkan 1.4 feature/property contract: implemented baseline

Current status: compliant for capability discovery.

`Device::makeDevice()` includes `vk::PhysicalDeviceVulkan14Features` in the
feature query chain and now snapshots Vulkan 1.4 features/properties beside
descriptor indexing, buffer device address, and ray tracing.

RHI-facing capability data:

- `dynamicRenderingLocalRead`
- `dynamicRenderingLocalReadDepthStencilAttachments`
- `dynamicRenderingLocalReadMultisampledAttachments`
- `maintenance5`
- `maintenance6`
- `hostImageCopy`
- host image copy layout lists and `identicalMemoryTypeRequirements`
- `pushDescriptor` and `maxPushDescriptors`
- `indexTypeUint8`
- line rasterization features/properties
- vertex attribute divisor features/properties
- pipeline robustness defaults
- `globalPriorityQuery`

Impact:

- Higher layers can inspect `Device::vulkan14Capabilities()` and
  `Device::vulkan14Properties()` before choosing a host-image-copy or
  local-read path.
- Future deferred / local-read render passes have a typed capability source for
  fail-fast gates.
- The RHI distinguishes "Vulkan 1.4 core type exists" from "this specific
  promoted feature/property is enabled and suitable for this engine path."

Policy:

- Keep optional Vulkan 1.4 paths selected per feature path until a renderer
  contract requires them globally.
- Fail fast only when a current RHI path requires a feature; otherwise expose it
  through the typed snapshots.

### 3. Core-promoted extension list avoids old core extensions

Current status: compliant.

`Device::deviceEnabledExtensions` no longer includes Vulkan 1.2 core-promoted:

- `vk::KHRShaderFloatControlsExtensionName`
- `vk::KHRSpirv14ExtensionName`

Both are Vulkan 1.2 core functionality. For a Vulkan 1.4-only device path,
requiring these as device extensions is redundant and can make diagnostics
misleading.

Recommendation:

- Remove Vulkan 1.2/1.3/1.4-promoted extension names from the required device
  extension list when the code uses the core path.
- Keep extension names only for functionality that is still not core in Vulkan
  1.4, such as swapchain, mesh shader, ray tracing pipeline, acceleration
  structure, ray query, invocation reorder, cooperative vector, extended dynamic
  state 3, and memory budget.
- Keep using unsuffixed core Vulkan-Hpp names where functionality is core.

### 4. Dynamic rendering local read is not represented

Current status: missing but not yet blocking current passes.

The RHI uses dynamic rendering but has no abstraction for Vulkan 1.4 dynamic
rendering local read:

- no `VK_IMAGE_LAYOUT_RENDERING_LOCAL_READ` / Vulkan-Hpp equivalent policy
- no attachment-location or input-attachment-index command helpers
- feature/property snapshot exists, but no render-pass contract consumes it yet
- no render-graph contract for same-dynamic-rendering-scope local dependencies

Impact:

- Current built-in passes do not need it.
- A future deferred renderer, OIT path, or tile-local attachment reuse path
  would either need ad hoc Vulkan code or might be tempted to bring back legacy
  subpasses/render passes.

Recommendation:

- Keep legacy render pass/subpass support forbidden.
- Add a narrow `nr.rhi` dynamic-rendering-local-read contract before adding
  deferred or subpass-like render passes.
- Require local read for single-sampled color attachments if that becomes part
  of the renderer contract; separately gate depth/stencil and MSAA local reads
  through Vulkan 1.4 properties.

### 5. Copy/upload/readback helpers still use older copy paths

Current status: helper internals migrated; host image copy remains separate.

Current RHI copy helpers use Vulkan 1.3 copy commands 2:

- `copyBuffer(...)` -> `copyBuffer2(...)`
- `copyBufferToImage(...)` -> `copyBufferToImage2(...)`
- `copyImageToBuffer(...)` -> `copyImageToBuffer2(...)`
- `copyImageToImage(...)` -> `copyImage2(...)`

`UploadReadbackContext::uploadImage(...)` and `readbackImage(...)` always use a
mapped staging/readback buffer plus command-buffer copy. The Vulkan Guide lists
three generations of image copy paths: Vulkan 1.0 copy commands,
`VK_KHR_copy_commands2` in Vulkan 1.3, and `VK_EXT_host_image_copy` in Vulkan
1.4 for host copies without a `VkBuffer` or `VkCommandBuffer`.

Impact:

- The current path is valid and good for device-local uploads on discrete GPUs.
- It is not the full Vulkan 1.4 streaming-transfer contract because there is no
  host-image-copy option when `hostImageCopy` is available and desirable.
- The command helpers are now pNext-extensible through copy commands 2, while
  retaining narrow compatibility adapters for local call sites that still pass
  original region structs.

Recommendation:

- Keep helper internals on copy commands 2 (`copyBuffer2`,
  `copyBufferToImage2`, `copyImageToBuffer2`, `copyImage2`) so future pNext
  usage has a typed path.
- Add a separate host-image-copy path for image upload/readback, gated by
  `hostImageCopy`, supported layouts, image usage
  `eHostTransfer`, and measured target-platform behavior.
- Do not blindly replace all staging uploads with host image copy. The official
  proposal explicitly notes that CPU image swizzling and memory-type constraints
  can make host image copy slower or less suitable unless the platform benefits.

### 6. Load/store op none is not exposed as a first-class policy

Current status: migration candidate.

`RenderingAttachmentDesc` defaults to `eLoad`/`eStore`, and built-in passes
mostly use `eClear`/`eStore`. `NormalBuffer` uses `eDontCare` for stencil.
The RHI does not expose a policy for Vulkan 1.4 `VK_ATTACHMENT_LOAD_OP_NONE` /
`VK_ATTACHMENT_STORE_OP_NONE`.

Impact:

- Current color-producing passes are correct because they clear and store.
- Future read-only, preserved, or conditionally unused attachments may pay
  unnecessary bandwidth or may use `DontCare` where `None` would better express
  "no load/store operation, no write dependency."

Recommendation:

- Add explicit RHI attachment intent helpers such as `clearAndStore`,
  `loadAndStore`, `preserveWithoutLoadStore`, or `unused`.
- Map preserve/unused cases to `eNone` where legal.
- Keep pass code from choosing raw load/store operations unless it has a clear
  attachment lifetime reason.

### 7. Extended dynamic state is only partially treated as pipeline identity policy

Current status: partial.

The RHI enables `VK_EXT_extended_dynamic_state3` and appends dynamic states for
cull mode, front face, topology, depth state, polygon mode, and rasterization
samples in `PipelineService::createGraphicsPipeline(...)`.

However, `GraphicsPipelineDesc` still carries those fields as pipeline creation
state. This is legal because Vulkan still needs initial values for many dynamic
states, but it can become an old PSO-style design if cache keys or call sites
treat those fields as reasons to create separate pipelines.

Impact:

- Current code can dynamically set raster state through `mesh::applyRasterState`.
- The abstraction does not yet make "dynamic state is not pipeline identity"
  explicit.

Recommendation:

- Document or encode which `GraphicsPipelineDesc` fields are true pipeline
  identity and which are initial dynamic-state defaults.
- Avoid creating distinct pipelines for cull/front/topology/depth/polygon/sample
  choices when the dynamic state path is active.
- Keep render-pass-bound PSO variants forbidden; current code already satisfies
  this via dynamic rendering.

### 8. Push descriptors are not used

Current status: optional, not a mandatory migration.

Vulkan 1.4 promotes push descriptors and gates them through
`VkPhysicalDeviceVulkan14Features::pushDescriptor`. The current RHI uses
reflection-generated descriptor set layouts, descriptor pools, allocated
descriptor sets, update-after-bind, and runtime-sized descriptor arrays.

Impact:

- The current design is appropriate for bindless/resource-array workflows.
- Push descriptors may reduce overhead for very small one-shot bindings, but
  they do not replace descriptor indexing or runtime arrays.

Recommendation:

- Do not replace the shader-cursor descriptor-set path globally.
- Consider a narrow push-descriptor fast path only for small transient bindings
  after measuring. It must still be driven by Slang reflection and must not
  create a second manual binding system in render passes.

### 9. Index type uint8, line rasterization, vertex divisor, shader 1.4 features

Current status: exposed as capabilities but not used by current paths.

The RHI currently defaults scene geometry to `vk::IndexType::eUint32` and ImGui
to `eUint16`/`eUint32`. There is no `eUint8` geometry path. Line rasterization,
vertex divisor, subgroup rotate, float-controls2, and expect/assume support are
visible through `Device::vulkan14Capabilities()` /
`Device::vulkan14Properties()`, but current geometry, line-rendering, and
shader compilation paths do not consume them.

Impact:

- These are not blockers for current triangle/mesh/UI/normal-buffer passes.
- They matter when introducing compressed index buffers, line rendering tools,
  instanced vertex input, or shader code that wants the new subgroup/compiler
  guarantees.

Recommendation:

- Add concrete runtime paths only when a feature uses them.
- When adding shader 1.4 usage, route it through Slang compile/runtime policy,
  not through ad hoc pass-local Vulkan checks.

## Prioritized Actions

Completed:

1. Added `Vulkan14CapabilitySnapshot` and `Vulkan14PropertySnapshot` to
   `Device`.
2. Cleaned `deviceEnabledExtensions` so core-promoted Vulkan 1.2+ extension
   names are not treated as required device extensions in the 1.4-only path.
3. Migrated copy helper wrappers to copy commands 2.

Remaining:

1. Add optional host-image-copy upload/readback routes behind explicit feature,
   layout, usage, and performance gates.
2. Add load/store op none attachment intent helpers.
3. Define the dynamic-rendering-local-read RHI contract before implementing any
   deferred/subpass-like render path.
4. Make graphics pipeline identity explicit so dynamic states do not create old
   PSO-style variants.

## Hard Rules From This Audit

- Do not add old render pass/framebuffer-backed pipeline support.
- Do not add compatibility fallbacks for Vulkan versions below 1.4 in `nr.rhi`.
- Do not expose Vulkan 1.4 features directly from render passes; add typed RHI
  wrappers/capability snapshots first.
- Do not replace the existing Slang-reflection descriptor path with manual
  descriptor code. Optional push-descriptor support must still be reflection-led.
