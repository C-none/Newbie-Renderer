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

Current status: partially present.

Evidence:

- `src/rhi/nrDevice.ixx` enables and requires
  `VK_KHR_acceleration_structure`, `VK_KHR_ray_tracing_pipeline`,
  `VK_KHR_pipeline_library`, `VK_KHR_ray_query`,
  `VK_KHR_deferred_host_operations`, and NVIDIA RT-related features such as
  `VK_EXT_ray_tracing_invocation_reorder`.
- `Device` snapshots only the RT pipeline properties needed by SBT creation
  and `traceRays` validation in `RayTracingCapabilitySnapshot`.
- AS limits are queried separately through
  `queryAsBuildLimits(...)` in `src/rhi/nrAccelerationStructure.ixx`.

Assessment:

- The base RT extensions are assumed, which matches the fail-fast target
  policy.
- The capability model is split and incomplete. Pipeline dispatch/SBT limits
  live in `RayTracingCapabilitySnapshot`, while AS limits require a separate
  call. No device-level snapshot covers AS feature bits such as indirect build,
  host commands, serialization support, maintenance1 features, or optional
  modern RT extensions.

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

Current status: one-geometry wrappers exist.

Evidence:

- `queryBlasBuildSizes(...)` and `queryTlasBuildSizes(...)` call
  `getAccelerationStructureBuildSizesKHR(...)`.
- `recordBuildBlas(...)` and `recordBuildTlas(...)` record
  `buildAccelerationStructuresKHR(...)`.
- `recordUpdateBlas(...)` and `recordUpdateTlas(...)` reuse the same build
  path with update mode.
- Validation covers scratch alignment, required buffer usage, BLAS/TLAS type,
  indexed or non-indexed triangle input, TLAS instance input, and update
  source/destination requirements.

Assessment:

- This is the strongest completed RT area.
- The shape is still minimal: one BLAS triangle geometry, one build range, one
  TLAS instance geometry, one command wrapper call. It does not model batch
  builds, multi-geometry BLAS, multiple ranges, procedural AABB geometry,
  indirect builds, or compaction/serialization workflows.

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
- SBT construction currently packs shader group handles only. The stride can
  be larger than the handle, but there is no typed API for writing per-record
  shader data.
- There is no wrapper for `vkGetRayTracingShaderGroupStackSizeKHR` or
  `vkCmdSetRayTracingPipelineStackSizeKHR`, so recursion-heavy or callable-hit
  layouts cannot tune pipeline stack size through RHI.
- `traceRaysIndirect(...)` supports the base KHR indirect command, but there
  is no maintenance1 `traceRaysIndirect2` path where SBT regions and dispatch
  dimensions are sourced from the device.

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

### 1. AS copy, compaction, serialization, and property query are not done

Severity: high.

Evidence:

- `recordCopyAccelerationStructure(...)` exists in
  `src/rhi/nrAccelerationStructure.ixx` but only asserts that it is reserved
  for a future stage.
- `AsBuildOptions` accepts arbitrary Vulkan AS build flags, including flags
  that can imply later compaction, but the RHI has no compacted-size query
  helper or compact-copy path.

Missing API coverage:

- `vkCmdCopyAccelerationStructureKHR` / device copy.
- `VK_COPY_ACCELERATION_STRUCTURE_MODE_CLONE_KHR`.
- `VK_COPY_ACCELERATION_STRUCTURE_MODE_COMPACT_KHR`.
- `VK_COPY_ACCELERATION_STRUCTURE_MODE_SERIALIZE_KHR`.
- `VK_COPY_ACCELERATION_STRUCTURE_MODE_DESERIALIZE_KHR`.
- `vkCmdCopyAccelerationStructureToMemoryKHR`.
- `vkCmdCopyMemoryToAccelerationStructureKHR`.
- `vkCmdWriteAccelerationStructuresPropertiesKHR` /
  `vkWriteAccelerationStructuresPropertiesKHR`.
