# `renderPasses` Rules

These rules extend `src/AGENTS.md`. Keep `README.md` synchronized when the node lifecycle or binding architecture changes.

- Except for vertex/index buffers and pipeline-fixed resources, every shader-visible descriptor resource and push constant must be driven by `nrslang` reflection and binding relations recorded through `shaderCursor`.
- Use renderer-side `RasterPassBuilder`, `ComputePassBuilder`, or `RayTracingPassBuilder` for shader-visible passes. Descriptor-backed resources are updated in prepare; descriptor sets and push constants are bound or pushed only in record.
- A lower-level shader-visible `addPass` path must preserve the same reflection, cursor, prepare-update, and record-bind/push contract. Direct `addPass` remains valid for passes without shader-visible bindings, such as transfer, acceleration-structure build, or typed external-operation passes.
- Do not hand-write Vulkan descriptor-set update/bind flows or push-constant flows in render-pass nodes, and do not call the lower RHI binding helpers directly from those nodes.
- GPU-only uploads follow the `UploadReadbackContext` ring rule in `src/rhi/AGENTS.md`. Do not add persistent per-node `TransferSrc` staging buffers; direct mapped writes remain valid only for intentionally CPU-writable frame resources.
