# RenderPasses Module Terminology

This module uses the same fixed terms as renderer:

- Node: top-level feature unit.
- pass: executable unit emitted by a Node.
- submitNode: debug-named graph control marker (not a business Node).

Rules:

- A Node may emit multiple passes.
- EmbeddedTriangle Node targets graphics queue output, binds renderer global frame uniforms, and emits a single draw-call demo pass.
- NormalBuffer Node targets graphics queue output, outputs world-space interpolated vertex normals for any scene.
- Ui Node targets graphics queue UI composition, uploads GPU-only texture pixels through the RHI upload ring before graph import, and keeps overlay texture reads in the render graph resource-intent path.
- Present Node targets compute queue path and final present preparation.

## Binding Checklist

Shader-visible bindings in this module must be reflection-led through renderer-side pass builders. Nodes should use `PipelineRuntime`, `NodeBuildContext::globalResources`, `RasterPassBuilder`, and `ComputePassBuilder` for persistent pipeline descriptor sets, renderer-owned global frame resources, static descriptor writes, push constants, dynamic descriptor snapshots, and the split prepare/record deployment path.

- Allowed direct command recording inside builder record callbacks: vertex/index buffers, draw/dispatch, scissor overrides, per-draw dynamic state, barriers, and copy helpers.
- Direct `context.addPass(...)` remains appropriate for non-shader-visible transfer/copy passes that do not update or bind shader descriptors. CPU uploads into GPU-only resources should use the RHI upload ring instead of node-local staging passes.
- Per-node/pass/draw scalar payloads must use push constants only when they are no larger than `nr::rhi::kMaxPushConstantBytes` (128 bytes). Larger payloads must be split into renderer global frame uniforms or buffer/texture upload resources.
- Dynamic or bindless resources may provide a builder `dynamicBindingSnapshot(...)` callback using reflection cursors, but descriptor writes and command-buffer descriptor binding still stay inside the builder prepare/record path.
- Scene bridge data that is consumed during deferred prepare/record work should be captured as `GraphFrameDataHandle` values from `NodeFrameParameters` and resolved through the pass context. Do not capture borrowed `SceneBridgeFrame` references or copy per-node bridge snapshots into record lambdas.
- Not allowed in nodes: hand-written descriptor-set update/bind flows, direct `ShaderBindingPool::update(...)` or `resolveDescriptorWriteRequests(...)` usage, direct `CursorPipelineLayout::bindDescriptorSets(...)` for shader-visible bindings, direct `updateResourcesForBindingSnapshot(...)`, direct `bindPreparedResourcesToCommandBuffer(...)`, direct `pushConstantsToCommandBuffer(...)`, or `bindResourcesToCommandBuffer(...)`.

Current audit note: all built-in shader-visible passes use `RasterPassBuilder` or `ComputePassBuilder`. `Present.CopyToSwapchain` still uses direct `context.addPass(...)` because it is a copy graph pass without shader-visible bindings.
