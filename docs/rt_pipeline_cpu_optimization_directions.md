# RT Pipeline CPU Optimization Directions

> **Status:** R0, the first R1 runtime phase, and the M1 lazy-diagnostic facility are implemented. R1 is not experimentally promoted because this patch does not retain a live legacy switch or differential mode; M1 performance promotion still requires an isolated benchmark; other work packages remain proposed experiments.
>
> **Baseline date:** 2026-07-25.
>
> **Provenance limit:** The baseline was captured from the current dirty working tree.
> The artifacts do not contain a commit hash, executable hash, CPU identity/affinity,
> fixed CPU or GPU clocks, laptop power-plan/AC-state provenance, or a thermal/throttling
> record.
>
> **Document role:** This is the implementation decision document. The
> [performance investigation](rt_pipeline_cpu_performance_investigation.md), especially
> sections 6 through 11, remains the historical evidence narrative and source audit. This
> roadmap turns that evidence into ordered experiments, shared contracts, and promotion
> gates.

## 1. Technical summary

The current two-run mean CPU work is approximately **2.380 ms** and mean Wait GPU is
approximately **0.008 ms**. Execute is the largest named top-level CPU bucket at
`0.827 ms` (`34.7%`), followed by Build at `0.652 ms` (`27.4%`). These are optimization
opportunity ceilings, not predicted savings.

Even if a valid timeline later proves `CPU < GPU` for this workload, that result answers
only the steady-state throughput-bottleneck question. It does not make `2.380 ms` of CPU
work acceptable for CPU budget, input-to-present latency, headroom for simulation and
streaming, frame-time tails, lower-power CPUs, higher refresh targets, or scene scaling.
Conversely, the near-zero Wait GPU counter does not prove a GPU queue gap.

The implementation order is:

1. close measurement provenance, obtain a valid CPU-thread/GPU-queue timeline, and split
   Unclassified;
2. A/B the executor's current worker policy against inline, reduced-task, and
   cost-threshold variants;
3. consolidate the frame-local raster and TLAS scene-texture request and expose descriptor
   cache telemetry;
4. establish explicit scene and extraction revision ownership;
5. reuse a revision-keyed immutable RT plan and patch truly dynamic frame state;
6. incrementally reuse scene bridge and TLAS texture products after revisions exist;
7. attribute Present and Prepare across application/API/driver/WSI boundaries.

Submit merging, managed transient-resource reuse, and graph skeleton/compile work remain
deferred. No valid Nsight timeline exists, so this roadmap makes no queue-gap, overlap, or
GPU critical-path claim.

## 2. Evidence hierarchy and metric semantics

Evidence is interpreted in this order:

1. The two current run artifacts control all numerical claims:
   [run 1 metadata](reports/rt_pipeline_cpu_performance/runs/20260725-165714-rtx5070ti-up-final-run1/metadata.json),
   [run 1 summary](reports/rt_pipeline_cpu_performance/runs/20260725-165714-rtx5070ti-up-final-run1/summary.json),
   [run 2 metadata](reports/rt_pipeline_cpu_performance/runs/20260725-165714-rtx5070ti-up-final-run2/metadata.json), and
   [run 2 summary](reports/rt_pipeline_cpu_performance/runs/20260725-165714-rtx5070ti-up-final-run2/summary.json).
2. Current dirty-tree source controls feasibility and existing-boundary claims.
3. Official Khronos and NVIDIA guidance constrains safe practice, but does not replace
   device-specific measurement.
4. The 2026-07-24 HTML/artifact and investigation sections 1-11 supply hypothesis history
   only. They are not current numerical authority.

Metric meanings follow
[renderer performance measurement](renderer_performance_measurement.md):

- **CPU work** is `total_ms - wait_gpu_ms`.
- **Top-level buckets** are mutually classified wall-clock intervals and may be added to
  describe CPU work. Unclassified is the residual after those buckets.
- **Nested substages** diagnose work inside or between top-level intervals. Unless the
  schema explicitly defines a mutually exclusive sequence, they are not additive and
  must not be added to top-level stages.
- **Execute substages** are a mutually exclusive main-thread accounting sequence within
  `RenderGraphExecutor::executePrepared`; worker recording time is not added to them.
- **GPU pass timestamps** are per-pass durations. Values from different queues must never
  be summed into a frame time or critical path.

The current runs and older captures lack controlled, matching provenance. This document
therefore makes no old-versus-current causal comparison.

The numerical baseline artifacts in this roadmap use schema v1: `post_scene_ms` was a
`cpu_substages` diagnostic and remained part of Unclassified. Current schema v2 promotes
that same mutually exclusive interval to a top-level stage, so the baseline table and its
`0.244 ms` Unclassified value retain historical v1 semantics.

