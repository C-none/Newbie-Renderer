# RenderPasses Module Terminology

This module uses the same fixed terms as renderer:

- Node: top-level feature unit.
- pass: executable unit emitted by a Node.
- submitNode: explicit graph control node (not a business Node).

Rules:

- A Node may emit multiple passes.
- EmbeddedTriangle Node targets graphics queue output, binds CPU camera uniforms, and emits a single draw-call demo pass.
- NormalBuffer Node targets graphics queue output, outputs world-space interpolated vertex normals for any scene.
- Ui Node targets graphics queue UI composition, may emit a texture upload pass before its overlay draw pass, and keeps texture layout transitions in the render graph resource-intent path.
- Present Node targets compute queue path and final present preparation.

## Binding Checklist

Shader-visible bindings in this module must be reflection-led. Nodes should use `ShaderCursor` to record descriptor-backed resources and push constants into a `ShaderBindingSnapshot`, update descriptor-backed resources from the `addPass` prepare callback, and bind already-updated descriptor sets from the `addPass` record callback.

- Allowed direct command recording: pipeline bind, vertex/index buffers, draw/dispatch, viewport/scissor/dynamic state, rendering scopes, barriers, and copy helpers.
- Prepare-stage helper path for descriptor-backed resources: `updateResourcesForBindingSnapshot(...)`.
- Record-stage helper path for shader-visible bindings: `bindPreparedResourcesToCommandBuffer(...)` for descriptor sets and `pushConstantsToCommandBuffer(...)` for push constants.
- Not allowed in nodes: hand-written descriptor-set update/bind flows, direct `ShaderBindingPool::update(...)` or `resolveDescriptorWriteRequests(...)` usage, direct `CursorPipelineLayout::bindDescriptorSets(...)` for shader-visible bindings, or push-constant deployment outside the cursor snapshot helpers.
- Persistent or bindless resources may allocate and cache `ShaderBindingSet`s, but descriptor writes and command-buffer descriptor binding still route through the split shared RHI helper path.
- `bindResourcesToCommandBuffer(...)` remains a synchronous compatibility helper and should not be used by RDG parallel record paths.

Current audit note: built-in nodes update descriptor snapshots during prepare and only bind prepared descriptor sets during record. `UiNode` push constants and bindless texture descriptors follow cursor snapshots plus the shared RHI deployment helpers. The previous bindless texture bypass was node-level policy drift rather than missing RHI support.
