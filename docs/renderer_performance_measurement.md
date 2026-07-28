# Renderer performance measurement

The viewer has an opt-in, fixed-schema benchmark export for the `rtobject` pipeline. It is disabled by default and does not change the interactive graph topology.

```powershell
main --pipeline rtobject --benchmark --warmup-frames 30 --measure-frames 120 --output build/benchmark --dlss-quality ultra-performance --render-graph-skeleton enabled
```

`--output` and a non-zero `--measure-frames` are required with `--benchmark`. The renderer accepts only successfully rendered frames: warmup is not written, measurement records exactly the requested count, and drain renders the existing frames-in-flight before the viewer exits cleanly. `--dlss-quality ultra-performance` is installed before the RT graph is built; without it the benchmark uses DLAA.

Performance samples are accepted only from an LLVM Release executable with validation
disabled. Debug runs remain correctness checks and diagnostics; their elapsed times,
including GPU-validation or shader-JIT time, are not performance evidence and must
not be mixed into Release distributions. An enabled `--benchmark` launch fails before
application/device initialization in a non-Release build, so a Debug application run
cannot emit a valid benchmark capture.

`--render-graph-skeleton legacy|enabled` is benchmark-only. If omitted, the benchmark
uses `enabled`. `legacy` is the A/B baseline; `Differential` is deliberately
unavailable because it compares cold materializations and is not a patch-versus-cold
timing mode. Run the two modes in separate output directories with the same
executable, model, extents, DLSS preset, warmup/measurement counts, and stable system
conditions.

The output directory contains `metadata.json`, `frames.csv`, `gpu_passes.csv`, and `summary.json`. Current captures use schema `nr-renderer-benchmark-v3`, an internal fixed schema rather than a general serialization format. CSV text follows RFC4180 quoting for commas, quotes, and newlines; JSON artifacts are built as typed values and serialized through the Boost.JSON-backed `dependency.json` boundary.

`frames.csv` records the monotonic renderer frame ordinal, recycled frame slot, stable configuration revision, display/render extents, DLSS preset, CPU top-level timing, selected scene/build diagnostics, packet/batch counts, `node_build_<index>_ms`, and AS-node telemetry. Schema v3 also records `skeleton_patch_ms`, `skeleton_rebuild_ms`, `skeleton_hit`, and `skeleton_miss_reason`. `metadata.json` maps each stable node column to its name and records `render_graph_skeleton_mode`. AS telemetry includes recorded/available state, cache scan, metadata planning, CPU writes, TLAS sizing, graph declaration, packet, instance, and dirty-BLAS counts. `Frame Setup` excludes `Wait GPU`; top-level CPU stages are wall-clock measurements and diagnostic sub-stages, including every node, AS, and Skeleton measurement, must not be added to them or to each other.

The retained v3 `frames.csv` evidence was mechanically normalized by removing one delimiter-only empty field between `execute_queue_submits` and `node_build_0_ms`; observed values and their order were unchanged.

`gpu_passes.csv` contains delayed Vulkan timestamp readback rows joined by the monotonic renderer frame ordinal, plus pass index/name, queue, copy flag, and duration. A pass duration is not a GPU frame critical path, and values from different queues must not be summed. Missing delayed readback rows remain absent and are called out by `summary.json` rather than treated as zero.

GPU rows are self-describing (`pass_index`, `pass_name`, queue, batch, copy flag) and are audited against the first captured frame's pass schema. Every captured ordinal requires exactly one complete status and exactly the expected available rows; missing, partial, duplicate, extra, or schema-drifting rows invalidate the run.

The summary validates strictly increasing frame ordinals and finite non-negative durations, checks accepted-frame count, and reports CPU distribution count, mean, p50, p95, p99, and max. Percentiles use Hyndman-Fan type 7; empty samples are represented as unavailable and singleton samples equal their sole sample.

Every summary distribution uses `count,min,p50,p95,p99,max,mean,stddev`; `stddev` is population standard deviation. Summary sections include `cpu_stages`, `cpu_substages`, `render_graph_skeleton`, `execute_substages`, `execute_counts`, `as_timings`, `as_counts`, `node_build`, and per-pass-only `gpu_passes`; GPU entries are never summed. `post_scene_ms` remains a mutually exclusive top-level `cpu_stages` interval between Scene and Build. `cpu_substages` covers the nested scene upload/extraction/bridge diagnostics plus TLAS texture collection, graph prelude, UI collection, node-loop, Skeleton patch, and Skeleton rebuild timing columns from `frames.csv`. Patch and rebuild are children of `build_ms`, never additional top-level totals.

