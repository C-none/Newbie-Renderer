# Renderer Module Terminology

This module uses the following fixed terms:

- Node: a feature-level graph producer that can emit one or more passes.
- pass: an executable graph unit with queue domain, resource uses, and an optional record callback.
- prepare: main-thread pass stage for runtime resource resolution, CPU buffer writes, descriptor updates, upload staging setup, and other per-frame mutable preparation.
- record: worker-capable pass stage for command-buffer recording only.
- submitNode: an explicit execution-control node that splits submit batches.

Scope boundary:

- renderer owns frame-graph build/compile/execute orchestration, renderer-persistent resources, graph-transient planning, queue submit structure, and present orchestration.
- renderer does not own scene asset lifecycle or input handling.
- present path is compute-queue oriented.
- RDG execution is three-stage: build/compile/prepare on the main thread, pass command recording on a fixed `std::jthread` worker pool into secondary command buffers, then compiled-order secondary merge, submit, and present on the main thread.
- When `VK_EXT_frame_boundary` is enabled by a capture tool, RDG submit batches for one frame share a monotonic frame-boundary ID and the final compute-present submit is marked as the frame end.
- Env-driven Nsight Graphics capture/trace control is owned by the RHI `NsightGraphicsFrameHelper` and invoked from `nr.rhi::Device`; renderer frame orchestration keeps the same begin/build/execute/present flow.
