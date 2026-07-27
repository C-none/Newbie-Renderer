# Renderer performance measurement

The viewer has an opt-in, fixed-schema benchmark export for the `rtobject` pipeline. It is disabled by default and does not change the interactive graph topology.

```
main --pipeline rtobject --benchmark --warmup-frames 30 --measure-frames 120 --output build/benchmark --dlss-quality ultra-performance
```

`--output` and a non-zero `--measure-frames` are required with `--benchmark`. The renderer accepts only successfully rendered frames: warmup is not written, measurement records exactly the requested count, and drain renders the existing frames-in-flight before the viewer exits cleanly. `--dlss-quality ultra-performance` is installed before the RT graph is built; without it the benchmark uses DLAA.

The output directory contains `metadata.json`, `frames.csv`, `gpu_passes.csv`, and `summary.json`. Current captures use schema `nr-renderer-benchmark-v2`, an internal fixed schema rather than a general serialization format. CSV text follows RFC4180 quoting for commas, quotes, and newlines; JSON text is escaped.

`frames.csv` records the monotonic renderer frame ordinal, recycled frame slot, stable configuration revision, display/render extents, DLSS preset, CPU top-level timing, selected scene/build diagnostics, packet/batch counts, `node_build_<index>_ms`, and AS-node telemetry. `metadata.json` maps each stable node column to its name. AS telemetry includes recorded/available state, cache scan, metadata planning, CPU writes, TLAS sizing, graph declaration, packet, instance, and dirty-BLAS counts. `Frame Setup` excludes `Wait GPU`; top-level CPU stages are wall-clock measurements and diagnostic sub-stages, including every node and AS measurement, must not be added to them or to each other.

`gpu_passes.csv` contains delayed Vulkan timestamp readback rows joined by the monotonic renderer frame ordinal, plus pass index/name, queue, copy flag, and duration. A pass duration is not a GPU frame critical path, and values from different queues must not be summed. Missing delayed readback rows remain absent and are called out by `summary.json` rather than treated as zero.

GPU rows are self-describing (`pass_index`, `pass_name`, queue, batch, copy flag) and are audited against the first captured frame's pass schema. Every captured ordinal requires exactly one complete status and exactly the expected available rows; missing, partial, duplicate, extra, or schema-drifting rows invalidate the run.

The summary validates strictly increasing frame ordinals and finite non-negative durations, checks accepted-frame count, and reports CPU distribution count, mean, p50, p95, p99, and max. Percentiles use Hyndman-Fan type 7; empty samples are represented as unavailable and singleton samples equal their sole sample.

Every summary distribution uses `count,min,p50,p95,p99,max,mean,stddev`; `stddev` is population standard deviation. Summary sections include `cpu_stages`, `cpu_substages`, `execute_substages`, `execute_counts`, `as_timings`, `as_counts`, `node_build`, and per-pass-only `gpu_passes`; GPU entries are never summed. In v2, `post_scene_ms` is a mutually exclusive top-level `cpu_stages` interval between Scene and Build. `cpu_substages` covers the nested scene upload/extraction/bridge diagnostics plus TLAS texture collection, graph prelude, UI collection, and node-loop timing columns from `frames.csv`.

`execute_substages` is benchmark-only main-thread wall time inside `RenderGraphExecutor::executePrepared`. Its mutually exclusive sequence is executor setup, completed GPU timestamp readback, timestamp setup, per-frame lookup/validation, swapchain acquisition, deferred prepare, record-task launch, primary recording before result collection, record-completion wait, primary replay/barriers/timestamps, primary end plus submit-batch construction, queue submit, initial-release record/submit, synthetic-present record/submit, and finalization. `deferred_prepare_ms` functionally belongs to prepare work but executes only after the swapchain image is acquired, so it is reported under Execute. `record_completion_wait_ms` is main-thread waiting for worker results, not a sum of worker CPU recording time. The substage values are additive only within this executor main-thread accounting and must not be added to worker recording durations or to unrelated top-level CPU stages.

`execute_accounted_main_thread_ms` is the sum of those mutually exclusive executor wall-time fields. `execute_unclassified_ms` is `execute_ms - execute_accounted_main_thread_ms`; residuals down to -0.001 ms are clamped to zero for clock noise, while a more negative residual invalidates the run. `execute_counts` reports compiled submit batches, swapchain-acquire batches, launched record tasks, replayed secondary command buffers, and actual queue submits. Counts are structural context, not timing terms or GPU critical-path values.

`cpu_work_ms` is `total_ms - wait_gpu_ms`. `classified_ms` is the sum of the mutually classified top-level CPU buckets (`wait`, setup, scene, post-scene, build, compile, prepare, execute, present), and `unclassified_ms` is `total_ms - classified_ms`; small floating-point noise is tolerated but material negative values invalidate the run. The quality audit rejects missing, partial, duplicate, extra, invalid, or schema-drifting GPU data and invalid node/AS telemetry. An invalid audit still writes artifacts with `run_status: invalid`, then returns a non-zero viewer exit status. After artifact writing completes, repeated finalization returns that same success or invalid result. GPU statistics are per pass only and must never be summed across queues into a frame time.

Historical v1 captures under `docs/reports/.../runs/*`, together with their generated `report.html` and `artifact.json`, remain immutable. In those artifacts `post_scene_ms` was stored in `cpu_substages` and was therefore included in `unclassified_ms`; compare v1 and v2 only after accounting for that classification change.

For a reproducible run, use a fixed model, output resolution, DLSS preset, and warmup/measurement counts; keep the visible UI state unchanged. Metadata records build configuration and validation status so a Debug validation-on verification run is distinguishable from an intentional optimized validation-off sampling run.

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
