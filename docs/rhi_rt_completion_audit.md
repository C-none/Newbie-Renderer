# RHI Ray Tracing Completion Audit

This document covers investigation item 2 only: ray tracing functionality that
is present in `nr.rhi` but still partial or not connected into a complete
runtime path.

The audit assumes a single Vulkan backend. It does not propose D3D/Metal
fallbacks or vendor fallback paths. The goal is to identify the remaining
Vulkan ray tracing work needed before the engine can treat RT as a practical
renderer feature rather than a set of low-level wrappers.

## Official Baseline

Primary references:

- Vulkan Guide ray tracing overview:
  <https://docs.vulkan.org/guide/latest/extensions/ray_tracing.html>
- Vulkan acceleration structure specification chapter:
  <https://docs.vulkan.org/spec/latest/chapters/accelstructures.html>
- Vulkan ray tracing specification chapter:
  <https://docs.vulkan.org/spec/latest/chapters/raytracing.html>
- `VK_KHR_acceleration_structure` reference:
  <https://docs.vulkan.org/refpages/latest/refpages/source/VK_KHR_acceleration_structure.html>
- `VK_KHR_ray_tracing_pipeline` reference:
  <https://docs.vulkan.org/refpages/latest/refpages/source/VK_KHR_ray_tracing_pipeline.html>
- `VK_KHR_ray_query` reference:
  <https://docs.vulkan.org/refpages/latest/refpages/source/VK_KHR_ray_query.html>
- `VK_KHR_ray_tracing_maintenance1` reference:
  <https://docs.vulkan.org/refpages/latest/refpages/source/VK_KHR_ray_tracing_maintenance1.html>

Important baseline facts:

- Vulkan ray tracing is not one extension. The core practical feature set is
  built from `VK_KHR_acceleration_structure`,
  `VK_KHR_ray_tracing_pipeline`, `VK_KHR_ray_query`,
  `VK_KHR_pipeline_library`, and `VK_KHR_deferred_host_operations`.
- Acceleration structures are required by both ray tracing pipelines and ray
  queries. The AS API surface includes build/update, device or host build size
  queries, copy, compact, serialize, deserialize, and property queries.
- `VK_KHR_ray_tracing_pipeline` adds RT shader stages, shader groups, shader
  binding tables, trace commands, pipeline-library integration, shader group
  handle queries, and stack-size controls.
- The shader binding table is application-allocated buffer memory. Each SBT
  record contains the shader group handle plus optional application-visible
  record data that shaders can read through `ShaderRecordBufferKHR`.
- `VK_KHR_ray_query` is shader functionality. It enables ray queries from
  graphics, compute, or RT shaders but adds no separate dispatch command.
- `VK_KHR_ray_tracing_maintenance1` is not the base RT requirement, but it is
  relevant to a modern RT RHI because it adds acceleration-structure copy/SBT
  synchronization2 bits, additional AS query types, and
  `vkCmdTraceRaysIndirect2KHR`.

## Current RHI Surface

### 1. Device feature and extension contract

Current status: present for the required KHR baseline used by the engine.

Evidence:

- `src/rhi/nrDevice.ixx` enables and requires
  `VK_KHR_acceleration_structure`, `VK_KHR_ray_tracing_pipeline`,
  `VK_KHR_pipeline_library`, `VK_KHR_ray_query`,
  `VK_KHR_deferred_host_operations`, and NVIDIA RT-related features such as
  `VK_EXT_ray_tracing_invocation_reorder`.
- `Device` snapshots RT pipeline properties needed by SBT creation,
  `traceRays` validation, capture/replay handles, and
  `VK_KHR_ray_tracing_maintenance1` indirect2 support in
  `RayTracingCapabilitySnapshot`.
- AS limits are queried separately through
  `queryAsBuildLimits(...)` in `src/rhi/nrAccelerationStructure.ixx`.

Assessment:

- The base RT extensions are assumed, which matches the fail-fast target
  policy.
- The capability model is still split. Pipeline dispatch/SBT limits and
  maintenance1 feature bits live in `RayTracingCapabilitySnapshot`, while AS
  limits require a separate call. There is still no unified device-level
  snapshot for every AS feature bit and optional modern RT extension.