`render_graph_skeleton` audits the selected mode, measurement-frame hits, Enabled
misses, explicitly disabled Legacy frames, patch failures, structural mismatches,
run-scoped cache counter deltas, final entry count, and every measurement-frame
miss-reason count. `run_cache_hit_delta`, `run_cache_miss_delta`,
`run_invalidation_delta`, and `run_structure_mismatch_delta` cover the complete
warmup + measurement + drain lifecycle; `frame_hits`, `enabled_misses`,
`disabled_frames`, and `miss_reasons` cover only the exported measurement frames.
In a Legacy run, `disabled_frames` and the `disabled` miss-reason count describe an
intentional rollback baseline; they are not failed Enabled hits.

The v3 frame audit is mode-aware. Legacy requires `skeleton_hit=false`,
`skeleton_miss_reason=disabled`, and zero patch/rebuild timing; its cold baseline
remains `node_loop_ms`. An Enabled hit requires reason `none` and zero rebuild timing.
An Enabled miss requires a known non-`none`, non-`disabled` reason and zero successful
patch timing; its cold fallback may report non-negative rebuild timing. Unknown enum
values, Differential mode, and contradictory hit/reason/timing combinations
invalidate `frames_valid` and therefore the complete run.

`execute_substages` is benchmark-only main-thread wall time inside `RenderGraphExecutor::executePrepared`. Its mutually exclusive sequence is executor setup, completed GPU timestamp readback, timestamp setup, per-frame lookup/validation, swapchain acquisition, deferred prepare, record-task launch, primary recording before result collection, record-completion wait, primary replay/barriers/timestamps, primary end plus submit-batch construction, queue submit, initial-release record/submit, synthetic-present record/submit, and finalization. `deferred_prepare_ms` functionally belongs to prepare work but executes only after the swapchain image is acquired, so it is reported under Execute. `record_completion_wait_ms` is main-thread waiting for worker results, not a sum of worker CPU recording time. The substage values are additive only within this executor main-thread accounting and must not be added to worker recording durations or to unrelated top-level CPU stages.

`execute_accounted_main_thread_ms` is the sum of those mutually exclusive executor wall-time fields. `execute_unclassified_ms` is `execute_ms - execute_accounted_main_thread_ms`; residuals down to -0.001 ms are clamped to zero for clock noise, while a more negative residual invalidates the run. `execute_counts` reports compiled submit batches, swapchain-acquire batches, launched record tasks, replayed secondary command buffers, and actual queue submits. Counts are structural context, not timing terms or GPU critical-path values.

`cpu_work_ms` is `total_ms - wait_gpu_ms`. `classified_ms` is the sum of the mutually classified top-level CPU buckets (`wait`, setup, scene, post-scene, build, compile, prepare, execute, present), and `unclassified_ms` is `total_ms - classified_ms`; small floating-point noise is tolerated but material negative values invalidate the run. The quality audit rejects missing, partial, duplicate, extra, invalid, or schema-drifting GPU data and invalid node/AS telemetry. An invalid audit still writes artifacts with `run_status: invalid`, then returns a non-zero viewer exit status. After artifact writing completes, repeated finalization returns that same success or invalid result. GPU statistics are per pass only and must never be summed across queues into a frame time.

Historical v1/v2 captures under `docs/reports/.../runs/*`, together with their generated `report.html` and `artifact.json`, remain immutable. In v1 artifacts `post_scene_ms` was stored in `cpu_substages` and was therefore included in `unclassified_ms`; compare v1 and later schemas only after accounting for that classification change. v2 does not contain the v3 Skeleton mode or per-frame Skeleton telemetry and must not be silently interpreted as v3.

For a reproducible run, use a fixed model, output resolution, DLSS preset, and warmup/measurement counts; keep the visible UI state unchanged. Metadata records build configuration and validation status. Promote performance data only when `build_config` is `Release`, `validation_enabled` is `false`, both A/B modes report valid audits and the requested accepted-frame count, and the executable and test conditions match.

## Nsight Graphics 2026.3 CLI launch and trace-completeness rules

