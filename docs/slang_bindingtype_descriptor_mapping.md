# Slang BindingType and Vulkan DescriptorType Mapping

## Scope

This document explains:

- what `slang::BindingType` means
- what `vk::DescriptorType` means
- why the mapping implemented by `src/rhi/nrDescriptor.cpp` is designed this way
- how the runtime binding flow is completed in `nrDescriptor` and `nrPipeline`

The implementation described here only covers `src/rhi` and does not modify `src/extern/slang`.

## Concepts

### Slang `BindingType`

`slang::BindingType` is a reflection-level semantic category. It tells the host what kind of shader parameter is represented at a binding range (sampler, texture, raw buffer, typed buffer, push constant, etc.).

Important characteristics:

- It is API-neutral (works across Vulkan, D3D12, Metal, and others).
- It describes shader resource semantics, not direct Vulkan API objects.
- Some binding types are not descriptor-backed on Vulkan (for example `PushConstant`).

### Vulkan `DescriptorType`

`vk::DescriptorType` is Vulkan pipeline layout and descriptor set ABI detail. It defines exactly how a descriptor slot is interpreted by the driver and hardware.

Important characteristics:

- It is Vulkan-specific.
- It directly determines descriptor set layout creation and descriptor write structure shape.
- It is required at descriptor set layout creation and at descriptor update.

## Mapping Table

Current mapping in `src/rhi/nrDescriptor.cpp` (`detail::toVkDescriptorType`):

| Slang `BindingType` | Vulkan `DescriptorType` | Notes |
|---|---|---|
| `Sampler` | `eSampler` | sampler-only descriptor |
| `CombinedTextureSampler` | `eCombinedImageSampler` | image+sampler in one descriptor |
| `Texture` | `eSampledImage` | sampled image view |
| `MutableTexture` | `eStorageImage` | UAV/storage image |
| `InputRenderTarget` | `eInputAttachment` | subpass input attachment |
| `ConstantBuffer` | `eUniformBuffer` | constant/uniform buffer |
| `ParameterBlock` | `eUniformBuffer` | default policy in this engine |
| `TypedBuffer` | `eUniformTexelBuffer` | formatted read buffer |
| `MutableTypedBuffer` | `eStorageTexelBuffer` | formatted read-write buffer |
| `RawBuffer` | `eStorageBuffer` | structured/raw buffer view |
| `MutableRawBuffer` | `eStorageBuffer` | read-write raw buffer |
| `InlineUniformData` | `eInlineUniformBlock` | runtime bytes are recorded through `ShaderCursor::setData(...)` |
| `RayTracingAccelerationStructure` | `eAccelerationStructureKHR` | ray tracing AS descriptor |
| `PushConstant` | N/A (not descriptor) | handled via pipeline push constant ranges |
| `Unknown`, `VaryingInput`, `VaryingOutput`, `ExistentialValue`, `MutableFlag`, `BaseMask`, `ExtMask` | N/A | not mapped in descriptor layout |

## Shader Declaration to BindingType to DescriptorType

This section answers a practical question: when you write a resource declaration in Slang shader code, what does reflection report as `slang::BindingType`, and what Vulkan descriptor type does this backend use?

### Common examples

| Slang shader declaration example | Reflected `slang::BindingType` | Vulkan `vk::DescriptorType` in this project |
|---|---|---|
| `SamplerState linearSampler;` | `Sampler` | `eSampler` |
| `Texture2D<float4> tex;` | `Texture` | `eSampledImage` |
| `RWTexture2D<float4> rwTex;` | `MutableTexture` | `eStorageImage` |
| `ConstantBuffer<MyData> cb;` | `ConstantBuffer` | `eUniformBuffer` |
| `ParameterBlock<MyData> params;` | `ParameterBlock` | `eUniformBuffer` (project default policy) |
| `Buffer<float4> typedBuf;` | `TypedBuffer` | `eUniformTexelBuffer` |
| `RWBuffer<float4> rwTypedBuf;` | `MutableTypedBuffer` | `eStorageTexelBuffer` |
| `ByteAddressBuffer rawBuf;` | `RawBuffer` | `eStorageBuffer` |
| `RWByteAddressBuffer rwRawBuf;` | `MutableRawBuffer` | `eStorageBuffer` |
| `[[vk::push_constant]] ConstantBuffer<PC> pushData;` | `PushConstant` | N/A (not descriptor; goes to push constant range) |
| `RaytracingAccelerationStructure sceneAS;` | `RayTracingAccelerationStructure` | `eAccelerationStructureKHR` |

