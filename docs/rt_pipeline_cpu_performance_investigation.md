# RT Pipeline CPU Performance Investigation

> Investigation date: 2026-07-24  
> Scope: CPU-side work performed by the real-time ray tracing pipeline on every frame  
> Primary scenario: DLSS Ray Reconstruction, Ultra Performance preset  
> Status: investigation and recommendations only; no optimization has been implemented
>
> Historical evidence note: implementation descriptions and recommendations in this
> document are preserved as observed on the investigation date. Current implementation and
> promotion status live in
> [RT Pipeline CPU Optimization Directions](rt_pipeline_cpu_optimization_directions.md).

## 1. Executive Summary

The Ultra Performance capture is CPU-bound.

The captured frame spends `5.230 ms` on the CPU, of which only `0.015 ms` is reported as
`CPU Wait GPU`. The CPU therefore performs approximately `5.215 ms` of non-waiting work.
The GPU pass timings shown in the capture add up to approximately `2.012 ms`; this sum is
only a rough comparison because passes on different queues may overlap and the sum is not
the GPU critical path.

The strongest findings are:

1. `Build` is the largest top-level CPU bucket at `2.052 ms`, or `39.2%` of the captured
   frame.
2. `0.805 ms`, or `15.4%`, is outside the existing top-level named buckets. A detailed
   diagnostic run placed most of the comparable gap between `Scene` and `Build`, especially
   in TLAS-referenced texture/material collection.
3. The render graph, node declarations, scene texture tables, shader cursor paths, and
   significant RT metadata are reconstructed every frame even when the graph shape and most
   scene content are stable.
4. With `dirty BLAS = 0`, the CPU still scans 103 TLAS packets/instances, rebuilds instance,
   material, texture-reference and hit-SBT metadata, queries TLAS sizes, writes CPU-visible
   buffers, and declares the TLAS pass.
5. `nrAssert` is an ordinary function. Diagnostic arguments such as `std::format(...)` and
   `ShaderCursor::debugSummary()` are evaluated before the function observes that the
   assertion succeeded. Static call-path counting shows a large guaranteed amount of
   successful-path diagnostic construction in descriptor binding code.
6. The executor launches 7 secondary-recording tasks and submits 3 batches per frame in the
   tested graph. The measured future wait includes useful worker recording, so it must not be
   classified entirely as scheduling overhead. A controlled A/B test is required.
7. Steady-state graph compilation is already inexpensive (`0.048 ms` in the capture).
   GPU TLAS build is also inexpensive (`0.112 ms`). Neither is a first-order target.

The recommended order is:

1. close the remaining measurement gaps and capture a CPU/GPU timeline;
2. remove eager successful-path diagnostic work and cache stable shader binding plans;
3. introduce revision-based RT metadata and descriptor-table reuse;
4. reuse the stable graph skeleton and trim scene extraction for the active RT graph;
5. tune command recording only after an executor A/B experiment identifies the winning
   policy.

## 2. Question and Success Criteria

The investigation answers three questions:

1. Which CPU stages consume the frame under DLSS Ultra Performance?
2. Which repeated operations in the current implementation explain those timings?
3. Which changes are likely to reduce CPU time without moving the bottleneck or weakening
   correctness?

The target outcome is not merely a lower average CPU number. A successful optimization
should:

- reduce steady-state CPU frame time and its `p95`/`p99`;
- remove GPU idle gaps attributable to late CPU submission;
- preserve graph synchronization, descriptor correctness, scene streaming, and material
  replacement behavior;
- avoid increasing submission count or command-buffer overhead;
- retain deterministic diagnostics outside the hot path.

## 3. Test Environment and Evidence

### 3.1 Observed environment

- Operating system: Windows
- GPU reported by the diagnostic executable: NVIDIA GeForce RTX 5060 Ti
- Logical processor count: 12
- Generated record-worker ceiling: `NR_MAX_THREADS = 12`
- Frames in flight: 3
- DLSS preset: Ultra Performance
- Internal render resolution: `640 x 352`
- Output target: `1920 x 1055`
- Stable graph submission shape: 7 record tasks and 3 submit batches per frame
- Stable RT scene: 103 TLAS packets/instances per frame
- Stable BLAS state: `dirty BLAS = 0`

