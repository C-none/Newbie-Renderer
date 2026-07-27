# RenderGraph Skeleton reuse

`RendererCacheSuite::skeletonCache` is the renderer-owned, bounded cache for exact
installed-graph structural variants. The initial capacity is eight entries. An entry
owns only normalized CPU structure: resource/pass/use/node/submit/execution-order
signatures. It never owns frame data, imported Vulkan objects, retained-state
references, descriptor snapshots, callbacks, command buffers, swapchain images, or
compiled barriers.

The lookup key includes the installed graph generation, display/render/swapchain
extents, swapchain format and color space, node configuration revisions and structural
branch keys, the Slang session generation, submit/acquire policy revision, and the
monotonic swapchain recreation generation. Frame ordinal is deliberately excluded.

Nodes opt in through the generic `NodeRuntime` structural snapshot and patch contract.
If any installed node does not opt in, the whole frame uses the legacy build path. An
enabled hit instantiates the immutable template, restores its named-resource maps and
exact global/node patch ranges, then patches only current imports, frame data,
descriptor snapshots, callbacks, and copy payloads. It does not call graph structural
declaration APIs. Patch failure discards the partial instance and performs one clean
cold build; previous-frame closures are never replayed.

Patch-only hits are implemented for `LightPrepare`, `PathTracing`, `Accumulate`,
DLSS Ray Reconstruction, UI, `Present`, and `AccelerationStructureBuild`, so the
complete `rtobject` graph can reuse an exact Skeleton. PathTracing keys and patches
its exact trace or unavailable-clear topology, including current guide imports,
TLAS/sideband bindings, pipeline/SBT selection, and bindless scene textures. DLSS RR
separates disabled, enabled, alpha-output, motion-vector-debug, and bypass variants
while patching current evaluation state. UI keys its active texture-table identity
and patches current draw uploads, image imports, bindless descriptors, and viewport.
Present processes completed screenshot readbacks during pre-snapshot UI collection,
then validates the frozen screenshot branch before patching slots or consuming a
one-shot request.
AccelerationStructureBuild performs its per-frame AS planning before lookup and keys
the resulting no-instance, TLAS-only, or dirty-BLAS topology, including exact dirty
mesh geometry variants and current resource capacities. A hit patches current
BLAS/TLAS imports, sideband frame data, build inputs, and callbacks without graph
declarations. A branch mismatch preserves the prepared AS packet so the clean cold
fallback consumes that same preflight result without advancing the frame serial again.
Dynamic instance transforms and masks remain patch-preserving; scene
identity, structural RT revisions, BLAS atlas replacement/growth, structural-plan
generation, or capacity changes select a cold exact variant.

The renderer exposes `Legacy`, `Enabled`, and `Differential` modes. `Enabled` is the
default for an all-capable installed chain. `Legacy` is the rollback path.
`Differential` retains the cold materialization comparison path and reports structural
mismatches instead of accepting partial or superset topology.

External state is specialized after Skeleton materialization. Buffer, image, and
acceleration-structure imports can reference their type-safe retained state. Their
common state records initialization, ownership, access, and the last successful
submission timeline value; images additionally record layout. Acceleration-structure
state describes its backing storage synchronization semantics. The compiler consumes
the current initial state, and the executor updates final state only for a valid final
use with a corresponding successful submission token.

Skeleton statistics report hits, misses, entries, invalidations, structural mismatches,
patch failures, and the latest miss reason. `node_loop` includes only cold structural
build work. Only successful work performed through
`RenderGraphSkeletonPatchContext` is charged to Skeleton patch timing.
