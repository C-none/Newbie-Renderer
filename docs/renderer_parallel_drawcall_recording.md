# Parallel Drawcall Recording

This document describes the current RDG parallel record contract for draw-heavy passes.
The first production user is `NormalBuffer.Raster`, but the contract is implemented at
the generic `addPass` layer.

## Contract

A pass that opts into `PassParallelRecordDesc` is recorded as unordered chunks:

- `ParallelRecordReplaySemantics::Unordered` is the only supported replay semantic.
- The pass declares `itemCount`, an optional primary scope, and `recordRange(begin, end)`.
- The executor owns thread count, chunk count, and contiguous range assignment through
  `ParallelRecordPlanner::planContiguousRanges(itemCount, availableRecordWorkers)`.
- Node/renderpasses code can observe the final `PassParallelRecordPlan` in the range
  context, but cannot override assigned thread count, chunk count, or range layout.

If rendering logic requires draw-order semantics, it must not use one parallel pass for
that ordered work. Split the work into multiple ordered `addPass` calls, then declare the
resource uses at those pass boundaries. When the next pass must be ordered after the
previous use of the same resource even though queue/layout stay unchanged, mark the next
use with `use::orderedAfterPrevious(...)`; the compiler emits a `BarrierRequired`
transition for that edge.

## Planner

The executor passes `totalRecordWorkerCount - 1` as the available thread count for one
parallel pass, with a floor of one thread. `ParallelRecordPlanner::planContiguousRanges(...)`
then balances contiguous ranges across that count:

- `itemCount == 0`: no ranges, `assignedThreadCount == 0`.
- otherwise: `assignedThreadCount = min(availableRecordWorkers, itemCount)`.
- range `threadId` receives `itemCount / availableRecordWorkers` items plus one extra
  item when `threadId < (itemCount % availableRecordWorkers)`.
- ranges are contiguous, balanced, and cover `[0, itemCount)` without overlap.

The remainder condition uses `<` because thread ids are zero-based. Cost hints, max chunk
counts, or pass-local policies can be added later as executor policy, not as current node
authority.

## Executor Flow

During `executePrepared(...)`, the executor launches record tasks for all submit batches
before the ordered primary assembly loop.

For a serial pass, the existing path remains: one worker records one secondary and the
primary executes that one command buffer for the pass.

For a parallel pass:

1. The executor evaluates `itemCount` using a `PassRecordContext` without a command
   buffer.
2. It computes a `PassParallelRecordPlan`.
3. It evaluates the pass primary scope, currently either `NoPrimaryScope` or
   `DynamicRenderingSecondaryContents`.
4. It submits one worker task per planned range. Each task records one secondary command
   buffer and calls `recordRange(...)`.
5. The primary wraps the whole pass in one timestamp pair, records the pass in-pass
   barriers before replay, opens the primary scope when needed, executes all chunk
   secondaries, closes the scope, then writes the end timestamp.

Chunk secondaries do not record pass barriers. For dynamic rendering, the primary opens
the render instance with `eContentsSecondaryCommandBuffers`; each chunk secondary begins
with `eRenderPassContinue` and a `vk::CommandBufferInheritanceRenderingInfo` built from
the pass scope.

In Debug builds the primary may rotate chunk replay order for parallel passes. This is
intentional: a parallel pass must be correct under any chunk replay order.

## RasterPassBuilder

`RasterPassBuilder::record(...)` keeps the serial dynamic-rendering path. The new
`RasterPassBuilder::recordParallel(itemCount, rangeRecord)` path builds a generic
`PassParallelRecordDesc`:

- the primary scope resolves color/depth attachments and opens dynamic rendering on the
  primary;
- graphics pipeline formats, depth/stencil formats, and sample count come from the
  retained graphics pipeline desc in `PipelineRuntime::state()`;
- each range secondary replays full graphics setup: pipeline bind, prepared descriptor
  binds, static push constants, viewport/scissor, primitive topology, and raster state;
- the user range callback records only the draw range and its per-draw state.

`NormalBufferNode` now uses `recordParallel`. Its item count is
`SceneBridgeFrame::rasterDraws.size()`, and each range records `[begin, end)`. Per-chunk
dynamic state caches, such as cull mode and front face, are initialized locally so chunk
recording and replay do not depend on neighboring chunks.

## Vulkan Constraints

The design follows these Vulkan constraints:

- secondary command buffers cannot execute other secondary command buffers;
- dynamic rendering with secondary contents must be opened on the primary;
- secondaries executed inside dynamic rendering need inheritance rendering info and
  `eRenderPassContinue`;
- secondaries do not inherit pipeline, descriptor, push constant, viewport, scissor, or
  dynamic raster state.

These constraints are why parallel raster recording is executor-owned and why every
chunk secondary repeats the graphics setup.

## Verification

The contract is covered by renderer unit tests:

- planner coverage for `0`, `1`, `63`, `64`, `65`, and item counts larger than worker
  count;
- builder retention of parallel pass descriptors;
- compiler emission of `BarrierRequired` for `use::orderedAfterPrevious(...)` even when
  queue and layout are unchanged.

The `normalBufferUiSmoke` path validates the integrated `NormalBuffer + UI + Present`
frame path.