- query handling for compacted size and serialization size.

Impact:

- Builds can produce AS objects, but the engine cannot compact BLAS memory.
- AS cache/persistence workflows cannot be represented.
- A build flag policy that enables `eAllowCompaction` would not have a matching
  RHI completion path.

Recommended fix:

- Add a narrow copy/query surface first:
  `recordCopyAccelerationStructure(...)`,
  `recordWriteAccelerationStructureProperties(...)`, and a small query helper
  for compacted size/serialization size.
- Keep wrappers thin and forward to Vulkan-Hpp RAII members directly.
- Add validation that `eAllowCompaction` is only accepted by higher-level
  builder flows when a compact-copy step is scheduled.

### 2. AS build model is triangle-only and single-range

Severity: high for production RT scenes, medium for the current minimal sample.

Evidence:

- `BlasGeometryLayout` and `BlasGeometryInput` describe one triangle geometry.
- `recordBuildBlas(...)` writes one
  `vk::AccelerationStructureGeometryTrianglesDataKHR`, one
  `vk::AccelerationStructureGeometryKHR`, and one build range.
- `recordBuildTlas(...)` writes one instance geometry.

Missing API coverage:

- Multiple BLAS geometries in one build.
- Multiple build ranges.
- AABB/procedural geometry input and intersection shader workflows.
- Per-instance full `VkAccelerationStructureInstanceKHR` construction policy,
  including SBT record offset, custom index, mask, flags, and BLAS reference.
- Indirect AS builds through `vkCmdBuildAccelerationStructuresIndirectKHR`.
- Batch build recording for several BLAS/TLAS objects in one command.

Impact:

- Any mesh with multiple submeshes/material-hit groups needs external splitting
  or a custom one-off bridge.
- Procedural primitives are impossible even though RT pipeline creation can
  create procedural hit groups.
- GPU-driven or large-scene AS update flows cannot use indirect builds.

Recommended fix:

- Introduce a multi-geometry `BlasBuildDesc` with spans of triangle/AABB
  geometry descriptors and range descriptors.
- Keep the current single-geometry helper as a convenience adapter if useful.
- Add a typed instance-builder helper that produces
  `vk::AccelerationStructureInstanceKHR` from scene packets and SBT layout
  decisions.
- Add batch build recording before indirect build; indirect build should follow
  once the direct multi-build shape is stable.

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

### 4. Synchronization is documented but not automated

Severity: medium to high.

Evidence:

- `recordBuildBlas(...)` and `recordBuildTlas(...)` document that queue-local
  hazards and queue-family ownership transfers are caller responsibilities.
- `appendAsSubmitIntent(...)` only appends wait/signal metadata to
  `CommandBatch`.
- Render graph buffer intents include AS read/write and SBT usage, but no RT
  node currently exercises AS build, AS copy, SBT read, or trace hazards.

Missing coverage:

- Barrier helpers for BLAS build to TLAS build in the same command buffer.
- Barrier helpers for AS build/update/copy to RT shader read.
- Barrier helpers for SBT upload/write to RT shader SBT read.
- Integration of AS build/copy stage/access into render graph scheduling.
- Maintenance1 sync2 stage/access bits when the project chooses to require
  `VK_KHR_ray_tracing_maintenance1`.

Impact:

- Correct execution depends on every caller remembering the Vulkan AS/RT sync
  rules.
- Once an RT pass is added, missing barriers can appear as intermittent
  traversal or SBT-data faults.

Recommended fix:

- Add small AS/SBT barrier helper functions in RHI using synchronization2.
- Thread AS build/copy and SBT read access through render graph compiler rules
  before adding a production RT node.

### 5. SBT records cannot carry typed shader-record data

Severity: medium.

Evidence:

- `ShaderBindingTable::create(...)` copies shader group handles into each
  record.
- `ShaderBindingTableSectionDesc::stride` can reserve extra bytes, but no API
  accepts per-record payload data.

