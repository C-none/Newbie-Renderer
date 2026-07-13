# RT Compute AS Timeline Submit Plan

## Status

Implemented and root-reviewed on 2026-07-13. LLVM Debug build passed. Focused pipeline/compiler contracts and the new renderer timeline contract passed. Full CTest finished 27/28; the remaining failure is the pre-existing PathTracing RNG sample-frame-ordinal source contract conflicting with the user-modified shader and is outside this plan's no-shader scope.

## Goal

Make the `rtobject` renderer graph execute acceleration-structure build work on the compute queue, insert an explicit submit boundary before graphics ray tracing, and retain the later graphics-to-compute boundary. All RDG batches in the frame must be chained by the renderer's single existing timeline semaphore using monotonically increasing values.

## Architecture decisions

- Keep `LightPrepare` and `PathTracing` on graphics. `LightPrepare::build` must publish its frame resources before `PathTracing::build` resolves them, so node order remains AS, LightPrepare, PathTracing, UI, Accumulate, Present.
- Set only `AccelerationStructureBuild` to `QueueDomain::Compute`.
- Insert `rtobject.ComputeToGraphics` after node index 0 and keep `rtobject.GraphicsToCompute` after node index 3.
- Reuse `RendererSubmissionTimeline`; do not create another semaphore or reset its counter per frame. The executor must chain the three batches as signal N, wait N/signal N+1, wait N+1.
- Change graphics submission waits to `vk::PipelineStageFlagBits2::eAllCommands`, because `eColorAttachmentOutput` does not cover a batch-head acquire and RT shader consumption after compute AS build.
- Rely on existing graph resource ownership compilation for TLAS release/acquire. Do not change AS node implementation or public interfaces.
- Preserve all pre-existing uncommitted changes and edit only local relevant hunks.

## Milestone 1: implement and validate

### Implementation

1. Update `buildRtObjectGraph` queue and submit-node specs.
2. Update `RenderGraphExecutor::submissionWaitStage` so graphics submit waits cover all commands.
3. Update pipeline contract tests for node queues and both named submit boundaries.
4. Change/add a renderer compiler contract fixture for compute AS build followed by explicit submit and graphics RT read. Assert two queues/batches, release-acquire strength, and AS-write to RT-read scopes.
5. Add a focused contract for the graphics submission wait stage if it can be exposed without widening production API; otherwise use the project's existing source-contract style to pin the behavior.
6. Update `docs/architecture/README.md` and the narrow render-passes topic README to describe the actual queue chain and single timeline semaphore.

### Acceptance criteria

- RT graph AS node is compute; PathTracing remains graphics; Accumulate/Present remain compute.
- Submit specs are ordered after node indices 0 and 3 and have stable compute-to-graphics / graphics-to-compute names.
- Compiler produces a compute AS batch and a graphics RT batch separated by an explicit boundary, with TLAS release/acquire synchronization.
- The executor waits on the same timeline semaphore signaled by the previous batch, with strictly increasing values and an all-commands graphics wait scope.
- No new semaphore, dispatch table, raw Vulkan command call, shader change, unrelated refactor, or overwritten user work.
- Architecture text matches the implemented flow.

### Validation

- Run `git diff --check` and focused source/contract inspections.
- Run the focused LLVM Debug unit target(s) if discoverable, then the required LLVM Debug build/test commands from `AGENTS.md`, unless the code is identified as Messiah C++; in that case do not build and report the skipped verification.
- No shader validation is required unless shader files are modified; if modified unexpectedly, stop and report scope drift rather than proceeding.

## Review checklist

- Inspect the complete diff against the dirty baseline.
- Verify queue transition boundaries match pass execution order.
- Verify semaphore wait stage covers RT and batch-head acquire work.
- Verify tests assert behavior rather than implementation-only naming.
- Use `/review` before final acceptance.
