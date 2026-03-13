# RHI AS Infrastructure Stage-1 TODO (ScopedRendering Philosophy)

## Document Purpose

This document is the stage-1 execution todo for building acceleration-structure infrastructure in the RHI layer using the existing design philosophy:

- plain structs describe intent
- free functions record commands
- lifecycle is controlled by caller

Stage-1 only defines plan and constraints. No implementation is included here.

## Motivation

Current RHI already has the right architectural baseline for this direction:

- RAII resource wrappers and allocator pipeline in [src/rhi/nrResource.ixx](../src/rhi/nrResource.ixx) and [src/rhi/nrMemoryAllocator.ixx](../src/rhi/nrMemoryAllocator.ixx)
- command/submit abstraction with timeline-friendly submit info in [src/rhi/nrCommandBatch.ixx](../src/rhi/nrCommandBatch.ixx)
- synchronization2-oriented resource ops in [src/rhi/nrResourceOps.ixx](../src/rhi/nrResourceOps.ixx)
- descriptor path already includes acceleration structure descriptor type and write payload in [src/rhi/nrDescriptor.ixx](../src/rhi/nrDescriptor.ixx)

The gap is not "whether RHI can do AS", but "how to add AS infrastructure without breaking layering and ownership boundaries".

## Tool Boundary (Must Be Enforced)

The future AS infrastructure in RHI must stay inside these boundaries.

### In Scope

- typed plain structs for BLAS/TLAS build intent
- size query helpers wrapping Vulkan query calls
- command recording helpers for AS build/update/copy commands
- RAII wrapper for acceleration structure object and backing storage linkage
- validation helpers for Vulkan mandatory usage constraints where practical

### Out of Scope

- scene graph management
- mesh import and geometry preprocessing policy
- frame graph policy and pass scheduling
- automatic global submission orchestration across all subsystems
- hidden internal queue submission side effects in generic build helpers

### Ownership Rules

- caller owns lifecycle of geometry source buffers, instance buffers, scratch buffers, and submit synchronization values
- RHI owns only RHI objects it explicitly creates (for example AS handle wrappers)
- command recording helper functions must not silently allocate long-lived resources

## Vulkan API Usage Constraints (Normative Baseline)

The implementation phase must obey the following core Vulkan constraints.

1. Build-size query and build execution are separate steps.
2. Size query ignores src or dst AS handles and most addresses; geometry shape and build parameters define required sizes.
3. Build input buffers for triangles or AABBs or instances must include acceleration-structure build-input read-only usage.
4. Scratch address must be from a storage-buffer-capable allocation and satisfy min scratch alignment.
5. One build call with multiple infos has no implied ordering between entries.
6. TLAS cannot be built in the same batch entry group as BLAS producers it depends on.
7. Timeline wait and signal values must be monotonically valid for the same semaphore.
8. Stage masks for semaphore submit info must match enabled features and intended synchronization scope.

Reference pages used for this todo baseline:

- https://docs.vulkan.org/refpages/latest/refpages/source/vkGetAccelerationStructureBuildSizesKHR.html
- https://docs.vulkan.org/refpages/latest/refpages/source/vkCmdBuildAccelerationStructuresKHR.html
- https://docs.vulkan.org/refpages/latest/refpages/source/VkAccelerationStructureBuildGeometryInfoKHR.html
- https://docs.vulkan.org/refpages/latest/refpages/source/VkSubmitInfo2.html
- https://docs.vulkan.org/refpages/latest/refpages/source/VkSemaphoreSubmitInfo.html
- https://docs.vulkan.org/refpages/latest/refpages/source/VkBufferUsageFlagBits.html

## Stage-1 Deliverables

1. finalize architecture boundary and API shape document
2. produce implementation checklist with explicit acceptance gates
3. prepare risk register and fallback strategy for update/refit or compaction paths

## Detailed TODO List

### A. Architecture and Naming

- [x] define module placement for AS infrastructure
- [x] decide whether AS wrapper lives in [src/rhi/nrResource.ixx](../src/rhi/nrResource.ixx) or a dedicated partition
- [x] define naming convention for BLAS and TLAS intent structs and command helpers
- [x] ensure naming aligns with existing style in [src/rhi/nrResourceOps.ixx](../src/rhi/nrResourceOps.ixx)
- [x] document why free functions are preferred over inheritance-based builders for this layer

Acceptance gate:

- there is a single source of truth listing all public API entry points and the owner of each lifecycle step

### B. Public Plain Struct Design

- [x] define BLAS geometry layout struct (format, stride, index type, max vertex, flags)
- [x] define BLAS runtime geometry input struct (addresses plus range info)
- [x] define TLAS build input struct (instance address source and instance count)
- [x] define build option struct (mode and build flags)
- [x] define lightweight submit-intent struct for optional caller-provided semaphore metadata
- [x] define result structs for size query outputs and diagnostics