Nsight Graphics 2026.3 has an argument-tokenization trap on Windows. Do not pass
`--platform` and `Windows (x86_64)` as two argv tokens. The embedded Qt
`QGuiApplication` consumes the split `--platform` as its QPA `-platform` option and
then tries to load a plugin named `windows (x86_64)`. The installed QPA plugin key is
`windows`, so this fails before the Nsight CLI can use its own platform value. Qt
documents both its removal of recognized command-line arguments and the QPA
`-platform` option in the
[QGuiApplication documentation](https://doc.qt.io/qt-6/qguiapplication.html).

Pass the Nsight value as one argv token, `'--platform=Windows (x86_64)'`, or omit
`--platform` for a local target. Use single-token `--name=value` forms for other
space-containing values as well. This PowerShell example matches the local 2026.3
installation and the current measurement executable:

```powershell
$ngfx = 'C:\Program Files\NVIDIA Corporation\Nsight Graphics 2026.3.0\host\windows-desktop-nomad-x64\ngfx.exe'
$app = 'D:\file\prog\Newbie-Renderer\build\codex-measurement-llvm-2\src\Release\main.exe'
$workdir = 'D:\file\prog\Newbie-Renderer\build\codex-measurement-llvm-2\src\Release'
$output = 'D:\file\prog\Newbie-Renderer\build\codex-nsight-retry\<timestamp>'
$appOutput = Join-Path $output 'benchmark'
$nsightOutput = Join-Path $output 'nsight-output'

$ngfxArgs = @(
    '--verbose'
    '--activity=GPU Trace Profiler'
    '--platform=Windows (x86_64)'
    "--output-dir=$nsightOutput"
    "--exe=$app"
    "--dir=$workdir"
    "--args=--pipeline rtobject --benchmark --dlss-quality ultra-performance --warmup-frames 600 --measure-frames 1200 --output `"$appOutput`""
    '--env=NR_NSIGHT_GRAPHICS_ACTIVITY=trace; NR_NSIGHT_GRAPHICS_FRAME=900; NR_NSIGHT_GRAPHICS_FRAMES=3; NR_NSIGHT_GRAPHICS_INSTALL_DIR=C:\Program Files\NVIDIA Corporation\Nsight Graphics 2026.3.0;'
    '--start-with-ngfx-sdk'
    '--stop-with-ngfx-sdk'
    '--max-duration-ms=1000'
    '--auto-export'
    '--trace-timeout=240'
    '--set-gpu-clocks=base'
    '--collect-screenshot=1'
)

