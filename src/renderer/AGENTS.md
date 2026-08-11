# `renderer` Rules

These rules extend `src/AGENTS.md` for the renderer-owned pass-builder boundary.

- `RasterPassBuilder`, `ComputePassBuilder`, and `RayTracingPassBuilder` own the shader-visible prepare/record deployment path used by `renderPasses`.
- Preserve the split in which reflection/cursor snapshots and descriptor writes are prepared before recording, while descriptor binding and push constants occur only during command recording.
- Keep lower RHI binding helpers behind the builders rather than exposing manual descriptor management to render-pass nodes.
- Treat changes to builder ownership, snapshot lifetime, or the renderer-to-renderPasses node lifecycle as stable-boundary changes that require the architecture update described by the root rules.