Acceptance gate:

- every field in public structs can be mapped to a concrete Vulkan field and has no scene-specific policy data

### C. Boundary Validation Rules

- [x] create checklist mapping each public field to Vulkan valid-usage expectations
- [x] mark which validations are performed at API boundary versus deferred to validation layers
- [x] explicitly require caller to pass compatible usage flags for build input and scratch buffers
- [x] define error reporting policy for invalid combinations (assert versus runtime error channel)

Acceptance gate:

- each API call has a documented precondition list and failure behavior

### D. Size Query Flow

- [x] define helper API for build size query that accepts plain layout structs and primitive counts
- [x] define deterministic mapping from helper input to Vulkan build geometry info for query
- [x] define handling for BLAS and TLAS mode-specific requirements
- [x] define strategy for preserving compatibility constraints for update mode

Acceptance gate:

- query helpers can be used without any command buffer and without pre-created destination AS handles

### E. Command Recording Flow

- [x] define free functions for recording BLAS build command batches
- [x] define free functions for recording TLAS build command batches
- [x] define update-mode recording signature and required caller-provided invariants
- [x] define memory aliasing and overlap constraints in function-level notes
- [x] define expectation that command buffer pool supports compute operations

Acceptance gate:

- recording helpers are side-effect free beyond command buffer recording and do not submit by default

### F. Synchronization Contract

- [x] define minimal timeline semaphore contract for external orchestration
- [x] document recommended stage mask for AS build wait and signal points
- [x] define when caller must insert explicit barriers around source data writes and AS reads
- [x] align with existing submit flow in [src/rhi/nrCommandBatch.ixx](../src/rhi/nrCommandBatch.ixx)

Acceptance gate:

- there is a one-page synchronization recipe that can be directly translated into submit code

### G. Descriptor Integration Contract

- [x] define how AS handles produced by RHI wrappers flow into descriptor write payloads
- [x] confirm compatibility with existing acceleration-structure descriptor write path in [src/rhi/nrDescriptor.ixx](../src/rhi/nrDescriptor.ixx)
- [x] document caller responsibility for descriptor update timing versus command recording lifecycle

Acceptance gate:

- descriptor binding examples require no hidden coupling to AS builder internals

### H. Version and Feature Guarding

- [x] define required feature set and extension checks at device setup level
- [x] define guard behavior for missing acceleration-structure capability
- [x] define optional capability flags for update or compaction related paths

Acceptance gate:

- unsupported capability paths fail early with explicit reason strings

### I. Test and Validation Plan (Planning Only)

- [x] draft smoke test cases for BLAS and TLAS creation and build record path
- [x] draft negative tests for invalid usage flags and alignment violations
- [x] draft synchronization misuse tests for timeline ordering violations
- [x] draft descriptor integration tests for AS handle binding

Acceptance gate:

- each planned API has at least one positive and one negative test case in the stage-2 implementation plan

## API Usage Guidelines (To Be Enforced in Stage-2)

1. Keep APIs data-oriented and explicit; avoid implicit global state in helper calls.
2. Separate query, allocation, recording, and submission steps.
3. Do not hide queue submission inside generic record helpers.
4. Keep synchronization explicit and caller-controlled.
5. Keep RHI free from scene-level semantics.
6. Use validation layers as authoritative runtime checker; RHI-side checks focus on early, high-value errors.

## Risks and Mitigation

- Risk: over-encapsulation reintroduces opaque builder behavior.
  Mitigation: enforce free-function recording APIs and explicit caller ownership.

- Risk: helper APIs accidentally encode scene policy.
  Mitigation: keep structs Vulkan-centric and avoid mesh or scene metadata fields.

- Risk: synchronization misuse across build and consume phases.
  Mitigation: require explicit wait or signal contract examples and stage-mask defaults.

- Risk: update-mode compatibility drift.
  Mitigation: preserve and compare key build parameters where feasible and document invariants.

## Exit Criteria for Stage-1 Completion

- architecture boundary is explicitly approved
- public API shape is approved in plain-struct plus free-function style
- synchronization and ownership contracts are approved
- stage-2 can begin without unresolved API ownership ambiguity

## Stage-1 Execution Output

Stage-1 outputs are finalized in:

- [RHI AS Infrastructure Stage-1 Execution](./rhi_as_infra_stage1_execution.md)

Notes for reference fetch:

- original reference URLs in this file returned `403` in the current environment.
- normative constraints in execution output are cross-checked against Vulkan man-page mirror pages under `https://vkdoc.net/man/...` for the same symbols.
