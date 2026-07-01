# RenderPasses Module Terminology

This module uses the same fixed terms as renderer:

- Node: top-level feature unit.
- pass: executable unit emitted by a Node.
- submitNode: debug-named graph control marker (not a business Node).

Rules:

- A Node may emit multiple passes.
- EmbeddedTriangle Node targets graphics queue output, binds renderer global frame uniforms, and emits a single draw-call demo pass.
- AccelerationStructureBuild Node targets graphics or compute queue AS build work, owns renderer-side BLAS/TLAS cache resources, allocates BLAS storage from one node-owned atlas buffer, batches dirty rebuild-only BLAS builds, rebuilds TLAS from scene TLAS packets, and publishes `frameResource::sceneTlas`.
- RayTraceInstanceHash Node targets ray tracing work, consumes `frameResource::sceneTlas`, writes one hash color per pixel from closest-hit instance ID, publishes `frameResource::presentSourceColor`, and uses the shared scene texture table binding helper for deferred descriptor-set allocation even though the current shader does not sample material textures.
- Display Node targets the compute queue final display path, consumes `frameResource::presentSourceColor`, and copies a swapchain-ready image without requiring UI composition.
- NormalBuffer Node targets graphics queue output, outputs world-space normals for any scene, binds the scene geometry atlas once per parallel record chunk, samples normal maps through the renderer global `gSceneTextures[]` bindless table when a draw provides a non-zero normal texture ID, and uses `RasterPassBuilder::recordParallel(...)` to record independent contiguous ranges of scene raster draws.
- Ui Node targets graphics queue UI composition, uploads GPU-only texture pixels through the RHI upload ring before graph import, and keeps overlay texture reads in the render graph resource-intent path.
- Present Node targets compute queue path and final present preparation with optional UI composition.

## Binding Checklist

Shader-visible bindings in this module must be reflection-led through renderer-side pass builders. Nodes should use `PipelineRuntime`, `NodeBuildContext::globalResources`, `RasterPassBuilder`, `ComputePassBuilder`, and `RayTracingPassBuilder` for persistent pipeline descriptor sets, renderer-owned global frame resources, static descriptor writes, push constants, dynamic descriptor snapshots, acceleration-structure descriptors, and the split prepare/record deployment path.

- Allowed direct command recording inside builder record callbacks: vertex/index buffers, draw/dispatch/trace, scissor overrides, per-draw dynamic state, barriers, and copy helpers.
- Direct `context.addPass(...)` remains appropriate for non-shader-visible transfer/copy/AS-build passes that do not update or bind shader descriptors. CPU uploads into GPU-only resources should use the RHI upload ring instead of node-local staging passes.
- Per-node/pass/draw scalar payloads must use push constants only when they are no larger than `nr::rhi::kMaxPushConstantBytes` (128 bytes). Larger payloads must be split into renderer global frame uniforms or buffer/texture upload resources.
- Dynamic or bindless resources may provide a builder `dynamicBindingSnapshot(...)` callback using reflection cursors, but descriptor writes and command-buffer descriptor binding still stay inside the builder prepare/record path.
- Scene bridge data that is consumed during deferred prepare/record work should be captured as `GraphFrameDataHandle` values from `NodeFrameParameters` and resolved through the pass context. Do not capture borrowed `SceneBridgeFrame` references or copy per-node bridge snapshots into record lambdas.
- Scene/model raster passes that emit conventional clip-space Y-up positions should select `RasterViewportYMode::ClipSpaceYUp`; screen-space UI passes should keep the default framebuffer-top-left viewport mode.
- A pass that opts into `recordParallel(...)` must treat chunk replay order as semantically unordered. If a feature needs ordered draw groups, split it into multiple ordered passes and declare resource uses at those pass boundaries; use `nr::renderer::use::orderedAfterPrevious(...)` when the next pass needs an explicit barrier despite unchanged queue/layout.
- `NormalBuffer.Raster` consumes `SceneBridgeFrame::geometryBuffers` as the scene vertex/index atlas binding source. Each parallel chunk binds the atlas vertex buffer once, binds the atlas index buffer once when the frame has indexed geometry, and the per-draw loop changes only dynamic raster state, push constants, and atlas-adjusted draw parameters.
- Scene material texture IDs in raster push constants default to 0 and are packed as two 16-bit IDs per 32-bit lane. Shader code should use `shader/materialTextureIds.slang` for slot constants and unpacking. Renderer global resources bind `gSceneTextures[0]` to a 1x1 purple fallback and bind resident scene textures at `TextureHandle.slot + 1` through a single set 1 / binding 0 variable descriptor array. Passes that use or may use this table should call `nr.renderPasses:sceneTextureTableBinding`, which owns frame-slot descriptor-set allocation, fallback prefill, version checks, resident-slot updates, and removed-slot fallback rewrites.
- `AccelerationStructureBuildNode` consumes build-time scene TLAS packets and scene AS mesh queries only during node build. It keeps BLAS storage in a node-owned atlas buffer, repacks/rebuilds on atlas growth instead of copying opaque AS storage, and validates TLAS hit SBT bucket offsets against the configured hit record count. Its deferred record callbacks resolve node-private graph frame data and call `nr.rhi` AS helpers; they must not capture borrowed scene references.
- Not allowed in nodes: hand-written descriptor-set update/bind flows, direct `ShaderBindingPool::update(...)` or `resolveDescriptorWriteRequests(...)` usage, direct `CursorPipelineLayout::bindDescriptorSets(...)` for shader-visible bindings, direct `updateResourcesForBindingSnapshot(...)`, direct `bindPreparedResourcesToCommandBuffer(...)`, direct `pushConstantsToCommandBuffer(...)`, or `bindResourcesToCommandBuffer(...)`.

Current audit note: all built-in shader-visible passes use `RasterPassBuilder`, `ComputePassBuilder`, or `RayTracingPassBuilder`. `NormalBuffer.Raster` is the built-in unordered parallel raster pass. `AccelerationStructureBuildNode`, `Present.CopyToSwapchain`, and `Display.CopyToSwapchain` still use direct `context.addPass(...)` because they are AS-build/copy graph passes without shader-visible bindings.
