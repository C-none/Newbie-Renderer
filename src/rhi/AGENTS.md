# `nr.rhi` Rules

These rules extend `src/AGENTS.md` for RHI implementation and Vulkan-facing tests.

## Target and Failure Policy

- Target Windows, Vulkan, and NVIDIA GeForce RTX 5070 Ti-class hardware only.
- Required Vulkan extensions and features may be assumed on the target. Do not add vendor, hardware, extension, or non-Vulkan fallback paths.
- If a required capability is unexpectedly unavailable, fail fast with a clear `errorHandle` diagnostic.

## Resource Lifetime and Commands

- Vulkan resources must be owned by RAII objects that release them in destructors. Prefer constructor/destructor lifetime over replaceable `init()`/`destroy()` pairs.
- Do not add custom PFN dispatch tables, per-command function-pointer caches, manual `getProcAddr` paths, or raw `vk*`/`vkCmd*` command calls in project RHI or Vulkan-facing tests.
- Invoke Vulkan commands through Vulkan-Hpp RAII object members. When an external operation needs an RHI surface, expose a narrow typed wrapper that forwards directly without another dispatch or indirection layer.

## Uploads

- Call `Buffer::writeMappedAndFlush(...)` only for resources intentionally allocated as CPU-writable GPU-visible memory, such as per-frame uniforms or dynamic vertex/index buffers.
- Upload all other CPU data through the device-level `nr::rhi::ops::UploadReadbackContext` ring with `uploadBuffer(...)` or `uploadImage(...)`; do not create competing module-local staging systems.
- Preserve transfer-to-destination queue ownership. Complete the destination-queue acquire barrier before exposing an uploaded resource as resident or importing it for shader-visible graph use.