### Representative shader snippet

```slang
struct CbData
{
   uint bias;
   float scale;
};

[[vk::push_constant]]
ConstantBuffer<CbData> pushData;            // PushConstant -> no descriptor

ConstantBuffer<CbData> cbData;              // ConstantBuffer -> eUniformBuffer
Texture2D<float4> tex2d;                    // Texture -> eSampledImage
RWTexture2D<float4> rwTex2d;                // MutableTexture -> eStorageImage
SamplerState linearSampler;                 // Sampler -> eSampler
Buffer<float4> typedBuffer;                 // TypedBuffer -> eUniformTexelBuffer
RWBuffer<float4> rwTypedBuffer;             // MutableTypedBuffer -> eStorageTexelBuffer
ByteAddressBuffer rawBuffer;                // RawBuffer -> eStorageBuffer
RWByteAddressBuffer rwRawBuffer;            // MutableRawBuffer -> eStorageBuffer
```

### Notes on reflection behavior

- Reflection reports resource categories as `BindingType` per binding range, not by variable spelling alone.
- Arrays keep the same `BindingType`; the descriptor count (and array element addressing) changes.
- Unbounded arrays such as `Texture2D<float4> textures[];` still reflect as the same resource semantic, but the engine can now map them to runtime-sized Vulkan descriptor-array bindings when `DescriptorBindingPolicy` enables variable descriptor count.
- `PushConstant` is intentionally outside descriptor-set mapping.
- `ParameterBlock<T>` is a grouping abstraction in Slang. In this backend, default mapping policy is `eUniformBuffer` for descriptor-backed ordinary data path.

### Where this is verified in the project

- Runtime mapping and cursor API: `src/rhi/nrDescriptor.cpp` and `src/rhi/nrDescriptor.ixx`
- Cursor snapshot and descriptor-write contract: `test/unit/rhi/nr_rhi_shader_cursor_contract_test.cpp`
- Ray-tracing descriptor reflection contract: `test/unit/rhi/nr_rhi_rt_shader_reflection_contract_test.cpp`
- Runtime smoke coverage path: `test/smoke/app/normalBufferUiSmoke.cpp`

## Why this Mapping

### 1) Preserve Slang semantic intent while producing Vulkan ABI

Slang reflection gives a portable semantic category. Vulkan requires concrete descriptor ABI. The mapping is the translation boundary.

### 2) `ParameterBlock` defaulting to `eUniformBuffer`

Reason:

- In Slang, `ParameterBlock<T>` is a grouping concept. For Vulkan, ordinary data in the block is represented through uniform-buffer-like binding behavior.
- `eUniformBufferDynamic` requires dynamic offsets at bind time. Making it the default forces a runtime policy on all callers.
- This engine keeps dynamic offsets as an optional higher-level policy, not an implicit default.

So the default mapping is `ParameterBlock -> eUniformBuffer`.

### 3) `PushConstant` is intentionally not a descriptor

Push constants are part of `VkPipelineLayout` push constant ranges and `vkCmdPushConstants`, not descriptor sets. Therefore this binding type is excluded from descriptor mapping and handled in push constant range collection.

Newbie-Renderer enforces a project hard limit of 128 bytes for push constants (`nr::rhi::kMaxPushConstantBytes`). Payloads larger than that must be split into renderer global frame uniforms or buffer/texture upload resources.

## Runtime Binding Flow (Implemented)

### Reflection and layout discovery

- `ShaderDescriptorLayout::create(...)` in `src/rhi/nrDescriptor.cpp`
- Collects descriptor set/binding metadata and push constant metadata
- Can now mark unbounded descriptor bindings as runtime-sized and attach:
  - `eVariableDescriptorCount`
  - `ePartiallyBound`
  - `eUpdateAfterBind`
- Exposes `ShaderCursor` for path-based lookup

### Runtime descriptor arrays and cursor indexing

- `ShaderCursor` can now index descriptor-backed resource arrays, including runtime-sized arrays such as `Texture2D[]`.
- This support covers two reflection shapes:
  - normal array/resource element layouts
  - descriptor-array reflection shapes that expose binding counts without a regular `elementTypeLayout`
- `ShaderCursor::referencesRuntimeDescriptorArray()` and `ShaderCursor::bindingDescriptorCount()` are the current runtime query helpers for this path.
- `ShaderBindingSet` now records the allocated descriptor capacity for variable-count bindings so write validation uses the real runtime set capacity rather than only the layout default.
- `DescriptorBindingPolicy` defaults to the semantic multi-set ABI for runtime-sized descriptor arrays. The RHI validates the set reported by Slang reflection and never remaps shader-declared `[[vk::binding(binding, set)]]` values on the host side.

