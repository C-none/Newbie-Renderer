# RenderPasses Module Terminology

This module uses the same fixed terms as renderer:

- Node: top-level feature unit.
- pass: executable unit emitted by a Node.
- submitNode: explicit graph control node (not a business Node).

Rules:

- A Node may emit multiple passes.
- EmbeddedTriangle Node targets graphics queue output, binds CPU camera uniforms, and emits a single draw-call demo pass.
- NormalBuffer Node targets graphics queue output, outputs world-space interpolated vertex normals for any scene.
- Present Node targets compute queue path and final present preparation.
