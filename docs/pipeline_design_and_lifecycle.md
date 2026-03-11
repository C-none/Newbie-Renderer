# Pipeline Design and Lifecycle

## Scope

This document describes the current Vulkan pipeline toolkit behavior in `nrrhi` after the latest capability-policy simplification.

Primary implementation files:

- `src/rhi/nrDevice.ixx`
- `src/rhi/nrPipeline.ixx`
- `src/rhi/nrDescriptor.ixx`

Related reference:

- `docs/slang_bindingtype_descriptor_mapping.md`

## Current Capability Policy

Capability checks are centralized at device extension enable time in `Device::makeDevice()`.

- All extensions listed in `deviceEnabledExtensions` are treated as required.
- Missing required extension triggers `nrAssert` during device creation.
- Pipeline creation no longer performs secondary capability assertions.

As a result, pipeline modules assume the required extension set is already enabled once device creation succeeds.

## Removed Runtime Capability Contract

The following session-level simplifications were applied:

1. Removed `PipelineRuntimeCapabilities` runtime contract structure.
2. Removed `PipelineCapabilityValidator` and its strict validation entry points.
3. Removed `Device::queryPipelineRuntimeCapabilities()` and runtime capability propagation into pipeline state.
4. Removed profile test flow that depended on the deleted strict capability validator (`nr_pipeline_strict_policy_test`).

## Extension Utilization Matrix

| Extension | Utilization in design | Why this matters |
|---|---|---|
| `VK_KHR_dynamic_rendering` | Enables renderpass-free graphics pipeline path | Modern graphics pipeline assembly |
| `VK_EXT_extended_dynamic_state` | Enables dynamic cull/front-face/topology states used by graphics path | Reduces static pipeline permutations |
| `VK_EXT_extended_dynamic_state3` | Enables depth/polygon/sample dynamic states appended by graphics path | Broader runtime state control |
| `VK_KHR_synchronization2` | Required during device extension enable flow | Aligns queue/sync model roadmap |
| `VK_EXT_descriptor_indexing` | Required during device extension enable flow | Descriptor model evolution |
| `VK_KHR_pipeline_library` | Required during device extension enable flow | Future pipeline composition path |
| `VK_EXT_pipeline_robustness` | Required during device extension enable flow | Robustness feature baseline |
| `VK_KHR_acceleration_structure` + `VK_KHR_ray_tracing_pipeline` | Required during device extension enable flow | Ray tracing pipeline enablement |

## Lifecycle Overview

1. `Device::makeDevice()` gathers extension support and asserts if any configured extension is missing.
2. `createGraphicsPipeline/createComputePipeline/createRayTracingPipeline` create descriptor layout, pipeline layout, shader program, and pipeline object directly.
3. No runtime capability struct or second-pass capability assertion is used in pipeline creation.

## Notes

- This policy intentionally favors a single hard-fail gate at extension-enable stage over duplicated checks in downstream pipeline code.
- If compatibility profiles are reintroduced later, add profile selection at device-creation stage instead of re-adding scattered pipeline-time capability asserts.
