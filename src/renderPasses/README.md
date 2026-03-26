# RenderPasses Module Terminology

This module uses the same fixed terms as renderer:

- Node: top-level feature unit.
- pass: executable unit emitted by a Node.
- submitNode: explicit graph control node (not a business Node).

Rules:

- A Node may emit multiple passes.
- NormalView Node targets graphics queue output.
- Present Node targets compute queue path and final present preparation.
