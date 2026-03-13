# RHI AS Infrastructure Stage-1 Execution

## 1. Scope and Baseline

This document is the executed output for `rhi_as_infra_stage1_todolist.md`.

Design baseline remains unchanged:

- plain structs carry intent
- free functions record commands
- caller owns lifecycle and submission orchestration

## 2. Current RHI Architecture (Codebase Survey)

Observed module layout in `src/rhi`:

- `nrDevice.ixx`: top-level orchestration (instance/device/features, queue manager, frame manager, allocators)
- `nrQueue.ixx` + `nrCommandBatch.ixx`: explicit submit2 contract (`vk::SubmitInfo2`, wait/signal arrays)
- `nrResource.ixx` + `nrMemoryAllocator.ixx` + `nrResourcePool.ixx`: RAII resources with strategy-based allocation
- `nrResourceOps.ixx`: data-oriented sync2 helpers and free-function copy/barrier ops
- `nrDescriptor.ixx`: descriptor write payload variants and update path, already includes AS descriptor payload

Existing AS-related support already present:

- device feature/extension gating in `nrDevice.ixx` (`accelerationStructure`, ray tracing pipeline/query)
- descriptor mapping: `slang::BindingType::RayTracingAccelerationStructure -> vk::DescriptorType::eAccelerationStructureKHR`
- descriptor payload type: `AccelerationStructureDescriptorWrite`
- runtime write path: `ShaderResourceWriter::bindAccelerationStructure(...)`

## 3. Architecture Boundary and Module Placement Decision

Decision:

- add a dedicated partition `nr.rhi:accelerationStructure` (new module boundary)
- do **not** overload `nrResource.ixx` with AS build flow details

Reason:

- matches current partitioned design (`resource`, `resourceOps`, `descriptor`, `commandBatch`)
- keeps Buffer/Image wrappers stable while isolating AS-specific API/VUID mapping
- keeps caller-controlled orchestration explicit and testable

## 4. Public API Shape (Single Source of Truth)

### 4.1 Types (plain structs only)

- `AsBuildMode` (`Build`, `Update`)
- `BlasGeometryLayout`  
  fields: `vertexFormat`, `vertexStride`, `indexType`, `maxVertex`, `geometryFlags`
- `BlasGeometryInput`  
  fields: `vertexAddress`, `indexAddress`, `transformAddress`, `primitiveCount`, `firstVertex`, `primitiveOffset`
- `TlasBuildInput`  
  fields: `instancesAddress`, `instanceCount`, `arrayOfPointers`
- `AsBuildOptions`  
  fields: `mode`, `buildFlags`, `allowUpdateExpected`
- `AsSubmitIntent` (optional metadata only)  
  fields: `waitSemaphore`, `waitValue`, `waitStageMask`, `signalSemaphore`, `signalValue`, `signalStageMask`
- `AsBuildSizes`  
  fields: `accelerationStructureSize`, `buildScratchSize`, `updateScratchSize`
- `AsDiagnostics`  
  fields: `isValid`, `message`

### 4.2 Wrapper ownership

- `AccelerationStructureResource` (RAII wrapper only for created AS handle + backing storage linkage)
- caller owns source/scratch/instance/geometry buffers and synchronization primitives

### 4.3 Free functions (no hidden submit)

- size query:
  - `queryBlasBuildSizes(...) -> AsBuildSizes`
  - `queryTlasBuildSizes(...) -> AsBuildSizes`
- recording only:
  - `recordBuildBlas(...)`
  - `recordBuildTlas(...)`
  - `recordUpdateBlas(...)`
  - `recordUpdateTlas(...)`
  - `recordCopyAccelerationStructure(...)` (future-stage guarded)
- validation helper:
  - `validateAsBuildInputs(...) -> AsDiagnostics`

Lifecycle ownership map:

- query: caller invokes
- allocation: caller allocates AS storage and scratch
- record: helper writes commands to provided command buffer
- submit/wait/signal: caller via existing `CommandBatch` + `GpuQueue`
- descriptor update: caller via existing descriptor writer

## 5. Validation Boundary Contract

