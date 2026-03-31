# UI Runtime Integration and Descriptor Indexing Plan

## Scope

This document is the execution handoff for the `UiNode` runtime work. It covers:

- the performance problems that motivated the change
- the RHI groundwork already landed for `descriptor_indexing` and `buffer_device_address`
- the current `UiNode` bindless texture path that is already implemented
- the remaining optimization work that should be finished next
- the concrete files, invariants, and validation steps another agent should follow

This document describes current code plus the accepted next-step direction. It does not replace the source of truth in code.

## Why This Work Exists

`UiNode` had two major cost centers:

1. UI texture management was allocation-heavy and synchronization-heavy.
   - The old path created temporary staging resources for texture uploads.
   - The old path updated textures with immediate queue submission and hard synchronization.
   - In captures this showed up as too many allocation events, including downstream `vkAllocateMemory` activity when VMA needed new blocks.

2. Descriptor binding scaled with texture count and draw count.
   - The previous shader model used one texture binding at a time.
   - That forced repeated descriptor updates and repeated descriptor-set binds for multi-texture UI.

The design direction approved for this project is:

- use `descriptor_indexing` to manage UI textures as a runtime-sized sampled-image array
- bind the descriptor table once per pass
- push the current texture slot through push constants per draw
- keep `buffer_device_address` available as the low-level tool for future optional GPU-managed array metadata paths, but do not force UI textures onto a BDA-only design

## Status Summary

### Already Landed

The following foundation is now in code:

1. RHI feature validation and capability snapshots
   - `src/rhi/nrDevice.ixx`
   - Device creation now requires:
     - `descriptorIndexing`
     - `runtimeDescriptorArray`
     - `descriptorBindingPartiallyBound`
     - `descriptorBindingVariableDescriptorCount`
     - `descriptorBindingSampledImageUpdateAfterBind`
     - `descriptorBindingUpdateUnusedWhilePending`
     - `bufferDeviceAddress`
   - `Device` now exposes:
     - `descriptorIndexingCapabilities()`
     - `bufferDeviceAddressCapabilities()`

2. Descriptor-layout policy support for runtime-sized bindings
   - `src/rhi/nrDescriptor.ixx`
   - `DescriptorBindingPolicy` now carries `defaultRuntimeDescriptorCount`.
   - `DescriptorBindingInfo` now records whether a binding is runtime-sized.
   - Unbounded Slang descriptor bindings can now map to:
     - variable descriptor count
     - partially bound
     - update-after-bind

3. Variable descriptor-count allocation and update bounds
   - `src/rhi/nrDescriptor.ixx`
   - `ShaderBindingPool` now tracks descriptor capacity per set and per binding.
   - `ShaderBindingSet` now records the allocated descriptor capacity for variable-count bindings.
   - descriptor writes validate against allocated capacity instead of only layout default capacity.
   - the current implementation supports at most one variable descriptor-count binding per set, and it must be the largest binding number in that set.

4. `ShaderCursor` support for runtime descriptor arrays
   - `src/rhi/nrDescriptor.ixx`
   - `ShaderCursor` can now:
     - report `bindingDescriptorCount()`
     - report `referencesRuntimeDescriptorArray()`
     - index resource descriptor arrays such as `Texture2D[]`
   - There is also a compatibility path for Slang reflection shapes that expose descriptor arrays without a normal `elementTypeLayout`.

5. Pipeline creation can opt into runtime descriptor-array policy
   - `src/rhi/nrPipeline.ixx`
   - `GraphicsPipelineDesc`, `ComputePipelineDesc`, and `RayTracingPipelineDesc` now expose `descriptorBindingPolicy`.
   - `allocateBindingSetsForLayout(...)` now has an overload that takes per-set variable descriptor counts.

6. Reflection lifetime is now preserved in pipeline state
   - `src/rhi/nrPipeline.ixx`
   - `PipelineState` now keeps a `SlangProgram` alive.
   - This is important because `ShaderDescriptorLayout` stores reflection-derived raw pointers. Without the retained `SlangProgram`, later `ShaderCursor` usage becomes a dangling-pointer hazard.

7. `buffer_device_address` is easier to consume from normal code
   - `src/rhi/nrResource.ixx`
   - `Buffer::deviceAddress()` is now `const`.
   - The cached device address is now stored in a mutable cache, which makes BDA-based lookup tables easier to integrate without needing mutable buffer references everywhere.

