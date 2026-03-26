# Renderer Module Terminology

This module uses the following fixed terms:

- Node: a feature-level graph producer that can emit one or more passes.
- pass: an executable graph unit with queue domain, resource uses, and an optional record callback.
- submitNode: an explicit execution-control node that splits submit batches.

Scope boundary:

- renderer owns frame-graph build/compile/execute orchestration, renderer-persistent resources, graph-transient planning, queue submit structure, and present orchestration.
- renderer does not own scene asset lifecycle or input handling.
- present path is compute-queue oriented.