At API boundary (RHI checks):

- null/zero critical fields
- geometry/type compatibility (BLAS must not use instances; TLAS must use instances)
- update invariants required by API contract (same `type`, `flags`, `geometryCount` policy metadata)
- usage-flag preconditions for build inputs and scratch buffers
- scratch alignment against device AS properties

Deferred to Vulkan validation layers:

- deep aliasing/runtime hazard graphs across external command streams
- vendor-specific/extension-specific rare VUID branches

Error policy:

- programmer contract violation -> `nrAssert(...)`
- recoverable configuration mismatch -> diagnostic return (`AsDiagnostics`) before record

## 6. Size Query Flow Contract

Rules:

1. query is independent from command buffer recording
2. query ignores src/dst handles and most addresses by spec; geometry shape + flags dominate
3. BLAS and TLAS each map deterministically to `VkAccelerationStructureBuildGeometryInfoKHR`
4. update-mode compatibility constraints are persisted in caller-visible metadata for later checks

## 7. Command Recording Flow Contract

Rules:

1. helpers only record `vkCmdBuildAccelerationStructuresKHR` (and related commands), never submit
2. one call with multiple infos has no implied ordering
3. TLAS must not be co-recorded in the same unordered batch entry with dependent BLAS producers
4. no overlap/aliasing between dst AS memory and scratch/input ranges participating in same build set
5. command pool/queue path must support compute capability, aligned with current queue system

## 8. Synchronization Recipe (One-Page Contract)

Recommended submit2 contract:

- wait stage for upstream writes to geometry/instance buffers:  
  `VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR`
- signal stage for AS completion to downstream consumers:  
  `VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR` (or narrower consumer-specific stage after explicit barrier)
- timeline values must be monotonic on same semaphore (`VkSubmitInfo2` constraints)

Barrier expectations:

- before AS build: ensure source writes are available/visible to AS build read scope
- after AS build before read/trace/copy: explicit barriers for AS read/write scopes
- integrate with existing `nr::rhi::ops::BarrierBatch` + `pipelineBarrier(...)`

## 9. Descriptor Integration Contract

Contract:

- AS wrapper exposes raw `vk::AccelerationStructureKHR` for descriptor payload
- descriptor write path remains unchanged (`AccelerationStructureDescriptorWrite`)
- caller controls descriptor update timing vs build/consume timeline ordering
- no hidden coupling to builder internals

## 10. Feature and Version Guarding

Required at device setup:

- `VK_KHR_acceleration_structure`
- `VK_KHR_ray_tracing_pipeline` (if pipeline path needed)
- `VK_KHR_ray_query` (if ray-query path needed)
- corresponding feature chain bits enabled

Guard behavior:

- missing mandatory capability -> fail early with explicit reason string/assert message
- optional capabilities (update/compaction/copy) exposed as explicit capability flags

## 11. Stage-2 Test Plan Draft

Positive tests:

- BLAS size query + record build path
- TLAS size query + record build path
- descriptor bind path with produced AS handle

Negative tests:

- missing buffer usage flags for geometry/scratch
- invalid scratch alignment
- update-mode incompatible metadata
- timeline wait/signal ordering violation in submit intent

## 12. Risks and Fallback Strategy

- over-encapsulation risk -> enforce plain structs + free functions only
- scene-policy leakage risk -> keep fields Vulkan-centric only
- synchronization misuse risk -> provide strict recipe + negative tests
- update/refit drift risk -> preserve and compare required build compatibility metadata
- compaction path uncertainty -> keep behind optional capability + explicit API, not default flow

## 13. Reference Notes

Original URLs in todo file were inaccessible (`403`) in this environment; constraints above were cross-checked against:

- `https://vkdoc.net/man/vkGetAccelerationStructureBuildSizesKHR`
- `https://vkdoc.net/man/vkCmdBuildAccelerationStructuresKHR`
- `https://vkdoc.net/man/VkAccelerationStructureBuildGeometryInfoKHR`
- `https://vkdoc.net/man/VkSubmitInfo2`
- `https://vkdoc.net/man/VkSemaphoreSubmitInfo`

