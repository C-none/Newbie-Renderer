# Render Graph Cross-Frame Buffer Barrier Defect

Status: root cause confirmed by measurement, not fixed. This document records the investigation so
the defect is not rediscovered and so two earlier incorrect explanations are not repeated.

## Symptom

Synchronization validation reports one `SYNC-HAZARD-WRITE-AFTER-WRITE` per frame against a buffer
imported into the render graph with a retained state. It was first observed while running
`neuralMaterialViewer`, which enables `SyncValidation` and disables the duplicate message limit:

```text
hazard_type    = WRITE_AFTER_WRITE
prior_access   = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT(VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT)
write_barriers = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT(ALL_ACCESSES)
command        = vkCmdDispatch
prior_command  = vkCmdDispatch
debug_region       = ...::NeuralAppearance.OptimizeTraining.Pair7
prior_debug_region = ...::NeuralAppearance.OptimizeTraining.Pair7
```

The report is a `SubmitTimeError`, and the two dispatches are in different command buffers, so the
hazard is between consecutive frames rather than inside one frame.

The defect is not specific to the neural appearance node. It applies to any retained buffer whose
access pattern matches the shape described under "Affected pattern".

## How the cross-frame edge is built

For the first use of a retained resource in a frame the compiler emits a pre-barrier whose source
scope is the retained state carried over from the previous frame:

- `nrRenderGraphCompiler.cpp` stores the retained scope as the scope of the resource's **last use**
  in the frame: `resource.finalAccessScope = pair.second.scope;`
- `nrRenderGraphCompiler.cpp` builds the frame-opening transition as
  `srcScope = conservativeSourceScope(resource.initialAccessScope)` and `dstScope = currentScope`,
  where `currentScope` is the scope of that **first use**
- `nrRenderGraphExecutor.cpp` writes the retained state back after submission with
  `state.access = resource.finalAccessScope;`

So the cross-frame dependency is expressed by exactly two single-point scopes:

```text
srcAccessMask = access of the previous frame's last use
dstAccessMask = access of this frame's first use
```

## Root cause

A write that is not located at either of those two endpoints is never made available across the
frame boundary. Both observed instances are the two mirror images of the same defect.

Measurements were taken by temporarily logging `resource.finalAccessScope` where the retained state
is written back, and logging the resolved `srcStageMask`/`srcAccessMask`/`dstStageMask`/
`dstAccessMask` inside `RenderGraphExecutor::addTransitionBarrier`. The instrumentation was removed
afterwards.

### Instance A: the write is lost on the source side

`NeuralAppearance.RowMajorWeightGradients` is written by the cooperative-vector conversion and then
read by the optimizer, so its last use in a frame is a read:

```text
frame end : finalAccess = { ShaderStorageRead }
frame open: src = { ShaderStorageRead }
            dst = ConvertCooperativeVectorMatrixNV / { TransferWrite }
```

The previous frame's `TransferWrite` from the conversion pass is absent from `srcAccessMask`, so it
is never made available to the next frame's first write.

### Instance B: the write is lost on the destination side

`NeuralAppearance.TrainingControl` ends a frame with a read-write access, so its source side is
correct, but the frame's first use is the initialization pass, which only reads it:

```text
frame end : finalAccess = { ShaderStorageRead | ShaderStorageWrite }
frame open: src = { ShaderStorageRead | ShaderStorageWrite }
            dst = { ShaderStorageRead }
```

The previous frame's write is made available to reads only. The optimizer passes later in the same
frame write the buffer, and no barrier covers the prior-frame write for a write access.

Validation confirms this reading: the `write_barriers` field of the hazard lists only an unrelated
`ALL_TRANSFER(ALL_ACCESSES)` barrier from the checkpoint readback path. The frame-opening barrier is
absent from that list precisely because its `dstAccessMask` does not cover the conflicting write.

## Affected pattern

A retained buffer hits this defect when either holds:

- its **last** use in a frame is not a write, while an earlier pass in that frame wrote it; or
- its **first** use in the next frame is not a write, while a later pass in that frame writes it.

Read-only retained buffers and buffers whose first and last uses are both writes are unaffected.

## Two explanations that were checked and are wrong

Both were proposed during this investigation before the measurements existed. They are recorded so
they are not repeated.

1. *"`addTransitionBarrier` skips the buffer barrier whenever the source and destination queue
   families match."* The early return is guarded by `isOwnershipPlacement`, so it only applies to
   `Release`/`Acquire` placements, where skipping a same-family ownership transfer is correct.
   `TransitionPlacement::InPass` transitions always emit their barrier.
2. *"The cross-frame dependency is provided by a submission timeline semaphore, so no barrier is
   needed."* The code contains that path, but it is inert here. Breakpoints on
   `nrRenderGraphExecutorResources.cpp` at the `if (isInitialTransition)` branch and at the
   `transition.sourceSubmissionTimelineValue = ...` assignment never fire across a full multi-frame
   run, while a breakpoint on the enclosing function entry does. `lastSubmissionTimelineValue`
   therefore stays zero, the early return above the assignment is always taken, and
   `initialResourceWaitsByBatch` is never populated. As a side effect the same early return skips
   the `DependencyStrength::InOrder` downgrade, so the frame-opening transition keeps
   `BarrierRequired` and a real barrier is emitted. The cross-frame dependency is carried by that
   barrier, not by a semaphore.

## Investigation method

The evidence was produced with two complementary techniques, both against
`neuralMaterialViewer --train-and-save`:

- **lldb breakpoints** to establish which code paths execute. Reducing the node's training step
  budget first is necessary, because a full run under the debugger exceeds a practical timeout.
  Breakpoint hit counts, rather than variable inspection, carried most of the signal; the bundled
  formatter could not read `std::vector` contents in this toolchain.
- **Temporary logging** injected at the retained-state write-back and inside `addTransitionBarrier`,
  filtered by `resource.debugName`, to capture the exact access masks per frame.

## Impact

The missing availability is real, not a validation false positive: the emitted barrier genuinely
omits the conflicting write from its access scope.

Both observed instances are benign in their current use. `RowMajorWeightGradients` is fully
overwritten by the conversion pass before it is read, and `TrainingControl` carries an idempotent
control record. Neural appearance training is numerically stable and reproducible for a fixed seed.
The defect nevertheless holds for every retained buffer matching the pattern above, so a future node
with a genuine cross-frame data dependency could be corrupted.

The hazard also floods synchronization validation with one message per frame, which masks unrelated
reports. Removing a redundant clear of `RowMajorWeightGradients` eliminated the first instance and
immediately revealed the second, because validation reports only the first hazard per submit.

## Fix direction

The retained cross-frame state has to carry the accesses that still require availability rather than
the scope of a single use:

- retain the union of writes performed since the last availability operation, instead of
  `finalAccessScope` alone; and
- widen the frame-opening transition's `dstAccessMask` to cover the resource's accesses in the new
  frame, instead of only its first use.

This changes `nr.renderer` barrier emission for every node and is a stable-boundary change under the
root `AGENTS.md` rules, so it requires the accompanying architecture update and a full Debug
acceptance run.

## References

- [../src/renderer/nrRenderGraphCompiler.cpp](../src/renderer/nrRenderGraphCompiler.cpp)
- [../src/renderer/nrRenderGraphExecutor.cpp](../src/renderer/nrRenderGraphExecutor.cpp)
- [../src/renderer/nrRenderGraphExecutorResources.cpp](../src/renderer/nrRenderGraphExecutorResources.cpp)
- [../src/renderer/nrRenderGraphType.ixx](../src/renderer/nrRenderGraphType.ixx)