Missing coverage:

- Typed or byte-span per-record data for raygen, miss, hit, and callable
  records.
- Validation that record data fits in the selected stride.
- SBT update/rebuild policy when per-material or per-instance records change.
- A connection between TLAS instance SBT offsets and hit record layout.

Impact:

- Materials, geometry data pointers, texture indices, and local constants must
  be accessed through global descriptors or ad hoc side channels.
- The current SBT is enough for a minimal triangle sample but not for a
  materialized scene.

Recommended fix:

- Add `ShaderBindingTableRecordData` as byte spans or typed trivially-copyable
  records.
- Make the TLAS instance builder consume a hit-group layout so
  `instanceShaderBindingTableRecordOffset` matches the SBT.

### 6. Pipeline stack-size control is missing

Severity: medium.

Evidence:

- `RayTracingPipeline::create(...)` sets `maxPipelineRayRecursionDepth`.
- There is no wrapper for shader group stack-size query or command-buffer stack
  size setting.

Missing coverage:

- `vkGetRayTracingShaderGroupStackSizeKHR`.
- `vkCmdSetRayTracingPipelineStackSizeKHR`.
- Policy for recursion depth, callable shaders, continuation shaders, and hit
  group stack budgeting.

Impact:

- The RHI cannot tune stack size for complex RT pipelines.
- Increasing recursion depth or introducing callable/intersection shaders may
  overpay or under-specify stack requirements depending on driver defaults and
  pipeline shape.

Recommended fix:

- Add a thin query helper on `RayTracingPipeline`.
- Add a command-buffer helper for setting stack size before `traceRays(...)`.
- Keep the default path simple for one-bounce/minimal RT, but expose an
  explicit advanced path.

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
- The codebase does not expose opacity micromap, displacement micromap, motion
  acceleration structure, or position fetch features.

Assessment:

- These features should not block completing the base KHR RT path.
- Because the project targets RTX-class hardware and already requires some
  NVIDIA-specific capabilities, each optional RT extension needs an explicit
  policy: required and surfaced, required but shader-only, or deliberately out
  of scope.

Recommended fix:

- Do not mix these extensions into the first AS/SBT completion pass.
- After base RT works, add a short feature-policy doc section for invocation
  reorder and micromap-related features.

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

1. Implement AS copy/compact/property query wrappers.
2. Add multi-geometry and multi-range BLAS descriptors.
3. Add batch AS build recording.
4. Add AS/SBT synchronization2 helper functions.
5. Add tests for AS query/build/update/copy validation.

### P1: connect RT to renderer data flow

1. Add renderer-owned BLAS/TLAS cache and scratch allocation policy.
2. Convert `TlasBuildInputPacket` into real TLAS instance buffers with SBT
   record offsets.
3. Add a ray-query pass first, then a full RT pass with SBT and output image.
4. Thread AS and SBT access through render graph scheduling.

### P2: production RT pipeline features

1. Add SBT record data.
2. Add pipeline stack-size query/set helpers.
3. Add `VK_KHR_ray_tracing_maintenance1` policy and optionally
   `traceRaysIndirect2`.
4. Add serialization/cache policy if startup time or streaming requires it.
5. Decide invocation reorder and micromap policy after base KHR RT is stable.

## Current Conclusion

The current RHI has the correct base pieces for Vulkan KHR ray tracing:
required extensions/features, RAII AS handles, single BLAS/TLAS build/update
helpers, RT pipeline creation, SBT allocation, trace commands, AS descriptors,
and scene packet inputs.

The incomplete areas are not small polish issues. The most important missing
pieces are AS copy/compaction/serialization/property query, multi-geometry and
batch AS builds, renderer-owned AS lifetime and scratch orchestration, AS/SBT
barrier integration, SBT record data, pipeline stack-size control, and a real
render graph node that consumes scene TLAS packets and records `traceRays(...)`.