8. UI shader and `UiNode` now use descriptor indexing
   - `shader/renderer/appUi.slang`
   - `src/renderPasses/Ui/nrUiNode.ixx`
   - Shader model is now:
     - `[[vk::binding(0, 0)]] SamplerState gUiSampler;`
     - `[[vk::binding(1, 0)]] Texture2D<float4> gUiTextures[];`
   - Push constants now include `textureIndex`.
   - `UiNode` now:
     - allocates stable texture slots
     - maintains one bindless descriptor table per frame slot
     - binds descriptor sets once per pass
     - updates push constants per draw when texture slot changes

9. Per-frame vertex/index buffers now grow geometrically instead of exact-fit growth
   - `src/renderPasses/Ui/nrUiNode.ixx`
   - This keeps the existing persistent-buffer model but reduces realloc frequency under slowly growing UI workloads.

### Verified

Current validation that was run after the changes:

- `cmake --build build --config Debug --target ALL_BUILD --parallel 4`
- `ctest --test-dir build -C Debug -R nr_normal_buffer_ui_smoke_test --output-on-failure`
- repeated smoke runs after the reflection lifetime fix

## Current Runtime Design

### Descriptor-Indexing Path

Current high-level flow:

1. Dear ImGui emits `ImTextureData` requests.
2. `UiNode` creates or updates `nr::rhi::Image` entries in `UiRuntimeCache::textures`.
3. Each `ImTextureID` gets a stable integer slot through `textureSlotById`.
4. For each frame slot, `UiNode` allocates one descriptor set with a variable descriptor-count binding for `gUiTextures`.
5. `UiNode` writes:
   - `gUiSampler`
   - `gUiTextures[slot]` for every live texture slot
6. Record stage binds the frame's descriptor set once.
7. Each draw command pushes `UiPushConstants.textureIndex` before draw if the slot changed.

### Why This Design

This fits the current engine better than `VK_EXT_descriptor_buffer` because:

- the project already has a reflection-driven `shaderCursor` and descriptor-set pathway
- the current render-pass contract requires resources and push constants to flow through the cursor-based system
- the project policy explicitly prefers Vulkan-Hpp RAII member functions and thin RHI wrappers over raw extension command dispatch in render-pass code

Descriptor indexing integrates with the current engine architecture. Descriptor buffer would require a more invasive binding model change.

## File Map

### RHI

- `src/rhi/nrDevice.ixx`
  - feature enable and capability snapshot source of truth
- `src/rhi/nrDescriptor.ixx`
  - descriptor binding policy
  - runtime-sized descriptor layout support
  - variable-count descriptor allocation/update
  - resource-array cursor addressing
- `src/rhi/nrPipeline.ixx`
  - pipeline-desc policy inputs
  - binding-set allocation overloads
  - retained `SlangProgram` lifetime in `PipelineState`
- `src/rhi/nrResource.ixx`
  - `Buffer::deviceAddress()` convenience for BDA consumers

### UI Pass

- `shader/renderer/appUi.slang`
  - bindless sampled-image array ABI
- `src/renderPasses/Ui/nrUiNode.ixx`
  - bindless descriptor-table runtime
  - texture-slot allocator
  - per-draw texture push constant
  - geometric vertex/index growth

### Tests

- `test/app/normalBufferUiSmoke.cpp`
  - current smoke coverage for `NormalBuffer + Ui -> Present`

## Important Invariants

Another agent should preserve these invariants:

1. `gUiTextures` stays the last binding in its descriptor set.
   - Current variable descriptor-count support assumes that Vulkan rule.

2. Only one variable descriptor-count binding per set is supported by the current allocator/update path.
   - If future work needs more than one, extend `ShaderBindingPool` first.

3. `PipelineState` must keep the `SlangProgram` alive as long as `ShaderDescriptorLayout` and any `ShaderCursor` derived from it can still be used.

4. Render passes must continue to bind through:
   - `bindResourcesToCommandBuffer(...)`
   - `pushConstantsToCommandBuffer(...)`
   - `CursorPipelineLayout` helper methods
   and not reintroduce raw descriptor binding logic outside the reflection-driven route.

5. `buffer_device_address` is a tool, not the default UI texture binding path.
   - UI texture sampling should stay on descriptor indexing.
   - BDA is reserved for optional metadata tables, indirection buffers, or future non-texture array management.

