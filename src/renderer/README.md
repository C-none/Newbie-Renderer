# Renderer Module Terminology

This module uses the following fixed terms:

- Node: a feature-level graph producer that can emit one or more passes.
- pass: an executable graph unit with queue domain, resource uses, and an optional record callback.
- prepare: main-thread pass stage for runtime resource resolution, CPU buffer writes, descriptor updates, upload staging setup, and other per-frame mutable preparation.
- record: worker-capable pass stage for command-buffer recording only.
- submitNode: a debug-named execution-control marker that splits submit batches.

Scope boundary:

- renderer owns frame-graph build/compile/execute orchestration, renderer-persistent resources, graph-transient planning, queue submit structure, and present orchestration.
- renderer does not own scene asset lifecycle or input handling.
- present path is compute-queue oriented.
- RDG execution is three-stage: build/compile/prepare on the main thread, pass command recording on a fixed `std::jthread` worker pool into secondary command buffers, then compiled-order secondary merge, submit, and present on the main thread.
- The main thread is an aggregation and submit owner during execute. Worker pass recording must use worker-only secondary pool slots; slot 0 is reserved away from RDG pass record tasks.
- Submit batch GPU debug labels include the submitNode debug name when the batch was opened by an explicit submit marker.
- When `VK_EXT_frame_boundary` is enabled by a capture tool, RDG submit batches for one frame share a monotonic frame-boundary ID and the final compute-present submit is marked as the frame end.
- Env-driven Nsight Graphics capture/trace control is owned by the RHI `NsightGraphicsFrameHelper` and invoked from `nr.rhi::Device`; renderer frame orchestration keeps the same begin/build/execute/present flow.
- `PipelineRuntime`, renderer-owned `FrameUniformArena`, `FrameGlobalResources`, `RasterPassBuilder`, and `ComputePassBuilder` are renderer-side helper abstractions for common node boilerplate: persistent pipeline descriptor sets, runtime-sized descriptor set allocation, renderer-owned global CPU-to-GPU frame uniform upload, descriptor snapshot updates, prepared descriptor binding, 128-byte-limited push constants, and graphics/compute pass prepare/record glue.
- RDG resources model buffers, images, swapchain images, and imported acceleration structures. Pass contexts resolve buffers, images, and acceleration structures separately so shader-visible AS descriptors bind TLAS/BLAS handles instead of their backing storage buffers.
- RDG frame data models CPU-side per-frame payloads that pass callbacks may need after graph build. `GraphFrameDataHandle` is graph-owned and type-checked through `PassPrepareContext::frameData<T>` / `PassRecordContext::frameData<T>`; renderer uses this path to expose the scene `SceneBridgeFrame` to all nodes without per-node value captures.
