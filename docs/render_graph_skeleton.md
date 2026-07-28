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
materialization consumes that same preflight result without advancing the frame serial
again. Materialization requires this packet and fails fast when preflight was skipped;
it never reruns AS planning through a node-local fallback.
Dynamic instance transforms and masks remain patch-preserving; scene
identity, structural RT revisions, BLAS atlas replacement/growth, structural-plan
generation, or capacity changes select a cold exact variant.

The renderer exposes `Legacy`, `Enabled`, and `Differential` modes. `Enabled` is the
default for an all-capable installed chain. `Legacy` is the rollback path.
`Differential` retains the cold materialization comparison path and reports structural
mismatches instead of accepting partial or superset topology.

The `rtobject` benchmark exposes only `--render-graph-skeleton legacy|enabled` for
Release A/B sampling. Absence selects `enabled`; the selector is rejected outside
benchmark mode. `Differential` is not a timing baseline because it performs cold
materialization comparison rather than patch-versus-cold execution. Non-Release
application benchmark launches are rejected before renderer/device initialization;
Debug remains available for contract and correctness tests only.

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
`RenderGraphSkeletonPatchContext` is charged to Skeleton patch timing. Benchmark
schema v3 records patch and rebuild distributions as nested children of graph build,
plus per-frame hit/miss reason and run-level cache deltas. Legacy frames use the
explicit `disabled` reason, keep rebuild timing at zero/not-applicable, and are not
counted as failed Enabled hits; their cold structural cost remains `node_loop`.
Run-cache deltas cover warmup, measurement, and drain, while frame hit/miss counts
cover only exported measurement frames. Mode-inconsistent or unknown telemetry
invalidates the benchmark audit.

## Release A/B result (2026-07-28)

One direct foreground Legacy/Enabled pair used the same LLVM Release executable
(SHA256 `03e97506ac52d2601631b2d00a17b7dca6fbbacbf25ad6108840980fe719d8ed`),
Sponza, RTX 5070 Ti Laptop GPU and driver `2559541248`, `1920 x 1080` display,
`640 x 360` render extent, DLSS Ultra Performance, visible-static UI, three frames in
flight, 600 accepted warmup frames, and 1200 accepted measurement frames. Validation
was disabled. The retained schema-v3 artifacts are the
[Legacy run](reports/rt_pipeline_cpu_performance/runs/20260728-rtobject-skeleton-legacy-release-v3)
and [Enabled run](reports/rt_pipeline_cpu_performance/runs/20260728-rtobject-skeleton-enabled-release-v3).

| Metric | Legacy p50 / p95 / mean (ms) | Enabled p50 / p95 / mean (ms) | Enabled delta p50 / p95 / mean (ms) |
|---|---:|---:|---:|
| Total | 2.184950 / 3.113600 / 2.182049 | 2.173650 / 3.040295 / 2.185846 | -0.011300 / -0.073305 / +0.003797 |
| CPU work | 1.393350 / 2.060700 / 1.479125 | 1.350600 / 2.024815 / 1.435721 | -0.042750 / -0.035885 / -0.043404 |
| Build | 0.257450 / 0.555220 / 0.293764 | 0.236100 / 0.511930 / 0.269315 | -0.021350 / -0.043290 / -0.024449 |
| Node loop | 0.200100 / 0.430300 / 0.225586 | 0 / 0 / 0 | -0.200100 / -0.430300 / -0.225586 |
| Skeleton patch | 0 / 0 / 0 | 0.080000 / 0.172700 / 0.089502 | +0.080000 / +0.172700 / +0.089502 |
| Compile | 0.022900 / 0.047700 / 0.025221 | 0.022000 / 0.046210 / 0.024426 | -0.000900 / -0.001490 / -0.000795 |
| Prepare | 0.118500 / 0.164730 / 0.125181 | 0.115700 / 0.213670 / 0.123235 | -0.002800 / +0.048940 / -0.001946 |
| Execute | 0.601600 / 0.812015 / 0.615095 | 0.598400 / 0.804705 / 0.607642 | -0.003200 / -0.007310 / -0.007454 |

Both runs report `run_status: valid`, exactly 1200 strictly ordered frames with one
stable configuration, and complete 8400-row GPU-pass exports with zero quality-audit
errors. Legacy reports 1200 `disabled` frames, zero patch/rebuild timing, and no
failed Enabled misses. Enabled reports 1200 measurement hits with reason `none`, zero
measurement misses, patch failures, structural mismatches, or rebuilds. Its complete
warmup/measurement/drain lifecycle recorded 1794 hits and nine cold misses.

This single ordered pair verifies the mechanism and shows lower CPU-work p50, p95, and
mean, but it is not a multi-pair statistical promotion. The CPU-work mean improvement
is `0.043404 ms` (`2.93%`), below the roadmap's `0.05 ms` re-measure band, while
Prepare p95 increased by `0.048940 ms`. Retain the rollback selector and repeat
counterbalanced pairs with fixed clocks, power, thermal, CPU-affinity, commit, and
dirty-tree provenance before making a broader performance claim. Nested node-loop and
patch timings diagnose Build and must not be added to top-level stages; GPU passes
remain per-pass observations and were not summed or used as a promotion endpoint.