### 2. Acceleration structure resource wrapper

Current status: basic RAII wrapper exists.

Evidence:

- `AccelerationStructureResource` wraps `vk::raii::AccelerationStructureKHR`
  and links the AS handle to a caller-provided storage `Buffer`.
- It validates storage buffer usage and exposes `raw()`, `storageBuffer()`,
  `size()`, `type()`, and `deviceAddress()`.

Assessment:

- Ownership of the Vulkan AS handle is RAII-compliant.
- Allocation policy is still external. The caller must query size, create
  storage, create scratch, pick offsets, track compaction output, and manage
  lifetime ordering. That is workable for tests and experiments, but not a
  complete renderer-facing AS subsystem.

### 3. BLAS and TLAS build/update recording

Current status: direct BLAS/TLAS build wrappers exist, including
multi-geometry BLAS input.

Evidence:

- `queryBlasBuildSizes(...)` and `queryTlasBuildSizes(...)` call
  `getAccelerationStructureBuildSizesKHR(...)`.
- `recordBuildBlas(...)`, `recordBuildBlasGeometries(...)`, and
  `recordBuildTlas(...)` record `buildAccelerationStructuresKHR(...)`.
- `recordBuildAccelerationStructuresIndirect(...)` forwards to
  `buildAccelerationStructuresIndirectKHR(...)` for caller-prepared indirect
  build records.
- `recordUpdateBlas(...)` and `recordUpdateTlas(...)` reuse the same build
  path with update mode.
- Validation covers scratch alignment, required buffer usage, BLAS/TLAS type,
  indexed or non-indexed triangle input, TLAS instance input, and update
  source/destination requirements.

Assessment:

- This is now a usable low-level RT area for triangle and AABB BLAS geometry,
  TLAS instance geometry, direct update paths, and indirect AS build recording.
- The remaining gap is ownership/orchestration: direct batch build APIs for
  several AS objects, storage/scratch allocation policy, compaction scheduling,
  and renderer-facing instance construction are still outside this layer.

### 4. Ray tracing pipeline and SBT

Current status: usable but narrow.

Evidence:

- `RayTracingPipelineDesc` and `RayTracingPipeline::create(...)` support RT
  entry points, shader groups, recursion depth, pipeline flags, pipeline
  library creation/linking, and `RayTracingPipelineInterfaceCreateInfoKHR`.
- `PipelineService::createRayTracingPipeline(...)` validates recursion depth
  against `Device::rayTracingCapabilities()`.
- `ShaderBindingTable::create(...)` validates section alignment and creates a
  host-visible SBT buffer for raygen, miss, hit, and callable sections.
- `traceRays(...)` and `traceRaysIndirect(...)` bind the RT pipeline and call
  Vulkan-Hpp RAII command buffer RT tracing methods.

Assessment:

- RT pipeline creation and dispatch are present.
- SBT construction supports shader group handles plus per-record byte payloads
  and non-contiguous shader group indices.
- `RayTracingPipeline` exposes shader group stack-size and capture/replay
  handle queries, and trace helpers can record dynamic pipeline stack size.
- `traceRaysIndirect(...)` supports the base KHR indirect command, and
  `traceRaysIndirect2(...)` supports the maintenance1 path where SBT regions
  and dispatch dimensions are sourced from the device.

### 5. Descriptor and shader ABI support

Current status: descriptor ABI is present.

Evidence:

- `src/rhi/nrDescriptor.ixx` maps Slang
  `RayTracingAccelerationStructure` to
  `vk::DescriptorType::eAccelerationStructureKHR`.
- `ShaderCursor::setObject(vk::AccelerationStructureKHR)` writes AS
  descriptor records through the existing reflection-backed binding path.
- `shader/test/rt/minimalRtTriangle.slang` uses a
  `RaytracingAccelerationStructure`, an output image, and raygen/miss/closest
  hit stages.

Assessment:

- The descriptor ABI is in good shape for RT pipeline and ray query shaders.
- There is still no higher-level bridge that binds a scene TLAS to an actual
  RT render node through the normal render-pass callback path.