The project architecture targets RTX 5070 Ti-class hardware, but this experiment was
observed on a 5060 Ti. Absolute timings should therefore not be generalized to the target
GPU without repeating the capture there. The CPU-side structure and repeated-work findings
remain applicable.

### 3.2 Evidence sources

The report uses three evidence classes:

1. **Capture baseline**: the user-provided Ultra Performance screenshot.
2. **Temporary timing experiment**: 300-frame aggregation probes around renderer, graph,
   AS, prepare and execute boundaries.
3. **Source audit**: direct inspection of the per-frame call path and a lower-bound count of
   eager descriptor diagnostics.

Raw experiment output is retained locally at:

- `build/perf_probe_stderr.log`
- `build/perf_probe_stdout.log`

Temporary probes added specifically for the investigation were removed after collection.
Pre-existing working-tree probes were preserved. No optimization was implemented.

## 4. Capture Baseline

### 4.1 CPU frame

| Stage | Time | Share of `5.230 ms` |
|---|---:|---:|
| CPU Wait GPU | 0.015 ms | 0.3% |
| Frame Setup | 0.074 ms | 1.4% |
| Scene | 0.455 ms | 8.7% |
| Build | 2.052 ms | 39.2% |
| Compile | 0.048 ms | 0.9% |
| Prepare | 0.524 ms | 10.0% |
| Execute | 0.869 ms | 16.6% |
| Present | 0.388 ms | 7.4% |
| Unclassified | 0.805 ms | 15.4% |
| **Total** | **5.230 ms** | **100.0%** |

`Unclassified` is calculated as:

```text
5.230 - (0.015 + 0.074 + 0.455 + 2.052 + 0.048 + 0.524 + 0.869 + 0.388)
= 0.805 ms
```

`CPU Wait GPU` means time during which the CPU waits for GPU completion. Its low value is
strong evidence that the CPU is not being throttled by prior GPU work in this capture.
It does not, by itself, prove the exact location or duration of a GPU idle bubble. A GPU
queue timeline is still required because swapchain acquisition, present pacing, and queue
dependencies can produce similar frame-level symptoms.

### 4.2 Visible GPU passes

| GPU pass | Queue | Time |
|---|---|---:|
| ASBuild.BuildTLAS | Graphics | 0.112 ms |
| LightPrepare.Upload | Graphics | 0.002 ms |
| PathTracing.Trace | Graphics | 0.689 ms |
| Ui.Overlay | Graphics | 0.018 ms |
| DLSS.RayReconstruction | Compute | 1.119 ms |
| Present.Convert | Compute | 0.049 ms |
| Present.CopyToSwapchain | Compute | 0.023 ms |
| **Naive sum** | | **2.012 ms** |

The naive sum is not a substitute for a critical-path trace. Graphics and compute work may
overlap, and the capture does not expose queue idle intervals or WSI timing.

## 5. Experimental Method

### 5.1 Probe placement

The experiment measured:

- renderer total and the time left outside named top-level stages;
- scene begin/upload, raster extraction, TLAS extraction, bridge construction, and
  TLAS-referenced texture collection;
- graph prelude, UI collection, node loop, and each node's `build(...)`;
- AS cache scan, metadata/SBT planning, CPU writes, TLAS size query and graph declaration;
- prepare transfer policy, plan creation, runtime resource resolution and callbacks;
- execute setup, acquire boundary, task launch, task completion, replay, primary recording,
  submit and finalization.

### 5.2 Sampling

- A probe emitted one average after 300 frames.
- Warm-up and preset-transition samples were excluded.
- The Ultra Performance stable region after the transition was retained.
- Depending on the probe, 50 or 51 stable 300-frame windows remained.
- Reported `P25`, `P50`, and `P75` are quantiles across **300-frame window means**.

They are not per-frame tail percentiles. In particular, the reported `P75` cannot be
interpreted as frame-time `p75`, and no valid per-frame `p95` or `p99` is available from this
experiment.

### 5.3 Perturbation and comparability

The diagnostic run and the user capture did not have identical UI state. Probe printing also
occurs once per 300 frames and can perturb a following window. Consequently:

- use the screenshot as the top-level baseline;
- use the probe statistics to identify structure and relative cost;
- do not add detailed probe medians directly to the screenshot buckets;
- repeat the experiment with per-frame samples stored in memory before making a savings
  claim.

## 6. Detailed Experimental Results

### 6.1 Largest measured sub-stages

| Sub-stage | P25 | P50 | P75 | Interpretation |
|---|---:|---:|---:|---|
| Graph node build loop | 1.034 ms | 1.190 ms | 1.445 ms | Dominant measured build boundary |
| TLAS texture/material collection | 0.415 ms | 0.469 ms | 0.568 ms | Between top-level Scene and Build |
| PathTracing node build | 0.414 ms | 0.463 ms | 0.552 ms | Mostly graph/binding declaration work |
| AccelerationStructureBuild node | 0.376 ms | 0.454 ms | 0.548 ms | Metadata planning dominates |
| Record task completion/critical path | 0.316 ms | 0.332 ms | 0.356 ms | Includes useful worker recording |
| AS metadata and hit-SBT planning | 0.242 ms | 0.268 ms | 0.321 ms | Rebuilt with no dirty BLAS |
| Prepare callbacks/binding boundary | 0.212 ms | 0.232 ms | 0.265 ms | About 79% of detailed Prepare median |
| SceneRenderBridge construction | 0.172 ms | 0.194 ms | 0.241 ms | Full bridge built each frame |
| Queue submit, 3 batches | 0.130 ms | 0.147 ms | 0.168 ms | Non-trivial but not dominant |
| Record task launch, 7 tasks | 0.092 ms | 0.103 ms | 0.114 ms | Fixed per-frame scheduling cost |
| BLAS cache scan | 0.053 ms | 0.058 ms | 0.068 ms | Persists with no dirty BLAS |
| Managed runtime resource resolution | 0.036 ms | 0.041 ms | 0.047 ms | One transient image per frame |

The PathTracing and AccelerationStructureBuild node medians together explain roughly 60% of
the diagnostic run's `Build` median.

### 6.2 Scene and the unclassified interval

The diagnostic-run median scene components are approximately:

| Scene component | Approximate median |
|---|---:|
| Scene begin/upload | 0.01 ms |
| Raster extraction | 0.05 ms |
| TLAS extraction | 0.03 ms |
| SceneRenderBridge | 0.19 ms |

After the top-level `Scene` timer ends, the renderer collects texture handles referenced by
the TLAS packets before the `Build` timer begins. This costs about `0.469 ms` at the median.
In the diagnostic run, the comparable unclassified median was about `0.516 ms`, so this one
operation explains approximately 91% of that run's gap.

The capture's unclassified value is larger (`0.805 ms`). It must not be replaced by the
diagnostic-run value because the two runs have different state. The source placement still
shows that this operation belongs in a missing named top-level bucket.

Relevant code:

- [`Renderer::renderFrame`](../src/renderer/nrRenderer.cpp), near the
  `collectTlasSceneTextureHandles(...)` call around line 1991.
- TLAS texture/material traversal helpers near lines 128-211 of the same file.

### 6.3 Build

Every frame, `Renderer::buildInstalledGraph(...)` currently:

1. clears the graph builder;
2. re-imports persistent resources;
3. uploads frame constants;
4. rebuilds the scene texture descriptor table;
5. copies the full `SceneBridgeFrame` into graph frame data when present;
6. collects every node's UI section;
7. creates fresh `std::map` instances for frame resources and frame data;
8. adds every installed node and invokes every node's `build(...)`;
9. recreates submit-node declarations.

Relevant code:

- [`Renderer::buildInstalledGraph`](../src/renderer/nrRenderer.cpp), around lines 2193-2290.

The graph shape is largely stable for a selected renderer configuration. Rebuilding the
declaration is therefore a likely source of avoidable CPU work. This does not mean the
compiled graph can be blindly retained: dynamic extents, imported handles, resource state,
scene revisions, DLSS mode and swapchain state must remain part of the cache contract.

### 6.4 Acceleration structure and RT metadata

The stable sample has:

- 103 TLAS packets/instances per frame;
- no dirty BLAS;
- GPU TLAS build time of `0.112 ms`;
- CPU AS node median of `0.454 ms`;
- CPU AS metadata/SBT planning median of `0.268 ms`.