## 3. Current baseline and opportunity

The table recomputes each two-run mean directly from the two final `summary.json` files.
Shares use the recomputed `2.380014583 ms` CPU-work mean as denominator.

| Top-level metric | Two-run mean | Share of CPU-work mean | Run p50 range / interpretation |
|---|---:|---:|---|
| CPU work | 2.380 ms | 100.0% | 2.356-2.423 ms |
| Execute | 0.827 ms | 34.7% | 0.813-0.814 ms; stable |
| Build | 0.652 ms | 27.4% | 0.645-0.672 ms |
| Scene | 0.250 ms | 10.5% | 0.249-0.260 ms |
| Unclassified | 0.244 ms | 10.2% | 0.250-0.267 ms |
| Present | 0.225 ms | 9.5% | 0.198-0.209 ms |
| Prepare | 0.135 ms | 5.7% | 0.134-0.139 ms |
| Frame Setup | 0.030 ms | 1.2% | 0.027-0.029 ms |
| Compile | 0.017 ms | 0.7% | approximately 0.016 ms in both runs |

These shares are safe upper bounds on the named top-level opportunities, not savings
forecasts. Build mean changes by about 6% between runs and Scene by about 9%; Execute mean
and p50 are comparatively stable. Every variant therefore needs isolated, counterbalanced
repeated pairs rather than a single before/after run.

## 4. Roadmap portfolio

| ID | Decision question | Measured opportunity | Confidence | Complexity / risk | Prerequisite | Current decision |
|---|---|---|---|---|---|---|
| G0 | Can the CPU/GPU timeline and Unclassified interval support causal decisions? | Gate, not a saving; Unclassified is 0.244 ms | High need; missing evidence | Medium tooling risk | Supported profiler setup and full provenance | Proceed first |
| E1 | Does inline, reduced-task, or thresholded recording beat always-worker recording? | Execute ceiling 0.827 ms; task-plan launch is nested/overlapped | Medium | Medium scheduling and GPU-feed risk | G0 timeline and policy switch | Proceed as first optimization A/B |
| E2 | Does retaining structural task-plan capacity reduce cost after policy is selected? | Subset of E1; task-plan launch mean is approximately 0.551 ms but is not removable overhead | Low-medium | Medium lifetime risk | E1 winner and allocation counters | Hold |
| D1 | Can raster and TLAS texture discovery produce one frame-local request? | Unclassified ceiling 0.244 ms; TLAS collection is nested there | High feasibility | Low-medium correctness risk | Request telemetry | Proceed independently of E1 |
| D2 | Can a shared request and earlier version hit avoid redundant request construction? | Build/Prepare are ceilings only; request cost unisolated | Medium | Medium descriptor-state risk | D1 and cache counters | Proceed after D1 |
| R0 | Which owner advances each scene/extraction revision? | Gate, not a direct saving | High need | High API and invalidation-design risk | Mutation matrix | Implemented |
| R1 | Can stable RT topology/metadata/material/geometry/hit-SBT plans be reused and dynamic instance state patched? | Build ceiling 0.652 ms; AS metadata-plan mean approximately 0.137 ms is nested | Medium-high | High correctness/lifetime risk | R0 and differential validation | Runtime implemented; legacy switch, differential validation, and promotion pending |
| S1 | Can static scene bridge and TLAS texture products become incremental? | Scene ceiling 0.250 ms plus non-additive TLAS collection in Unclassified | Medium | High mutation coverage risk | R0; preferably R1 contracts | Hold until revisions |
| M1 | Does lazy successful `nrAssert` message construction produce repeatable savings? | Unmeasured micro-opportunity | Medium feasibility, low impact confidence | Low | Isolated counter and A/B | Lazy facility and expensive ShaderCursor contexts implemented; promotion pending |
| P2 | Where do Present and Prepare costs belong? | Present 0.225 ms; Prepare 0.135 ms, separately | Medium | Medium WSI/driver attribution risk | G0-quality trace | Attribute later |
| Deferred | Do fewer submits, managed transient reuse, or compile redesign become material? | Submit nested in Execute; Compile only 0.017 ms | Low current priority | High architecture/synchronization risk | New evidence | Do not implement now |

## 5. Shared contracts required before implementation

### 5.1 Proposed source-of-truth revision ownership

R0 must turn this proposed ownership table into explicit APIs. A revision changes only
after its owned semantic state changes; frame ordinal is not a substitute.