Default runtime-array set convention:

| Runtime descriptor semantic | Required set |
|---|---:|
| `Sampler` | 0 |
| `CombinedImageSampler` / `SampledImage` | 1 |
| `StorageImage` | 2 |
| buffer and texel-buffer descriptors | 3 |
| `AccelerationStructure` | 4 |

Fixed-size descriptors can still use shader-declared sets outside this convention. The shared global frame uniform `gFrame` is one such fixed descriptor and lives at Vulkan set 5, binding 0. Scene lights use fixed descriptors at set 6: `gSceneLightHeader` at binding 0, `gSceneLights` at binding 1, and `gSceneLightAliasTable` at binding 2. The runtime-array convention only applies when Slang reports an unbounded/runtime-sized descriptor array. Runtime-sized input-attachment and inline-uniform-block arrays do not have reserved semantic sets in the current ABI; add an explicit convention only when a real pass requires that model.

Scene material textures use this convention through a single global combined image sampler table: `gSceneTextures[]` is declared at Vulkan set 1, binding 0. Shader-visible material texture IDs default to 0, where the renderer binds a 1x1 neutral white fallback texture (linear RGBA(1,1,1,1)); resident scene textures use `TextureHandle.slot + 1` as their descriptor index. `MaterialTextureSlot` only defines push-constant/draw-packet ordering and does not create additional set 1 bindings. Raster material texture IDs are stored as 16-bit values and packed two IDs per 32-bit push-constant lane; `kSceneTextureDescriptorCapacity` must fit that ABI. `shader/include/share/materialTextureIds.slang` owns the shader-visible `MaterialTextureSlot` enum, while `shader/include/materialTextureIds.slang` owns the packed-ID unpack helpers so raster, GBuffer, and RT shaders share one unpack ABI. C++ render passes bind the table through the shared `nr.renderPasses:sceneTextureTableBinding` adapter, which forwards table application to the renderer-owned bindless image table cache for frame-slot allocation invalidation, fallback prefill, version checks, resident-slot updates, and removed-slot fallback rewrites.

### Reflection lifetime requirement

- `ShaderDescriptorLayout` stores reflection-derived raw pointers into the linked Slang program layout.
- Because of that, `PipelineState` in `src/rhi/nrPipeline.ixx` now retains a `SlangProgram` copy for the full lifetime of the descriptor layout and cursor usage.
- Removing that ownership without replacing it with another lifetime guarantee will reintroduce dangling reflection pointers.

### Cursor-owned binding snapshot

- A root `ShaderCursor` owns shared mutable binding state; copied field/element cursors write
  into the same state.
- `ShaderCursor::setObject(...)` records descriptor-backed resources:
   - RAII `Buffer` and `Image`
   - `vk::Sampler`, `vk::BufferView`, and `vk::AccelerationStructureKHR`
   - `LogicalResourceDescriptorWrite` for resources resolved later from an RDG pass context
- `ShaderCursor::setData(...)` records push-constant bytes or inline-uniform bytes.
- `ShaderCursor::snapshot()` captures the immutable `ShaderBindingSnapshot` consumed by the
  pipeline prepare/record path.
- `resolveDescriptorWriteRequests(...)` resolves logical resources and produces
  `DescriptorWriteRequest` values with the set/binding metadata already carried by the
  snapshot.

### Descriptor update submission

- `ShaderBindingPool::update(...)` in `src/rhi/nrDescriptor.cpp`
- Converts `DescriptorWriteRequest` payloads to Vulkan write structs
- Calls `vkUpdateDescriptorSets` through RAII device
- Validates set index and array element bounds
- Logical buffer descriptors carry explicit offset and range; renderer global frame uniforms use this to bind suballocations from the renderer-owned CPU-to-GPU uniform arena.

### RAII resource adaptation

- `ShaderCursor::setObject(...)` adapts the project resource wrappers directly and validates
  them against the reflected binding:
   - buffer descriptors carry explicit offset/range
   - image descriptors carry layout and optional sampler
   - typed-buffer writes may create a `BufferView` from a mutable `Buffer`
   - acceleration structures and logical RDG resources use dedicated overloads
- VMA-backed resource details remain behind `Buffer` and `Image`; the snapshot stores Vulkan
  descriptor payloads or typed logical-resource records, not ownership.