Even with no dirty BLAS, the build stage performs work that includes:

- scanning and pruning BLAS cache state;
- planning instances;
- resolving materials and texture references;
- rebuilding hit-SBT records;
- rebuilding RT instance, geometry, material-header, layer and texture-reference arrays;
- writing per-frame CPU-visible buffers;
- querying TLAS build sizes;
- declaring and importing AS graph resources;
- declaring a TLAS build pass.

Relevant code:

- [`AccelerationStructureBuildNode::build`](../src/renderPasses/AccelerationStructureBuild/nrAccelerationStructureBuildNode.cpp),
  around lines 1043-1547.
- [`queryTlasBuildSizes`](../src/rhi/nrAccelerationStructure.cpp), around lines 268-300.

This is primarily a CPU metadata reuse problem, not a GPU AS-build problem.

The data naturally separates into:

- **static or revision-controlled**: mesh geometry metadata, material compilation, material
  layers, texture-reference lists, BLAS cache entries, most hit-SBT record structure;
- **dynamic**: instance transform, visibility/mask, active instance list, TLAS instance
  buffer, per-frame TLAS build/update command;
- **occasionally dynamic**: texture residency, material replacement, mesh streaming,
  shader/pipeline revision.

An optimization must key each cached artifact against the revisions that can invalidate it.
Frame index alone is not a correctness key.

### 6.5 Descriptor and ShaderCursor work

`nrAssert` is declared as a normal inline function:

- [`nrAssert`](../src/utils/errorHandle.ixx), around lines 36-53.

In C++, function arguments are evaluated before entering the function. Therefore calls like:

```cpp
nrAssert(condition, std::format("... {}", cursor.debugSummary()));
```

pay for `std::format(...)` and `debugSummary()` even when `condition` is true.

The hot `ShaderCursor` functions contain many such contexts:

- [`ShaderCursor::debugSummary`](../src/rhi/nrDescriptor.cpp), around line 1339;
- field lookup and validation around lines 1370-1429;
- path lookup around lines 1568-1632;
- descriptor writes around lines 1739-1769;
- push-data writes around lines 1777-1855.

A conservative static count for the currently audited simple-root binding paths found:

#### Build, PathTracing plus Present

- 23 descriptor bindings and 2 push bindings;
- at least 646 `std::format` calls per frame;
- at least 298 `debugSummary()` calls per frame;
- at least 25 root-field-list constructions;
- at least 46 descriptor-type-list constructions;
- at least 46 binding-description constructions.

#### Steady Prepare

With `U` active UI descriptors:

- at least `160 + 33U` `std::format` calls per frame;
- at least `12 + 10U` `debugSummary()` calls per frame.

#### First use of a frame slot

When the UI table is non-empty, with `T` active scene texture descriptors and `U` active UI
descriptors:

- at least `62,670 + 27T + 30U` `std::format` calls;
- at least `20,492 + 10T + 10U` `debugSummary()` calls.

The large constants come from filling descriptor-table fallback capacity, currently 1024,
and this first-use path repeats for each of the three frame slots.

#### Record

For `D` actual UI texture switches:

- at least `25D` `std::format` calls.

These are lower-bound call counts, not measured milliseconds. They depend on the audited
binding paths and should be re-counted if reflection or binding behavior changes. They are
nevertheless direct evidence of successful-path work that has no rendering value.

### 6.6 Bindless scene texture table

The current path copies descriptor mappings more than once:

1. renderer creates a global descriptor map;
2. `makeSceneTextureTableBindingInput(...)` copies that map;
3. `makeSceneTextureTableBindingRequest(...)` creates another request map;
4. `ensureTableForFrame(...)` and `makeSnapshotForFrame(...)` each construct a request;
5. the cache can identify a version hit only after this request construction.

Relevant code:

- [`nrSceneTextureTableBinding.ixx`](../src/renderPasses/nrSceneTextureTableBinding.ixx),
  around lines 47-132.
- [`BindlessImageTableCache`](../src/renderer/nrRendererCache.cpp), around lines 435-609.

This weakens the main benefit of the version cache: the Vulkan descriptor update may be
avoided, but much of the CPU-side request preparation has already happened.

### 6.7 Prepare