| Revision domain | Proposed owner | Consumers | Effect on derived products | Invalidation lifetime |
|---|---|---|---|---|
| Scene identity and instance topology | `nr::scene::Scene` | extraction, bridge, AS plan | Rebuild membership, ordering, topology, instance-to-metadata mapping | Entire scene identity; invalidate on unload/reload |
| World transforms | Scene transform/hierarchy mutation boundary | raster bridge, TLAS instances, bounds/culling | Patch transform-bearing products; rebuild only if policy requires | Until next transform mutation |
| Visibility and instance mask | Scene selection/visibility owner | extraction, raster lists, TLAS instances | Patch masks or rebuild active-instance list | Until visibility/mask mutation |
| Mesh GPU geometry and BLAS atlas generation | Scene mesh GPU record for geometry; AS runtime for BLAS atlas | extraction, BLAS cache, RT plan | Rebuild geometry/BLAS-dependent plan; retire old atlas references | Until upload/replacement/repack retirement completes |
| Material CPU version | Scene material asset record | raster/RT material compilation and texture discovery | Rebuild affected material and hit-plan entries | Until material replacement/unload |
| Texture descriptor ID and GPU residency version | Scene texture asset plus renderer descriptor-table owner | request, bindless cache, material plan | Patch descriptor payload; rebuild texture-dependent request/version | Until replacement/eviction retirement completes |
| Shader/pipeline/SBT ABI and record-plan hash | Pipeline/Slang reflection owner plus RT node | PathTracing pipeline/SBT and AS hit plan | Rebuild incompatible pipeline/SBT/record plan | Until pipeline and SBT retirement completes |
| Graph and render configuration | Renderer graph-spec/configuration owner | builder, compile cache, nodes | Rebuild graph/config-dependent products | Until config revision changes again |
| Frame-slot allocation generation | `PipelineRuntime`/descriptor allocation owner | bindless applied-version cache | Invalidate only the affected owner/table/frame-slot applied state | Per allocation generation and frame slot |

The exact type names and API location are an R0 decision. Existing material and texture
version fields may participate, but they do not form a global extraction revision contract.

### 5.2 Three-frames-in-flight and retirement rules

- Applied descriptor versions and allocation generations remain per owner, table, and
  frame slot.
- First use after invalidation must initialize every affected slot independently.
- A resource or descriptor backing still referenced by an in-flight frame must not be
  replaced in place.
- Replacement uses deferred retirement for at least the established frame/timeline
  lifetime; completion evidence, not CPU frame count alone, controls safe destruction.
- Scene unload/reload, device teardown, graph replacement, swapchain recreation where
  relevant, and node shutdown invalidate all cached references in their ownership domain.
- Differential validation must force first use in all three slots rather than validating
  one slot repeatedly.

### 5.3 Cache-value and command-buffer boundaries

Stable cache values may contain owned immutable values, stable handles covered by explicit
generations, and reproducible keys. They must not retain per-frame
`std::reference_wrapper`s, swapchain handles, current command buffers, transient frame
data, or dynamic descriptor snapshots.

The executor already reuses primary and secondary command-buffer objects per frame slot.
The current path resets them and begins each recording with
`vk::CommandBufferUsageFlagBits::eOneTimeSubmit`. Retain that behavior. This roadmap does
not propose reusing recorded command contents, skipping required reset, or switching to
simultaneous-use recording.