### Command binding

Pipeline-layout-aware helpers provide:

- `CursorPipelineLayout::bindDescriptorSet(...)`
- `CursorPipelineLayout::bindDescriptorSets(...)`
- `CursorPipelineLayout::pushConstants(...)`

`src/rhi/nrCommand.ixx` currently only provides command-buffer begin/end recording helpers and does not expose descriptor/pipeline bind wrappers.

## Resource-Type Support Matrix (Code-Accurate)

This matrix reflects current behavior in `src/rhi/nrDescriptor.cpp` and its
`src/rhi/nrDescriptor.ixx` interface:

| Slang `BindingType` | Mapped to Vulkan descriptor | `ShaderCursor` write API | `ShaderBindingPool::update` payload support | End-to-end runtime bindable now |
|---|---|---|---|---|
| `Sampler` | Yes (`eSampler`) | `setObject(vk::Sampler)` | Yes (`ImageDescriptorWrite`) | Yes |
| `CombinedTextureSampler` | Yes (`eCombinedImageSampler`) | `setObject(Image, vk::Sampler, layout)` | Yes (`ImageDescriptorWrite`) | Yes |
| `Texture` | Yes (`eSampledImage`) | `setObject(Image, layout)` | Yes (`ImageDescriptorWrite`) | Yes |
| `InputRenderTarget` | Yes (`eInputAttachment`) | `setObject(Image, layout)` | Yes (`ImageDescriptorWrite`) | Yes |
| `MutableTexture` | Yes (`eStorageImage`) | `setObject(Image, layout)` | Yes (`ImageDescriptorWrite`) | Yes |
| `ConstantBuffer` | Yes (`eUniformBuffer`) | `setObject(Buffer, offset, range)` | Yes (`BufferDescriptorWrite`) | Yes |
| `ParameterBlock` | Yes (`eUniformBuffer`) | `setObject(Buffer, offset, range)` | Yes (`BufferDescriptorWrite`) | Yes |
| `TypedBuffer` | Yes (`eUniformTexelBuffer`) | `setObject(BufferView)` or typed-buffer overload | Yes (`TexelBufferDescriptorWrite`) | Yes |
| `MutableTypedBuffer` | Yes (`eStorageTexelBuffer`) | `setObject(BufferView)` or typed-buffer overload | Yes (`TexelBufferDescriptorWrite`) | Yes |
| `RawBuffer` | Yes (`eStorageBuffer`) | `setObject(Buffer, offset, range)` | Yes (`BufferDescriptorWrite`) | Yes |
| `MutableRawBuffer` | Yes (`eStorageBuffer`) | `setObject(Buffer, offset, range)` | Yes (`BufferDescriptorWrite`) | Yes |
| `RayTracingAccelerationStructure` | Yes (`eAccelerationStructureKHR`) | `setObject(vk::AccelerationStructureKHR)` | Yes (`AccelerationStructureDescriptorWrite`) | Yes |
| `InlineUniformData` | Yes (`eInlineUniformBlock`) | `setData(...)` | Yes (`InlineUniformDescriptorWrite` + `VkWriteDescriptorSetInlineUniformBlock`) | Yes |
| `PushConstant` | Not descriptor-backed | `setData(...)` | N/A | Yes (via push constants path) |

## Practical Usage Pattern

1. Build descriptor layout from Slang program.
2. Create pipeline layout and descriptor pool.
3. Allocate descriptor sets.
4. Record objects/data through the root cursor and capture its binding snapshot.
5. Resolve logical resources and submit changed descriptor requests through `ShaderBindingPool::update(...)`.
6. Bind pipeline and prepared descriptor sets on the command buffer.
7. Push the snapshot's constant ranges through the pipeline-layout helper.

## Notes and Constraints

- `InlineUniformData` runtime writes follow Vulkan inline-uniform rules: write size and destination byte offset are in bytes and must both be multiples of 4.
- Device creation enables and validates `VK_EXT_inline_uniform_block` / `inlineUniformBlock` via the feature chain in `Device::makeDevice()`.
- Acceleration structure descriptors require device feature support; the current policy enforces required support during device extension enable flow (`Device::makeDevice()`), not via pipeline-time secondary capability validators.
- Multi-set bind helper in `CursorPipelineLayout` intentionally disallows shared dynamic offsets to avoid ambiguous offset routing.
- If dynamic UBO policy is needed later, add explicit API for binding-specific dynamic offset routing rather than changing default mapping.