& $ngfx @ngfxArgs
```

The supported activity options and SDK start/stop behavior are documented in
[NVIDIA's GPU Trace overview and CLI section](https://docs.nvidia.com/nsight-graphics/UserGuide/gpu-trace-overview.html),
the executable/working-directory/argument fields are described in
[App Configuration and Activity Selection](https://docs.nvidia.com/nsight-graphics/UserGuide/ui-launch-application.html),
and programmatic triggering is described in the
[Nsight Graphics SDK guide](https://docs.nvidia.com/nsight-graphics/UserGuide/sdk.html).

`Session established. Starting activity...` and detection of Vulkan `Present` calls
prove only that Nsight connected and observed the target. They do not prove that a
GPU Trace completed. Timeline claims require a clean activity completion and a
non-empty trace artifact that can be opened/exported; NVIDIA describes completed
GPU Trace data as a saved trace file in the
[GPU Trace UI reference](https://docs.nvidia.com/nsight-graphics/UserGuide/gpu-trace-ui.html).
If either condition is absent, record the attempt as “no timeline captured” and use
only the benchmark telemetry.

On 2026-07-25 the local driver was `610.62`. Nsight Graphics 2026.3 lists Windows
Release 615 or newer as required in its
[release notes](https://docs.nvidia.com/nsight-graphics/ReleaseNotes/index.html).
This is a support caveat and a reason to repeat the trace on a supported driver; it
is not proof that the driver caused any particular failed capture.

## Smoke-test process lifetime and bounded diagnostics

On 2026-07-27, a diagnostic launch used a hidden nested `pwsh` created by
`Start-Process`. The outer script called `Wait-Process -Timeout 25`, swallowed the
timeout exception, and did not terminate or otherwise reclaim the launched process
tree. The surviving `nr_rt_object_material_smoke_test.exe` process, PID `48340`, was
observed after approximately 9.5 hours with approximately 34,362 seconds of CPU time,
a 1.20 GB working set, and 2.03 GB of private memory, and was then terminated
manually. These values describe one incident; they are not performance or memory
budgets.

The confirmed cause of that orphaned lifetime is the external diagnostic harness, not
the renderer. No thread stack or dump was captured for that incident, so it does not
identify the renderer, Validation Layers, driver, NGX, or PowerShell pipe as the cause
of the test's internal non-termination. CPU time close to elapsed time supports an
active busy loop, but does not locate it. A single memory snapshot does not
demonstrate a leak or its owner. The later bounded lifecycle investigation below is
separate evidence about NGX shutdown behavior under filesystem sandboxing.

The current RT smoke is a windowed LLVM Debug CTest that exercises the GPU, ray
tracing, and repository assets. The implemented containment gives this test a
90-second CTest `TIMEOUT`, bounds each frame-image acquisition to five seconds, and
allows at most three resize retries (four total frame attempts). The default Debug
validation profile still enables Core, Synchronization, GPU-Assisted Validation, and
DebugPrintf. These validation features remain risk surfaces and possible timing
perturbations, not established causes. A validation-on Debug diagnostic run must not
be compared as though it were an equivalent sample to a validation-off performance
run.

The finite CTest timeout, finite acquire, and bounded resize retry are implemented
containment. The following additional controls remain future work:

- Do not use a detached hidden helper unless one owner guarantees cleanup of the
  complete process tree.
- If asynchronous launch is necessary, use a Windows Job Object or equivalent
  process-tree owner; a `finally` block that stops the known process is only a minimum
  safeguard, not complete descendant ownership.
- On timeout, capture the required stack or dump within a bounded collection window,
  then terminate the complete process tree.
- Record the exact command, timeout, validation profile, output topology, exit or
  timeout result, and retained diagnostic artifacts.
- Keep ordinary Debug smoke verification separate from dedicated, finite
  GPU-AV/DebugPrintf diagnostics.

### Plan A: unsandboxed NGX lifecycle and benchmark execution

NGX/RT hardware acceptance and benchmark launches use synchronous foreground Release
processes outside the filesystem sandbox, each with a finite hard timeout and
process-tree cleanup if that timeout expires. They must not use detached
`Start-Process`/`Wait-Process` pipelines, and a run is accepted only after its process
exits and no residual renderer, smoke-test, or NGX update process remains. CTests that
exercise the retained RT smoke carry the `requires-unsandboxed` label.
Sandboxed results remain diagnostic only; their timing is never Release performance
evidence.

This rule follows a bounded native and layered lifecycle investigation. In the
filesystem sandbox, the direct MSVC Release `Init` -> `Shutdown1` probe reached
`shutdown-begin` and exceeded its timeout after capability-parameter destruction had
completed. Wait Chain Traversal showed the main thread waiting on a same-process NGX
worker, with no child process present. Reusing or uniquifying the data path, disabling
discovery, supplying an explicit feature path, changing handle inheritance and
logging sinks, using global shutdown, and adding delay did not remove that wait. The
identical foreground command outside the sandbox exited naturally in about five
seconds and reported `shutdown-complete result=0x1`.

The native and layered lifecycle probes used for that investigation were one-time
diagnostics and are not retained as repository regression targets. The maintained
repository regression surface is the bounded RT material smoke described above.

A timeout, orphaned process, or run missing its required stack, trace, or dump is not
promotion or performance evidence. CPU and memory snapshots alone are likewise
insufficient to attribute a leak. Relevant lifecycle and diagnostic behavior is
documented by Microsoft for
[`Start-Process`](https://learn.microsoft.com/en-us/powershell/module/microsoft.powershell.management/start-process?view=powershell-7.5),
[`Wait-Process`](https://learn.microsoft.com/en-us/powershell/module/microsoft.powershell.management/wait-process?view=powershell-7.5),
and [Windows Job Objects](https://learn.microsoft.com/en-us/windows/win32/procthread/job-objects);
CMake defines process termination through the test
[`TIMEOUT` property](https://cmake.org/cmake/help/latest/prop_test/TIMEOUT.html).
The instrumentation and default-state context for GPU validation is described in the
[Vulkan GPU validation guidance](https://docs.vulkan.org/tutorial/latest/Advanced_Vulkan_Compute/11_Diagnostics_and_Refinement/02_compute_validation.html)
and [Vulkan initialization specification](https://docs.vulkan.org/spec/latest/chapters/initialization.html).