6. Do not switch this work to `VK_EXT_descriptor_buffer` unless the whole binding model is intentionally being redesigned.

## Remaining Work

~~The biggest remaining cost is still texture upload behavior.~~ **This section has been largely completed.**

The bindless descriptor path reduces descriptor churn. The upload path has now been migrated to `UploadReadbackContext`.

### 1. Replace Synchronous UI Texture Uploads with `UploadReadbackContext` ✅ COMPLETED

#### Why

The current `createOrUpdateUiTexture(...)` path in `src/renderPasses/Ui/nrUiNode.ixx` ~~still~~ previously:

- ~~creates~~ created temporary staging buffers
- ~~creates~~ created temporary command pools/command buffers
- ~~submits~~ submitted immediate copy work
- ~~forces~~ forced synchronization too aggressively

~~This remains the main performance issue~~ This was the main performance issue, especially when the font atlas or runtime UI textures update.

#### Implementation (Completed)

The upload path now uses `UploadReadbackContext::uploadImage(...)`:

- `uploadUiTextureAsync()` - Async upload using UploadReadbackContext
- `waitForPendingUiTextureUploads()` - Waits for timeline completion
- `makeUiTextureUploadOwnershipPlan()` - Creates proper ownership transfer

Key data structures added:
- `UiTextureEntry::pendingUpload` - Stores upload ticket per texture
- Uses `device.uploadReadback()` for the upload context

#### Validation ✅

- No per-update temporary command pool creation
- No per-update `waitIdle` - uses timeline semaphore wait instead
- All smoke tests pass

### 2. Implement Partial Texture Updates for `ImTextureData::WantUpdates`

#### Why

Dear ImGui 1.92 texture flow distinguishes:

- create
- partial update
- destroy

The current `UiNode` path still treats `WantUpdates` too much like a whole-texture upload.

#### Target Design

For `WantUpdates`:

- use `tex->UpdateRect`
- upload only the dirty pixel rectangle
- issue a `vk::BufferImageCopy` or upload-context equivalent for the updated region

#### Concrete Steps

1. Check the Dear ImGui texture request fields used by the vcpkg backend version in this project.
2. Extract dirty-region bounds.
3. Compute row pitch and sub-rectangle byte span correctly.
4. Feed the rectangle into `UploadReadbackContext::uploadImage(...)` or a thin helper around it.
5. Keep the existing texture slot stable. Partial updates must not invalidate the slot mapping.

#### Validation

- runtime font atlas edits should no longer copy the whole image
- descriptor slot remains stable across updates
- no redundant descriptor-table invalidation for pure subresource upload when image identity is unchanged

### 3. Add Safe Deferred Destruction for UI Textures ✅ COMPLETED

#### Why

Immediate destroy is risky once uploads and sampling become more asynchronous.

#### Implementation (Completed)

Deferred destruction is now implemented:

- `UiRetiredTexture` struct stores image, slot, and `retiredFrameIndex`
- `UiRuntimeCache::retiredTextures` queue holds pending destructions
- `cleanupRetiredTextures()` reclaims slots after `maxFrameInFlight + 1` frames
- Slot recycling is deferred until retirement is complete

#### Validation ✅

- Repeated create/destroy of UI images does not cause use-after-free
- Descriptor slot reuse only happens after retirement completes
- All smoke tests pass

### 4. Reduce Descriptor-Table Rewrite Cost

#### Current State

The current bindless table is much better than the old per-texture/per-draw binding path, but it still rewrites the table for a frame when initialization is invalidated.

#### Recommended Next Step

Introduce dirty-slot tracking:

- keep descriptor table per frame slot
- only rewrite sampler once
- only rewrite slots whose image identity changed
- keep a per-frame "written capacity" / "dirty slots" structure

This is not as urgent as the upload-path rewrite, but it is the logical next refinement.

### 5. Reuse CPU Scratch Storage for Copied UI Draw Data

#### Why

`copyUiDrawData(...)` still copies vertices, indices, and commands into `std::vector` containers every frame.

#### Recommended Direction

- reuse `UiFrameDrawData` scratch buffers per frame slot
- reserve geometrically
- avoid unnecessary `std::vector` reallocations
- keep the current simple copy model unless profiling shows a need to switch to direct mapped writes