### 6. Scene, renderer, and render graph integration

Current status: data packets exist, runtime RT pass does not.

Evidence:

- `src/scene/nrSceneType.ixx` defines `RayTracingInstancePacket` and
  `TlasBuildInputPacket`.
- `src/scene/nrScene.ixx` can extract `rtInstances` and `tlasBuildInputs`.
- `src/renderer/nrRenderer.ixx` reports `sceneTlasPacketCount`.
- `src/renderer/nrRendererType.ixx` defines AS/SBT buffer usage and access
  intents.
- `src/renderer/nrRenderGraphCompiler.ixx` maps AS/SBT buffer usage intents to
  Vulkan usage flags.

Assessment:

- The renderer recognizes AS-related buffer intents, and the scene can produce
  TLAS-oriented packet data.
- There is no observed render graph node that owns BLAS/TLAS resources,
  schedules AS builds, inserts AS build/copy/trace barriers, constructs SBTs,
  binds an RT output target, and records `traceRays(...)`.

## Completion Gaps

### 1. AS copy, compaction, serialization, and property query are low-level complete

Severity: remaining risk is medium, mostly orchestration and tests.

Evidence:

- `recordCopyAccelerationStructure(...)`,
  `recordCopyAccelerationStructureToMemory(...)`, and
  `recordCopyMemoryToAccelerationStructure(...)` forward to Vulkan-Hpp RAII
  AS copy commands.
- Host/device copy helpers expose the corresponding Vulkan-Hpp device
  functions and optional deferred operation handles.
- AS property helpers cover compacted size, serialization size, maintenance1
  device-timeline size, and serialized BLAS pointer count.
- `AsBuildOptions` accepts arbitrary Vulkan AS build flags, including flags
  that can imply later compaction; the low-level compacted-size query and
  compact-copy path now exist, but no renderer policy schedules them yet.

Remaining coverage:

- Renderer-side scheduling of compact-copy and old-storage retirement.
- Serialization cache policy, compatible storage allocation, and persistence
  ownership.
- GPU tests for query/write/copy/compact paths.

Impact:

- The RHI can express the Vulkan copy/query operations, but callers still need
  a higher-level builder to schedule compaction and storage replacement safely.

Recommended fix:

- Add renderer/builder policy around `eAllowCompaction`, query timing, compact
  destination allocation, and source AS retirement.

### 2. AS build model still lacks renderer-facing batch/instance policy

Severity: high for production RT scenes, medium for the current minimal sample.

Evidence:

- `BlasGeometryLayout` and `BlasGeometryInput` describe one triangle geometry
  for the convenience path.
- `BlasGeometryRecord` plus `recordBuildBlasGeometries(...)` support
  multi-geometry triangle/AABB BLAS input with one range per geometry.
- `recordBuildTlas(...)` writes one instance geometry.

Remaining API/runtime coverage:

- Direct batch recording for several BLAS/TLAS objects in one wrapper call.
- Per-instance full `VkAccelerationStructureInstanceKHR` construction policy,
  including SBT record offset, custom index, mask, flags, and BLAS reference.

Impact:

- Meshes with multiple submeshes/material-hit groups can be represented in the
  low-level BLAS wrapper, but no renderer AS builder maps scene mesh/material
  batches into those records yet.
- Procedural AABB geometry is available at the RHI command-wrapper level, but
  no renderer-facing primitive/instance policy consumes it yet.

Recommended fix:

- Add a typed instance-builder helper that produces
  `vk::AccelerationStructureInstanceKHR` from scene packets and SBT layout
  decisions.
- Add direct batch build recording if production build scheduling needs to pack
  several AS objects into one helper call.

### 3. AS allocation, scratch, and lifetime orchestration are external

Severity: high for renderer integration.

Evidence:

- `AccelerationStructureResource::create(...)` requires the caller to provide
  storage buffer and offset.
- `BlasBuildRecordInfo` and `TlasBuildRecordInfo` require caller-provided
  scratch buffer and scratch device address.
- `queryBlasBuildSizes(...)` and `queryTlasBuildSizes(...)` only return sizes.