Khronos recommends a reasonable number of secondary buffers, per-thread pools for
concurrent recording, and appropriate recycling in
[Command Buffer Usage](https://docs.vulkan.org/samples/latest/samples/performance/command_buffer_usage/README.html).
Those are constraints on E1/E2, not proof that more or fewer tasks win on this workload.

## 6. Detailed work packages

Each package should be independently switchable and retain the current path until promotion. The current R1 runtime patch does not yet meet that promotion requirement.

### G0 - Provenance, timeline, and Unclassified closure

- **Objective / hypothesis:** distinguish application work, scheduler delay, Vulkan calls, submission, queue execution, and WSI so later changes target a causal interval.
- **Current boundary:** metric and trace rules are in [renderer performance measurement](renderer_performance_measurement.md). `Renderer::renderFrame` in [`nrRenderer.cpp`](../src/renderer/nrRenderer.cpp) starts the post-Scene residual before frame parameters and TLAS texture collection; `RenderGraphExecutor::executePrepared` in [`nrRenderGraphExecutor.cpp`](../src/renderer/nrRenderGraphExecutor.cpp) exposes additive Execute substages.
- **Proposal:** record full provenance, partition top-level Unclassified with narrow timers, obtain a supported non-empty Nsight Graphics GPU Trace for GPU queue execution and metrics, and separately capture CPU threads, Vulkan API calls/submissions, and system scheduling with Nsight Systems or an equivalent system profiler. Follow [Khronos profiling guidance](https://docs.vulkan.org/guide/latest/profiling.html), the [Nsight Graphics GPU Trace overview](https://docs.nvidia.com/nsight-graphics/UserGuide/gpu-trace-overview.html), [GPU Trace UI reference](https://docs.nvidia.com/nsight-graphics/UserGuide/gpu-trace-ui.html), [Nsight Graphics release notes](https://docs.nvidia.com/nsight-graphics/ReleaseNotes/index.html), and the [Nsight Systems User Guide](https://docs.nvidia.com/nsight-systems/UserGuide/).
- **Must remain dynamic:** frame ordinal/slot, CPU scheduling, queue and acquire/present events, and per-frame resource state are observations, not cache keys.
- **Dependencies:** supported driver/tool combinations, saved GPU Trace and system-timeline artifacts, and an executable hash matching the benchmark.
- **Telemetry / experiment:** CPU core/thread lanes, Vulkan calls, submits, queue lanes, acquire/present, residual timers, power/thermal state, and profiler completion.
- **Risks:** instrumentation perturbation; a connected session without openable saved artifacts from the relevant profiler is invalid evidence.
- **Exit:** proceed when Unclassified is partitioned, the completed GPU Trace opens, and the separate CPU/system timeline is analyzable; hold if any is absent; abandon only an unsupported tool path, not the evidence requirement.
- **Rollback:** keep probes opt-in and disable them if they perturb sampling.

### E1 - Executor recording-policy A/B

- **Objective / hypothesis:** test whether inline, reduced-task, or thresholded recording beats always dispatching seven recording tasks.
- **Current boundary:** `RenderGraphExecutor::launchRecordTasksForBatch` in [`nrRenderGraphExecutor.cpp`](../src/renderer/nrRenderGraphExecutor.cpp) rebuilds descriptions, assigns workers round-robin, and always calls `recordThreadPool_.submitTo`; `collectRecordTaskResults` consumes futures before replay. Per-slot command-buffer objects persist, but task/pass-plan/future/result/lookup structures do not.
- **Proposal:** one switch selects current always-worker, inline, reduced-task, or a cheap structural cost threshold; never use timing feedback from the same frame.
- **Must remain dynamic:** callbacks, bindings, frame data, resource state, current command buffers, swapchain image, frame slot, query indices, and replay order.
- **Dependencies:** G0 timeline, retained baseline, and policy-specific task/submit markers.
- **Telemetry / experiment:** inline/worker counts, utilization, submit-to-start delay, recording intervals, main-thread overlap, completion wait, Execute/CPU work, and queue timing.
- **Risks:** inline work can delay submit; too few tasks lose parallelism; tiny secondaries add overhead. NVIDIA recommends parallel recording and reasonable buffer counts but warns batching can add latency in [Vulkan Dos and Don'ts](https://developer.nvidia.com/blog/vulkan-dos-donts/).
- **Exit:** promote only a section 8 major-gate winner with no adverse valid GPU timeline; hold an inconclusive winner; abandon latency/queue regressions.
- **Rollback:** select the unchanged always-worker path.

### E2 - Structural task-plan allocation and capacity reuse

- **Objective / hypothesis:** after E1, test whether owned capacity or a value-only structural plan removes measurable allocation/planning cost.
- **Current boundary:** `RecordTaskDesc`, `RecordPassExecutionPlan`, and `RecordBatchTasks` in [`nrRenderGraphExecutor.ixx`](../src/renderer/nrRenderGraphExecutor.ixx) are reconstructed with vectors/futures each batch.
- **Proposal:** retain capacity or key an owned structural plan by compiled pass structure and policy, then materialize fresh current-frame descriptions.
- **Must remain dynamic:** every `RecordTaskDesc` reference, future, command buffer, binding, lookup, frame index, and swapchain value; never cache live `reference_wrapper`s.
- **Dependencies:** E1 winner and allocation/capacity evidence.
- **Telemetry / experiment:** allocation count/bytes, growth, materialization time, task-plan launch, Execute, and CPU work.
- **Risks:** stale references, key mismatch, or complexity exceeding savings.
- **Exit:** proceed only if E1/timeline evidence makes churn material and the isolated variant passes; otherwise hold/abandon.
- **Rollback:** clear the structural cache and rebuild current vectors.

### D1 - Consolidated scene-texture request and cache telemetry

- **Objective / hypothesis:** one frame-local request from raster and TLAS consumers removes duplicate traversal and exposes cache behavior.
- **Current boundary:** `Renderer::renderFrame` in [`nrRenderer.cpp`](../src/renderer/nrRenderer.cpp) collects raster IDs through `SceneRenderBridge::buildFrame`/`collectSceneMaterialTextureIds`, then separately calls `collectTlasSceneTextureHandles`; `RendererGlobalDescriptorTableCache` in [`nrRendererCache.ixx`](../src/renderer/nrRendererCache.ixx) already owns global descriptor key/version calculation.
- **Proposal:** union both packet sets into one owned request deduplicated by descriptor ID, pass it once to `buildSceneTextureDescriptorTable`, and expose discovery/version counters.
- **Must remain dynamic:** packet membership, residency, fallback, image/layout, texture GPU version, and current consumers.
- **Dependencies:** stable benchmark schema; independent of E1.
- **Telemetry / experiment:** raster-only/TLAS-only/union IDs, duplicates, nonresident entries, version probes/changes, and discovery/table times.
- **Risks:** losing TLAS-only textures, changing fallback ID zero, or treating nonresident textures as stable.
- **Exit:** proceed when the union equals old traversals under forced mutation and passes; hold if telemetry only enables D2; abandon unjustified ownership complexity.
- **Rollback:** run both old paths and compare their union in differential mode.

### D2 - Earlier bindless version hit and shared request

- **Objective / hypothesis:** avoid constructing equivalent request maps twice when the table version is already applied for the owner/frame slot.
- **Current boundary:** request/ensure/snapshot functions in [`nrSceneTextureTableBinding.ixx`](../src/renderPasses/nrSceneTextureTableBinding.ixx) build twice; `BindlessImageTableCache` already tracks owner/table/frame-slot state, versions, IDs, and fallback rewrites, while binding-set reallocation clears per-slot writes.
- **Proposal:** share one D1 request and add a read-only early-hit query covering owner, table, slot, version, and allocation generation; bypass copying only after reflection, binding, and allocation checks. This follows [Khronos descriptor-management guidance](https://docs.vulkan.org/samples/latest/samples/performance/descriptor_management/README.html) to avoid unnecessary updates without weakening lifecycle rules.
- **Must remain dynamic:** per-pipeline/per-slot state, fallback identity, allocation generation, removed-slot rewrites, residency, and GPU-AV refresh; `refreshActiveDescriptorsOnCacheHit` must still force diagnostic writes.
- **Dependencies:** D1 and an allocation-generation contract.
- **Telemetry / experiment:** builds/copies, early hits, miss causes, writes, fallback rewrites, reallocations, GPU-AV refreshes, Build/Prepare, and CPU work.
- **Risks:** a version-only hit can skip first slot use, a new owner, reallocation, fallback change, eviction, or diagnostic refresh.
- **Exit:** require all three slots, every miss cause, and the major gate; hold partial savings; abandon weakened validation.
- **Rollback:** build both current requests and let the existing cache decide.

### R0 - Explicit scene and extraction revisions

- **Objective / hypothesis:** authoritative mutation tokens make reuse correct without large-product comparison or static-scene assumptions.
- **Implemented boundary:** [`nrRevision.ixx`](../src/utils/nrRevision.ixx) provides composition-based fixed-storage revision sets/bundles, owned snapshots, masks/diff, typed projections/keys, explicit-commit batches, policy concepts, and stateless C++26 explicit-object syntax sugar. `Scene` owns one identity plus one global RT domain set; `ScenePacketSet` captures an immutable snapshot.
- **Mutation ownership:** one `SceneRevisionMutationPolicy` maps template/instance lifecycle, simulation, residency completion, and explicit external ECS/manual mutations to domains. Callers that mutate test fixtures or ECS state outside Scene APIs must finish through `commitExternalMutation(...)`.
- **Must remain dynamic:** camera/frustum, jitter, slot, streaming completion, and changed domains.
- **Deliberate separation:** handle generations, asset CPU/GPU versions, descriptor versions, frame serials, and retirement tokens were not migrated into revisions.
- **Validation:** fixed-storage, snapshot/projection, policy, heterogeneous-bundle, explicit-commit, scene identity, lifecycle, and external-residency contracts are covered by Debug tests.
- **Risks:** a missed mutation creates stale GPU state; an overly coarse revision erases benefit.
- **Rollback:** disable hits and force full extraction/build while retaining diagnostics.

### R1 - Revision-keyed immutable RT plan reuse

- **Objective / hypothesis:** reuse immutable RT topology, geometry/material metadata, and the exact hit-SBT plan while patching dynamic state.
- **Implemented AS plan:** `AccelerationStructureBuildRuntimeCache` owns an exact structural key and value-only plan containing instance/geometry/material metadata, material table, hit-SBT plan, and ordered instance templates. The key combines scene identity, static revision projection, ordered packet identity, and exact current mesh semantic keys; equality, not hash alone, controls hits.
- **Dynamic path retained:** every frame resolves current transforms, mask, winding flip, BLAS address, and frame slot, writes the instance buffer, and records a TLAS rebuild. The three slot-local static sideband buffers upload on first use or structural-plan generation change.
- **Scene switch safety:** changing scene identity retires the current BLAS atlas/resources and clears BLAS cached descriptors, material values, structural plan, and slot-applied generations before reused handle values can match.
- **Renderer/PathTracing integration:** Renderer caches TLAS-only texture collection with the same static/dynamic split and exact ordered packet identity. PathTracing adds `ShaderService::sessionGeneration()` to its pipeline key; the separate nested SBT key therefore cannot retain concrete group handles across reload.
- **Deferred by design:** per-asset journals, local instance/material patches, shared cross-slot GPU buffers, TLAS UPDATE, update-after-bind migration, and command-buffer reuse remain out of scope.
- **Promotion status:** runtime reuse is implemented, but the legacy full-build switch and optional differential mode are not part of this patch, so R1 is not promoted.
- **Telemetry / experiment:** compare post-change texture collection, AS metadata planning, static-sideband CPU writes, Build, and total CPU work against the baseline after all three frame slots are warm.
- **Risks:** `instanceShaderBindingTableRecordOffset` must match the exact plan; atlas repack changes addresses, streaming changes dependencies, and shader/CHS changes alter SBT layout. [RTX Best Practices](https://developer.nvidia.com/blog/rtx-best-practices/) supports content- and measurement-specific AS policy, not universal skipping.
- **Rollback:** revert or disable this patch; there is no live old-path switch until the legacy and differential modes are added.

### S1 - Incremental scene bridge and TLAS texture collection

- **Objective / hypothesis:** reuse static extraction/bridge products and rebuild only mutation-affected components.
- **Current boundary:** `SceneRenderBridge::buildFrame` in [`nrSceneBridge.cpp`](../src/scene/nrSceneBridge.cpp) creates a fresh `SceneBridgeFrame`; renderer performs raster/TLAS extraction and later TLAS texture collection, while extraction reads residency live.
- **Proposal:** split topology/geometry/material values from camera, transform, light, visibility, and residency patches; key TLAS dependencies by topology/material revisions and patch residency/version.
- **Must remain dynamic:** camera/frustum/jitter, transforms, lights, visibility/masks, streaming, constants, and live GPU references.
- **Dependencies:** R0, D1 request, and R1 key alignment.
- **Telemetry / experiment:** full/incremental counts, patches, miss causes, Scene, Unclassified texture collection, CPU work.
- **Risks:** culling/residency can make a product dynamic; references can outlive scene/device.
- **Exit:** require differential equality and the major gate; hold until revisions; abandon excessive patch complexity.
- **Rollback:** force current extraction, bridge, and texture traversal.

### M1 - Lazy successful `nrAssert` diagnostic construction

- **Objective / hypothesis:** test whether suppressing successful-path message formatting saves measurable hot cursor/descriptor time.
- **Implemented boundary:** [`errorHandle.ixx`](../src/utils/errorHandle.ixx) provides a constrained context-factory overload that invokes the factory only after a failed condition while preserving the original source location. Expensive `std::format`/`ShaderCursor::debugSummary()` contexts in [`nrDescriptor.cpp`](../src/rhi/nrDescriptor.cpp) use that lazy form.
- **Remaining experiment:** count suppressed constructions and A/B the converted implementation against the eager baseline before claiming a measurable CPU improvement.
- **Must remain dynamic:** condition and complete failure diagnostic; reporting stays in `nr.utils:errorHandle`.
- **Dependencies:** counters/profiler evidence and isolated A/B for promotion.
- **Telemetry / experiment:** suppressed constructions, failure golden tests, affected bucket, CPU work.
- **Risks:** broad conversion, changed diagnostics, or complexity for sub-`0.05 ms`; measurable impact is not yet proven.
- **Exit:** retain the implementation only with complete failure diagnostics; promote it as a performance result only if the micro-improvement gate passes without displacing E1/D1/R0/R1.
- **Rollback:** convert the affected sites back to eager string contexts and remove the factory overload.

### P2 - Present and Prepare boundary attribution

- **Objective / hypothesis:** separate application work from Vulkan, driver, OS scheduling, pacing, and WSI before optimizing.
- **Current boundary:** `Renderer::renderFrame` times prepare, execute, and `Device::presentFrame`; `prepareFrame` handles pre-acquire passes, while `executePrepared` acquires at the exact first swapchain boundary, resolves it, and invokes deferred prepare. `Device::submitFrameBatch` decouples pre-present work.
- **Proposal:** mark prepare categories, acquire, final swapchain submit, present call, and WSI/OS intervals in a valid trace.
- **Must remain dynamic:** image, extent/format checks, acquire result/pre-acquired option, semaphore, boundary metadata, pacing, and recreation.
- **Dependencies:** G0 trace and unchanged two-phase prepare/acquire semantics.
- **Telemetry / experiment:** callback categories, API duration, scheduling delay, results, submit-to-present, and WSI/queue lanes.
- **Risks:** moving the boundary can add latency or break synchronization while relabeling time.
- **Exit:** propose a change only after attribution finds a controllable interval meeting threshold; otherwise hold.
- **Rollback:** remove probes and preserve the boundary.

## 7. Mutation and correctness matrix

The matrix is mandatory for R0/R1/S1 and relevant descriptor variants.

| Mutation | Products invalidated | Rebuild versus patch | Required validation |
|---|---|---|---|
| No change / frame-slot rollover | Per-slot descriptor application and dynamic frame data only | Reuse stable products; initialize/patch current slot | All three slots, first-use counters, identical image |
| Transform-only | Raster transforms, bounds/culling result if applicable, TLAS instances | Patch transform; rebuild active list only if culling changes | Motion sequence, mirrored transform flags, image diff |
| Camera/frustum | Camera constants, culling selection, temporal state | Patch camera; rerun camera-dependent selection | Frustum boundary sweep, jitter/history behavior |
| Visibility/mask | Draw/TLAS membership and instance mask | Patch mask or rebuild active ordering | Toggle every visibility bit; ray/raster agreement |
| Add/remove instance | Topology, ordering, metadata indices, hit plan | Rebuild affected topology/plan | Add/remove at first/middle/last positions |
| Mesh replacement | Geometry plan, BLAS, metadata, texture/material dependencies | Rebuild affected mesh/BLAS/plan | Indexed/non-indexed and geometry-count changes |
| BLAS rebuild / atlas repack | BLAS address generation and all dependent TLAS instances | Rebuild BLAS; patch addresses; retire old atlas | Forced grow/repack, in-flight retirement, GPU-AV |
| Material update | Compiled material, geometry-material map, permutation/hit plan as needed | Rebuild affected material/records | Feature/alpha/CHS permutation changes |
| Texture upload/replacement/eviction/residency change | Descriptor table/version, texture IDs, material dependencies | Patch descriptor; rebuild request dependency when identity changes | Fallback-to-resident-to-evicted cycle in every slot |
| Shader/CHS permutation/SBT record-plan change | Pipeline key, SBT, exact hit-record offsets | Rebuild pipeline/SBT/plan | ABI mismatch rejection and record-offset equality |
| Graph/pipeline/config change | Graph products and config-dependent RT/descriptor state | Rebuild keyed products | Each supported pipeline/config switch |
| Resolution/DLSS/swapchain recreation | Extent-dependent resources, temporal state, swapchain bindings | Rebuild extent resources; late-bind new swapchain image | Resize, DLSS switch, out-of-date smoke, history reset |
| Scene unload/reload/device teardown | All scene/node/device-owned caches and references | Clear, retire safely, rebuild from new identity | Repeated unload/reload and device/node shutdown |

## 8. Experiment protocol and decision gates

### 8.1 Fixed sampling workload

- Sponza through the `rtobject` pipeline;
- Release build with validation disabled for performance sampling;
- `1920 x 1080` display to `640 x 360` render extent;
- DLSS Ultra Performance;
- visible-static UI;
- three frames in flight;
- 600 accepted warmup frames and 1200 accepted measured frames.

Each run must preserve valid artifact counts, expected node/pass topology, and zero missing,
partial, duplicate, extra, invalid, or schema-drifting GPU rows.

### 8.2 Provenance record

Record the Git commit and dirty-state hash, executable hash, CPU model, process affinity,
power plan and AC state, CPU/GPU clocks, temperatures and throttling indicators, GPU and
driver, exact commands, wall-clock timestamps, build configuration, validation mode, and
profiler/tool versions.

Run one change at a time. Use counterbalanced baseline/variant order over at least four
independent pairs, such as `B/V`, `V/B`, `B/V`, `V/B`, rather than consecutive aggregate
runs that confound drift.

### 8.3 Endpoints and correctness

Primary endpoints are CPU work and the affected top-level bucket's mean, p50, and p95.
p99/max and nested timers are diagnostics, not promotion endpoints. Each variant also
records its own mechanism counters.

Correctness requires:

- valid artifacts and counts with no missing GPU rows;
- expected pass, submit, task, packet, and instance topology;
- deterministic image/checksum or a bounded image-diff policy;
- a separate validation and GPU-AV error-free diagnostic run;
- the full forced invalidation matrix in section 7;
- no stale references or retirement errors through three frames in flight.

### 8.4 Engineering decision policy

**Proceed (major package):** paired median improvement of at least **0.10 ms** in CPU work
or the affected top-level bucket, direction consistent in at least three of four pairs, no
CPU-work p95 regression greater than `0.05 ms`, no correctness failure, and, for executor
or submit changes, no adverse valid GPU timeline.

**Hold and re-measure:** `0.05-0.10 ms`, inconsistent direction, improvement visible only
in a nested timer, or incomplete provenance.

**Abandon and revert:** below `0.05 ms`, material CPU-work p95/GPU/correctness regression,
or complexity unjustified by the result. A very low-risk micro-improvement may be retained
if repeatable, but it must not displace larger work.

These thresholds are an engineering decision policy derived from the observed
`0.095587333 ms` (approximately `0.096 ms`) difference between the two current CPU-work
means. They are not a claim of statistical significance.

## 9. Dependency and implementation phases

### Phase 0 - Measurement gate

Deliver G0: complete provenance, supported valid timeline, and partitioned Unclassified.
No optimization promotion occurs without this baseline.

### Phase 1 - Independent policy and request experiments

Implement independently switchable E1 and D1 variants. Add mechanism counters. D2 may
follow D1 after allocation-generation requirements are explicit. Retain the old paths.

### Phase 2 - Revision contract

Implement R0 revision ownership and Debug differential validation. Exercise the complete
mutation matrix before enabling any reuse hit in sampling.

### Phase 3 - Stable-plan and incremental reuse

Implement R1 immutable RT plan reuse/dynamic patch, then S1 incremental bridge/extraction
paths. E2 is allowed only if Phase 1 evidence shows structural task-plan churn remains
material.

### Phase 4 - Boundary attribution and reconsideration

Perform P2 Present/Prepare attribution. Reconsider submit merging, transient-resource reuse,
or compile work only if new timeline/allocation evidence makes them material.

Every implementation package needs a feature/config switch, the old path, and an optional
differential mode until promotion. R1 currently lacks those promotion controls. Later changes to stable runtime boundaries must update
[`docs/architecture/README.md`](architecture/README.md) and any linked topic document in
the same patch.

## 10. Explicit deferrals, rejections, and revisit triggers

- Do not sum GPU passes across queues. Revisit GPU critical-path statements only after a
  valid timeline.
- Do not claim a queue gap from Wait GPU or timestamp rows. Revisit after G0.
- Do not merge submits from the nested approximately `0.125 ms` submit measurement alone.
  Revisit only if a valid trace proves preserved ownership, WSI boundaries, overlap, and
  lower end-to-end latency.
- Do not blindly skip TLAS build or assume update is always legal/faster. Revisit after
  revisions classify topology and dynamic content and an A/B chooses the policy.
- Do not reuse recorded command-buffer contents. Object reuse/reset with
  `eOneTimeSubmit` remains the contract.
- Do not migrate to descriptor buffers solely because they are newer. Revisit only with a
  measured descriptor bottleneck and a lifecycle design superior to current caches.
- Do not rewrite the compile cache while Compile remains approximately `0.016 ms` p50 and
  `RenderGraphCompileCache` is already used.
- Do not redesign managed transient allocation until allocation frequency/bytes and CPU
  attribution make it material. That work is an RHI allocator redesign, not a local node
  cache.
- Do not combine packages into a mega patch. Revisit integration only after isolated
  promotion gates pass.

## 11. Open questions and next review checkpoint

The next review must resolve:

1. Can a supported Nsight setup produce a non-empty, openable trace, and what CPU/GPU
   relationship does it show?
2. What functions and scheduler/API intervals compose the `0.244 ms` Unclassified mean?
3. Which E1 policy wins across four counterbalanced pairs without delaying GPU work?
4. Which concrete owner and API advance each R0 revision, especially topology versus
   transforms, residency, and allocation generation?
5. Which extraction/RT products are provably static, patchable, or necessarily rebuilt?
6. How much of Present and Prepare is controllable application work versus API/driver/WSI?

The roadmap is complete when G0 evidence is valid; every revision has one authoritative
owner and mutation coverage; E1/D1/D2/R1/S1/P2 have explicit proceed, hold, or abandon
records from the common protocol; promoted paths pass the full correctness matrix; old
paths are removed only after promotion; architecture-facing boundaries are documented;
and deferred candidates have either a new evidence-based trigger or a recorded rejection.