### 6. Prepare Optional `buffer_device_address` Array Infrastructure

#### Intent

This is not required for UI textures themselves. It is for optional future GPU-side arrays such as:

- metadata arrays parallel to descriptor-indexed textures
- material tables
- draw record arrays
- bindless-like lookup structures stored in storage buffers

#### What Already Exists

- `Device` now validates and exposes BDA support
- `Buffer::deviceAddress()` is now easy to call from const code

#### What Still Needs to Be Added

If another agent wants to build the BDA tooling next, add:

1. a typed helper for GPU-addressable structured arrays in `nr.rhi`
2. optional reflection-aware cursor helpers for plain scalar address values when they are carried in uniform/storage data
3. validation helpers for alignment and stride rules
4. a small policy layer for when to prefer:
   - descriptor-indexed textures
   - descriptor-indexed buffers
   - BDA metadata arrays

#### Important Constraint

Do not replace texture sampling descriptors with raw addresses. Images still need descriptor-backed sampling in Vulkan. BDA is best used for auxiliary arrays and indirection data.

## Recommended Execution Order

If another agent continues this work, the safest order is:

1. Finish upload-path migration to `UploadReadbackContext`
2. Add partial dirty-rect updates
3. Add deferred destroy/retirement
4. Add descriptor-table dirty-slot tracking
5. Add CPU scratch reuse
6. Add optional BDA array utilities

This order yields the best performance return early while minimizing the risk of destabilizing the working descriptor-indexing path.

## Verification Checklist

### Build

```powershell
cmake --build build --config Debug --target ALL_BUILD --parallel 4
```

### Current Smoke Test

```powershell
ctest --test-dir build -C Debug -R nr_normal_buffer_ui_smoke_test --output-on-failure
```

### Recommended New Tests

1. Add a focused RHI test for runtime descriptor-array cursor indexing.
   - verify `root["gUiTextures"][n]` produces a valid cursor
   - verify descriptor array element indices are preserved in snapshot/write resolution

2. Add a focused RHI test for variable descriptor-count allocation bounds.
   - verify `ShaderBindingPool::update(...)` rejects out-of-range writes beyond allocated count

3. Add a UI smoke case with more than one texture.
   - verify multiple draw commands sample different slots in the same pass

4. Add a UI smoke case with texture updates.
   - verify create, update, and destroy all work without descriptor-table corruption

## Risks and Known Sharp Edges

1. Reflection lifetime is easy to break.
   - Any refactor that removes `PipelineState::reflectionProgram` without replacing it with another ownership model can reintroduce dangling reflection pointers.

2. Runtime-sized descriptor arrays require the binding-order invariant.
   - Vulkan variable descriptor-count bindings must remain last in the set.

3. Upload-path optimization changes synchronization semantics.
   - When replacing the current synchronous upload path, lifetime and visibility must be revalidated carefully.

4. Descriptor invalidation can silently become too broad.
   - Keep identity changes, content changes, and slot-allocation changes separate.

5. UI texture destroy must become timeline-safe before upload path is made fully asynchronous.

## External References

- Slang `DescriptorHandle<T>` reference: <https://docs.shader-slang.org/en/latest/external/core-module-reference/types/descriptorhandle-0a/index.html>
- Slang convenience features and bindless-related notes: <https://docs.shader-slang.org/en/latest/external/slang/docs/user-guide/03-convenience-features.html>
- Vulkan descriptor set and descriptor indexing rules: <https://docs.vulkan.org/spec/latest/chapters/descriptorsets.html>
- Vulkan descriptor buffer guide, for contrast with the chosen design: <https://docs.vulkan.org/guide/latest/descriptor_buffer.html>
- Dear ImGui backend texture-management notes: <https://github.com/ocornut/imgui/blob/master/docs/BACKENDS.md>
- Dear ImGui Vulkan backend reference implementation: <https://github.com/ocornut/imgui/blob/master/backends/imgui_impl_vulkan.cpp>

## Code Reference Index

- `src/rhi/nrDevice.ixx`
- `src/rhi/nrDescriptor.ixx`
- `src/rhi/nrPipeline.ixx`
- `src/rhi/nrResource.ixx`
- `src/rhi/nrResourceOps.ixx`
- `src/renderPasses/Ui/nrUiNode.ixx`
- `shader/renderer/appUi.slang`
- `test/app/normalBufferUiSmoke.cpp`