Missing RHI/runtime coverage:

- A renderer-facing AS builder that allocates storage and scratch from RHI
  resource facilities.
- Scratch reuse policy across BLAS/TLAS builds.
- Storage replacement for update vs rebuild vs compact.
- Lifetime ownership for BLAS per mesh/submesh and TLAS per frame/view/bucket.
- Clear ownership between scene packets, resource module mesh data, renderer
  AS cache, and render graph transient resources.

Impact:

- Callers can record a build only after doing all memory planning manually.
- There is no standard place to implement compaction, scratch pooling, rebuild
  heuristics, or BLAS/TLAS cache invalidation.

Recommended fix:

- Add a renderer-facing AS cache/builder outside the low-level command wrapper.
- Keep `nr.rhi` as the RAII and command-recording layer, but expose enough typed
  helpers for the renderer to allocate storage/scratch without duplicating
  Vulkan details.

### 4. Synchronization has RHI helpers but is not automated by renderer scheduling

Severity: medium to high.

Evidence:

- `recordBuildBlas(...)` and `recordBuildTlas(...)` document that queue-local
  hazards and queue-family ownership transfers are caller responsibilities.
- `appendAsSubmitIntent(...)` only appends wait/signal metadata to
  `CommandBatch`.
- RHI `ops` exposes sync2 scopes/barriers for AS build/copy, RT shader AS read,
  and maintenance1 SBT read access.
- Render graph buffer intents include AS read/write and SBT usage, but no RT
  node currently exercises AS build, AS copy, SBT read, or trace hazards.

Missing coverage:

- Integration of AS build/copy and SBT read stage/access into render graph
  scheduling.
- RT node usage that proves the helper path in a real frame.

Impact:

- Correct execution still depends on callers choosing the right helper until
  renderer scheduling owns these hazards.
- Once an RT pass is added, missing barriers can appear as intermittent
  traversal or SBT-data faults.

Recommended fix:

- Thread AS build/copy and SBT read access through render graph compiler rules
  before adding a production RT node.

### 5. SBT records carry byte payloads; typed layout policy is still external

Severity: medium.

Evidence:

- `ShaderBindingTable::create(...)` copies shader group handles and optional
  byte-span record payload data into each record.
- `ShaderBindingTableSectionDesc::records` can select non-contiguous shader
  groups and validates payload fit against stride.

Missing coverage:

- Typed wrappers over the byte-span record payload API.
- SBT update/rebuild policy when per-material or per-instance records change.
- A connection between TLAS instance SBT offsets and hit record layout.

Impact:

- Materials, geometry data pointers, texture indices, and local constants can
  be packed into SBT record data, but scene/material systems do not yet produce
  those typed payloads.

Recommended fix:

- Add typed trivially-copyable record builders on top of byte spans.
- Make the TLAS instance builder consume a hit-group layout so
  `instanceShaderBindingTableRecordOffset` matches the SBT.

### 6. Pipeline stack-size control exists; budgeting policy is missing

Severity: medium.

Evidence:

- `RayTracingPipeline::create(...)` sets `maxPipelineRayRecursionDepth`.
- `RayTracingPipeline::shaderGroupStackSize(...)` wraps
  `vkGetRayTracingShaderGroupStackSizeKHR`.
- `setRayTracingPipelineStackSize(...)` and trace helpers wrap
  `vkCmdSetRayTracingPipelineStackSizeKHR` when the pipeline uses dynamic
  stack size.

Missing coverage:

- Policy for recursion depth, callable shaders, continuation shaders, and hit
  group stack budgeting.

Impact:

- The RHI can record the stack-size state, but it does not compute a renderer
  policy for recursion/callable/intersection-heavy pipelines.

Recommended fix:

- Add a stack-budget helper once concrete RT pipelines exist, using shader
  group stack queries and the intended max recursion/callable depths.

### 7. Ray query is enabled but not surfaced as a runtime feature

Severity: medium.

Evidence:

- `Device` requires `rayQuery`.
- Descriptor reflection and AS descriptor writes support RT AS descriptors.
- No render pass or shader integration path currently advertises a ray-query
  effect.

Missing coverage:

- A render-pass convention for binding TLAS to graphics/compute shaders using
  the existing shader cursor path.
- Sample or test coverage for ray query in a non-RT pipeline.
- Documentation for when to use ray query vs full RT pipeline in this engine.

Impact:

- The feature is enabled but only useful if an individual pass manually wires
  all AS creation, binding, and synchronization.

Recommended fix:

- Treat ray query as the first hybrid-RT integration target after AS builder
  completion. It needs AS build and descriptor binding, but not RT pipeline or
  SBT orchestration.

### 8. Optional RTX-era extensions are not a baseline, but need policy

Severity: low to medium.

Evidence:

- `Device` requires NVIDIA invocation reorder, but there is no RHI surface that
  exposes shader invocation reorder policy to RT pipeline/pass code.
- `Device` requires `VK_EXT_opacity_micromap` and exposes the opacity micromap
  feature bits through `RayTracingCapabilitySnapshot`, but there is no typed
  RHI surface yet for micromap resources, build commands, or AS geometry
  micromap attachment.
- The codebase does not expose displacement micromap, motion acceleration
  structure, or position fetch features.

Assessment:

- These features should not block completing the base KHR RT path.
- Because the project targets RTX-class hardware and already requires some
  NVIDIA-specific capabilities, each optional RT extension needs an explicit
  policy: required and surfaced, required but low-level only, shader-only, or
  deliberately out of scope.

Recommended fix:

- Do not mix these extensions into the first AS/SBT completion pass.
- After base RT works, add a short feature-policy doc section for invocation
  reorder and the remaining micromap-related feature surfaces.

### 9. Test and sample coverage is not enough

Severity: medium.

Evidence:

- The repository contains `shader/test/rt/minimalRtTriangle.slang`.
- Search did not find a complete runtime test that builds BLAS/TLAS, creates an
  RT pipeline/SBT, traces rays, and verifies output.

Missing coverage:

- AS build size query test.
- BLAS/TLAS build smoke test.
- AS update validation test.
- AS copy/compact test once implemented.
- SBT layout validation test with record data.
- Minimal RT render output smoke test.
- Ray query hybrid-pass smoke test.

Impact:

- RT regressions can pass compilation because most current coverage is wrapper
  existence and shader compilation, not an end-to-end runtime path.

Recommended fix:

- Add tests in stages. Start with pure validation/unit tests for descriptors
  and SBT layout, then add a GPU smoke test once AS build ownership is stable.

## Recommended Work Order

### P0: make current RT wrappers complete enough for real AS objects

1. Add tests for AS query/build/update/copy validation.
2. Add direct batch AS build recording if renderer scheduling needs it.
3. Add typed `VkAccelerationStructureInstanceKHR` construction from scene
   packets and SBT layout decisions.

### P1: connect RT to renderer data flow

1. Add renderer-owned BLAS/TLAS cache and scratch allocation policy.
2. Convert `TlasBuildInputPacket` into real TLAS instance buffers with SBT
   record offsets.
3. Add a ray-query pass first, then a full RT pass with SBT and output image.
4. Thread AS and SBT access through render graph scheduling.

### P2: production RT pipeline features

1. Add typed SBT record builders over the current byte-span API.
2. Add stack-budget policy once real RT pipelines exist.
3. Add serialization/cache policy if startup time or streaming requires it.
4. Decide invocation reorder and micromap policy after base KHR RT is stable.

## Current Conclusion

The current RHI has the correct low-level pieces for Vulkan KHR ray tracing:
required extensions/features, RAII AS handles, BLAS/TLAS build/update helpers,
multi-geometry BLAS input, AS copy/query/serialization wrappers, RT pipeline
creation, SBT allocation with record payloads, trace/indirect trace commands,
maintenance1 indirect2 dispatch, AS descriptors, and scene packet inputs.

The incomplete areas are not small polish issues. The most important missing
pieces are renderer-owned AS lifetime and scratch orchestration, compaction and
serialization policy, typed TLAS/SBT layout construction, render-graph hazard
scheduling, test coverage, and a real render graph node that consumes scene
TLAS packets and records `traceRays(...)`.