The `Prepare` baseline is `0.524 ms`. In the detailed run, the callbacks/binding boundary has
a median of `0.232 ms` and represents about 79% of the detailed Prepare median.

This aligns with the source audit:

- descriptor-backed resources are prepared here;
- reflected cursor paths are repeatedly resolved;
- bindless table requests and snapshots are built;
- one managed transient image is resolved each frame.

Managed transient resolution is only about `0.041 ms`, so descriptor/binding preparation is
the stronger initial target.

### 6.8 Execute and command recording

The stable graph launches 7 record tasks and submits 3 batches:

1. graphics work;
2. compute work containing DLSS and `Present.Convert`, before the acquire boundary where
   applicable;
3. compute `CopyToSwapchain`, after swapchain acquisition.

Measured medians:

| Execute component | Median |
|---|---:|
| Task launch | 0.103 ms |
| Task completion/critical path | 0.332 ms |
| Queue submit | 0.147 ms |

The task completion interval includes actual secondary command-buffer recording on worker
threads. It is not a pure wait penalty and cannot be added to a separate `primary_record`
value because the intervals are nested.

Relevant code:

- task submission in [`nrRenderGraphExecutor.cpp`](../src/renderer/nrRenderGraphExecutor.cpp),
  around lines 2281-2297;
- future completion and result collection around lines 2312-2330.

The device prepares command-pool slots for graphics, compute and transfer roles across the
record workers, up to 36 pool slots per frame slot on this 12-thread configuration, even
though the stable graph has no transfer submit batch. The capture's `Frame Setup` is only
`0.074 ms`, so this is a secondary optimization unless a more detailed counter shows a
tail-latency issue.

### 6.9 Compile, transient resources and presentation

- `Compile = 0.048 ms`: the steady-state graph compile cache is effective. Cache signature
  construction and map/template copying remain visible in source, but this is not a priority
  until larger buckets are reduced.
- Managed transient resolution median is `0.041 ms` for one transient image, the DLSS output.
  Resource reuse is valid future work but cannot explain the current multi-millisecond gap.
- `Present = 0.388 ms`: this needs decomposition into API call time, WSI/pacing behavior and
  any CPU work around the present operation.
- Swapchain acquisition attribution differs by graph/resolution path. A resolution resolver
  can force pre-acquire into Frame Setup; another path acquires in Execute. Cross-pipeline
  comparisons are invalid until acquire is reported as its own stage.

The steady render loop already uses frame fences and timeline semaphores rather than
unconditionally calling `waitIdle` each frame. Existing `waitIdle` calls are associated with
non-steady transitions such as shutdown or reconfiguration and are not the measured
steady-state cause.

## 7. Vulkan and NVIDIA Best-Practice Mapping

### 7.1 Descriptor management

Khronos recommends caching and reusing descriptor sets rather than allocating, freeing and
rewriting them every frame. It also recommends consolidating data where appropriate to
reduce descriptor and allocation pressure.

Current mapping:

- Vulkan descriptor writes can already be skipped on a version hit.
- CPU request construction, map copying, cursor lookup and diagnostics still happen before
  or around the hit.
- The first optimization should strengthen the existing cache fast path, not immediately
  replace the descriptor model.

Source:

- [Khronos Vulkan Samples: Descriptor Management](https://docs.vulkan.org/samples/latest/samples/performance/descriptor_management/README.html)

### 7.2 Command buffers and CPU parallelism

Khronos recommends:

- multithreaded secondary recording only when there is enough work to amortize overhead;
- avoiding excessive secondary command-buffer splitting;
- avoiding worker oversubscription;
- resetting command pools in bulk rather than resetting individual command buffers;
- using one-time-submit behavior for command buffers recorded once.

NVIDIA likewise recommends explicit application-level parallelism because the Vulkan driver
will not infer a task graph for the application, while also recommending sensible submission
batching.

Current mapping:

- 7 tasks and 3 submits are measurable but not proven excessive.
- The graph is small enough that an adaptive cost threshold may outperform unconditional
  secondary recording.
- Increasing worker count without an A/B experiment is not justified.

Sources:

- [Khronos Vulkan Samples: Command Buffer Usage](https://docs.vulkan.org/samples/latest/samples/performance/command_buffer_usage/README.html)
- [NVIDIA: Vulkan Dos and Don'ts](https://developer.nvidia.com/blog/?p=14696)

### 7.3 Synchronization and waiting

Khronos recommends using per-frame fences/semaphores and avoiding `vkQueueWaitIdle` or
`vkDeviceWaitIdle` in the main loop because an idle wait drains useful overlap.

Current mapping:

- the steady renderer already follows a frame-ring/timeline approach;
- optimization should preserve this structure;
- any change to submission batching must be validated on the queue timeline.

Source:

- [Khronos Vulkan Samples: Wait Idle](https://docs.vulkan.org/samples/latest/samples/performance/wait_idle/README.html)

### 7.4 Pipeline creation and cache

Pipeline creation should be cached and, where necessary, moved off the critical rendering
path. The current steady compile bucket is already only `0.048 ms`.

Current mapping:

- retain the existing compile cache;
- consider persistent cache warm-up or asynchronous creation for configuration-change spikes;
- do not spend first-order effort on steady compile.

Source:

- [Khronos Vulkan Samples: Pipeline Cache](https://docs.vulkan.org/samples/latest/samples/performance/pipeline_cache/README.html)

### 7.5 Barriers and async compute

Barriers should use the narrowest correct stage/access scope. Async compute is valuable only
when queue overlap exists and dependencies do not serialize the work.

Current mapping:

- no evidence identifies barrier construction or GPU barrier stalls as the primary issue;
- DLSS and presentation use compute submissions, but only a timeline can show whether this
  produces overlap or bubbles;
- submission merging must not be proposed solely from CPU submit cost.

Sources:

- [Khronos Vulkan Samples: Pipeline Barriers](https://docs.vulkan.org/samples/latest/samples/performance/pipeline_barriers/README.html)
- [Khronos Vulkan Samples: Async Compute](https://docs.vulkan.org/samples/latest/samples/performance/async_compute/README.html)

### 7.6 Ray tracing acceleration structures

NVIDIA recommends separating static and dynamic acceleration-structure policies and choosing
build/update flags according to actual mutation behavior.

Current mapping:

- BLAS caching is active and no BLAS is dirty in the stable sample;
- GPU TLAS build is already small;
- the larger opportunity is revision-caching the CPU metadata that feeds the TLAS and SBT.

Source:

- [NVIDIA: RTX Best Practices](https://developer.nvidia.com/blog/rtx-best-practices/)

### 7.7 Profiling

Khronos recommends CPU profilers for host code and Vulkan timestamps for GPU work. NVIDIA
Nsight Systems is appropriate for CPU threads, Vulkan submissions and queue gaps; Nsight
Graphics is appropriate for a frame-level GPU inspection.

Sources:

- [Khronos Vulkan Guide: Profiling](https://docs.vulkan.org/guide/latest/profiling.html)
- [NVIDIA Nsight Systems User Guide](https://docs.nvidia.com/nsight-systems/pdf/UserGuide.pdf)
- [NVIDIA Nsight Graphics](https://docs.nvidia.com/nsight-graphics/)

## 8. Recommended Improvement Plan

No item in this section has been implemented.

### P0.1 Establish a trustworthy per-frame timeline

Add a common frame ordinal and configuration revision to all CPU/GPU samples. Store per-frame
samples in memory and report:

- count;
- mean;
- median;
- `p95`;
- `p99`;
- maximum;
- number of frames exceeding 0.25, 1 and 4 ms for each stage.

Split new top-level stages for:

- post-scene TLAS texture/material collection;
- swapchain acquire;
- present;
- frame-slot fence wait;
- resource-pool/VMA cleanup;
- command-pool reset;
- record task queue delay, active recording and join;
- queue submit per batch.

Record accompanying counters:

- scene packets, TLAS instances, materials and textures;
- dirty BLAS count;
- descriptor-table active count and capacity;
- cache-hit/miss count;
- record tasks, workers and submit batches;
- command pools reset by role;
- managed transient images/buffers;
- UI texture switches.

Then capture one representative Ultra Performance frame with:

1. Nsight Systems for CPU threads, Vulkan API calls, submissions and GPU queue gaps;
2. Nsight Graphics for the frame's queue and pass dependency structure.

Acceptance criterion:

- a GPU idle claim must identify the idle interval, the preceding CPU dependency and the
  submission that closes the gap.

### P0.2 Make successful diagnostics lazy

Candidate direction:

- change assertion/reporting APIs so expensive context is produced only on failure;
- pass a lambda, deferred formatter or compile-time policy rather than an already-built
  `std::string`;
- remove repeated `debugSummary()` and reflection-list construction from successful
  ShaderCursor operations;
- preserve complete diagnostics when the condition actually fails.

Experiment:

1. baseline eager diagnostics;
2. lazy assertion context only;
3. lazy context plus pre-resolved binding plan.

Measure Build, Prepare and first-use frame-slot spikes separately.

Acceptance criterion:

- identical failure diagnostics in a forced-failure test;
- lower Build/Prepare CPU distributions;
- no descriptor or push-constant behavior change.

### P0.3 Cache static RT metadata by revision

Introduce explicit revisions for:

- scene mesh topology/geometry residency;
- material assignment and compiled RT material;
- texture reference and residency;
- instance membership;
- instance transform/mask;
- shader/pipeline/SBT ABI.

Separate caches for:

- mesh/geometry metadata;
- material headers/layers/texture references;
- hit-SBT record plan;
- active instance plan;
- per-frame TLAS instance buffer.

Only the dynamic subset should be rebuilt when transforms change. A static scene should
reuse all static metadata and update only data required by the selected TLAS policy.

Required correctness experiments:

- transform-only animation;
- add/remove instance;
- material replacement;
- texture streaming/residency change;
- mesh replacement;
- shader or SBT layout change;
- frame-slot rotation.

Acceptance criterion:

- static-scene AS metadata/SBT planning approaches zero steady-state work;
- each mutation invalidates exactly the dependent cache;
- GPU output and validation behavior remain unchanged.

### P0.4 Move descriptor version checks ahead of request construction

Candidate direction:

- carry table identity, version and descriptor count to the earliest call boundary;
- return on a frame-slot version hit before copying descriptor maps;
- construct the request once and share it between prepare and snapshot;
- pre-resolve stable shader symbols/paths into a binding plan;
- retain fallback-descriptor correctness for sparse tables.

Acceptance criterion:

- a steady frame with unchanged scene/UI versions performs no descriptor-map rebuild and no
  Vulkan descriptor update;
- material/texture/UI changes update the correct frame slots;
- first-use behavior remains valid across all three frame slots.

### P1.1 Reuse the stable render-graph skeleton

Candidate direction:

- cache pass/resource declarations keyed by renderer configuration, resolution class, graph
  installation revision and relevant pipeline revisions;
- patch imported handles, frame constants, active frame data and retained resource states;
- keep compile cache and graph-declaration cache as separate contracts.

Invalidation cases:

- DLSS/DLAA preset or resolution-plan change;
- node installation/configuration change;
- swapchain format/extent change;
- shader/pipeline revision;
- resource lifetime or usage-intent change.

Acceptance criterion:

- stable frames skip node declaration work;
- transitions rebuild once;
- graph validation and synchronization plans match the uncached path.

### P1.2 Trim scene extraction and bridge data for the active graph

The RT graph should request the minimum scene products it consumes.

Candidate direction:

- declare graph-level scene-data requirements;
- skip raster extraction when no active node consumes raster draws;
- avoid constructing/copying a full `SceneBridgeFrame` when only camera/light information is
  required;
- cache bridge products by scene/camera/light revision where appropriate.

Acceptance criterion:

- RT output is unchanged;
- `Scene` and bridge timings fall;
- raster graphs continue to receive complete data.

### P1.3 Select command-recording policy by measured cost

Compare four controlled modes:

1. current automatic parallel recording;
2. one record worker;
3. direct primary recording;
4. cost-threshold parallel recording.

For each mode measure:

- task queue delay;
- active record time per task;
- join/critical path;
- primary assembly/replay;
- command-pool reset;
- submit cost;
- GPU submission gap;
- CPU total `p50`/`p95`/`p99`.

Candidate policy:

- direct recording for small passes;
- secondary recording only when estimated record cost or item count exceeds a measured
  threshold;
- prepare/reset pools only for queue roles and workers used by the compiled plan;
- merge submit batches only if the GPU timeline proves no lost overlap or WSI regression.

Acceptance criterion:

- the chosen policy reduces CPU frame time without delaying GPU submission or increasing GPU
  critical-path time.

### P2.1 Reuse managed transient resources

Reuse frame-slot-compatible DLSS output images and other transient resources by compatible
create-info key instead of recreating VMA-backed images/views on slot reuse.

Current expected benefit is limited because measured resolution is only about `0.041 ms`.

### P2.2 Retain pipeline cache and improve only transition spikes

Preserve the current steady compile cache. If configuration changes still spike:

- persist Vulkan pipeline cache data;
- warm known variants;
- create new variants asynchronously outside the critical frame.

### Not recommended as initial work

- optimizing GPU TLAS build before CPU metadata;
- redesigning steady pipeline compilation;
- adopting descriptor buffer solely because it is newer;
- adopting device-generated commands before eliminating repeated host-side declaration and
  diagnostic work;
- blindly increasing record-worker count;
- merging submissions without a queue timeline.

## 9. Validation Matrix for Future Changes

| Change | Primary metric | Guardrail |
|---|---|---|
| Lazy diagnostics | Build/Prepare `p50`, first-use spike | Failure diagnostics remain complete |
| Binding-plan cache | Build/Prepare and format-call counts | Reflection/layout revision invalidates |
| RT metadata revisions | AS node and metadata-plan time | Streaming/material/transform correctness |
| Descriptor early hit | Map copies and descriptor updates | Three frame slots remain coherent |
| Graph skeleton cache | Build node-loop time | Dynamic resources and sync remain correct |
| Scene trimming | Scene and bridge time | Raster path remains complete |
| Executor policy | CPU total and GPU submission gap | No GPU critical-path regression |
| Transient reuse | Runtime-resource resolution | Lifetime and retained-state correctness |

Each experiment should:

1. use the same scene, camera, UI visibility, resolution and preset;
2. include warm-up before collection;
3. collect at least several thousand per-frame samples;
4. report distributions rather than only averages;
5. preserve a baseline executable for same-session A/B switching;
6. include one forced invalidation test relevant to the cache being introduced.

## 10. Confidence and Open Questions

### High confidence

- the Ultra Performance capture is CPU-bound;
- Build is the largest named CPU stage;
- TLAS texture/material collection sits outside the current named Scene/Build timers;
- graph and RT metadata are rebuilt each frame;
- eager diagnostic arguments execute on successful assertions;
- descriptor request construction occurs before the strongest cache-hit opportunity;
- compile and GPU TLAS build are not first-order bottlenecks.

### Medium confidence

- lazy diagnostics and cached binding plans will materially reduce Build/Prepare time;
- revision-cached RT metadata is the largest architectural CPU opportunity;
- stable graph declaration reuse will remove a substantial portion of Build.

The direction is strongly supported by source structure and sub-stage measurements, but
exact savings require isolated A/B changes.

### Not yet proven

- the exact GPU idle interval and its responsible CPU dependency;
- whether 7 record tasks are slower than direct recording on this CPU;
- whether any of the 3 submit batches can be safely merged;
- the capture's complete `0.805 ms` unclassified composition;
- true per-frame `p95`/`p99` and first-use spike distributions.

## 11. Final Prioritization

| Priority | Work | Why |
|---|---|---|
| P0 | Per-frame observability and Nsight timeline | Establishes causal CPU-to-GPU gaps |
| P0 | Lazy diagnostics and binding-plan reuse | Directly removes guaranteed hot-path work |
| P0 | Revision-based RT metadata/SBT cache | Targets the largest RT-specific repeated work |
| P0 | Early descriptor cache hit | Avoids map/request construction on stable frames |
| P1 | Stable graph skeleton reuse | Targets the largest measured Build boundary |
| P1 | Scene extraction/bridge trimming | Removes unconsumed RT-frame preparation |
| P1 | Executor A/B and adaptive threshold | Resolves whether parallel recording pays off |
| P2 | Managed transient reuse | Valid but currently small measured cost |
| P2 | Pipeline warm-up/persistence | Only for transition spikes |

The first implementation should not combine all P0 items. Each change should be introduced
as an independently measurable experiment so that the saved time, invalidation contract and
correctness risk remain attributable.
