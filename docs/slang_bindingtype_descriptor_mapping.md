# Slang BindingType and Vulkan DescriptorType Mapping

## Scope

This document explains:

- what `slang::BindingType` means
- what `vk::DescriptorType` means
- why the mapping in `src/rhi/nrDescriptor.ixx` is designed this way
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

Current mapping in `src/rhi/nrDescriptor.ixx` (`detail::toVkDescriptorType`):

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
| `InlineUniformData` | `eInlineUniformBlock` | runtime write path is implemented via `ShaderResourceWriter::bindInlineUniformData(...)` |
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

- Shader declaration coverage example: `shader/test/main/resourceBindingReflection.slang`
- Runtime mapping function: `src/rhi/nrDescriptor.ixx`
- Runtime smoke coverage path: `test/smoke/app/normalBufferUiSmoke.cpp` (through `shaderCursor` + descriptor write/update + draw/dispatch binding flow)

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

- `ShaderDescriptorLayout::create(...)` in `src/rhi/nrDescriptor.ixx`
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

Fixed-size descriptors can still use shader-declared sets outside this convention. The convention only applies when Slang reports an unbounded/runtime-sized descriptor array. Runtime-sized input-attachment and inline-uniform-block arrays do not have reserved semantic sets in the current ABI; add an explicit convention only when a real pass requires that model.

### Reflection lifetime requirement

- `ShaderDescriptorLayout` stores reflection-derived raw pointers into the linked Slang program layout.
- Because of that, `PipelineState` in `src/rhi/nrPipeline.ixx` now retains a `SlangProgram` copy for the full lifetime of the descriptor layout and cursor usage.
- Removing that ownership without replacing it with another lifetime guarantee will reintroduce dangling reflection pointers.

### Write request construction

- `ShaderCursor` is intentionally address/reflection-only:
   - path navigation (`field`, `element`, `getPath`)
   - metadata query (`descriptorBinding`, type/layout helpers)
- `ShaderResourceWriter` in `src/rhi/nrDescriptor.ixx` builds write requests from runtime resources:
   - RAII `Buffer` / `Image`
   - `vk::Sampler`, `vk::BufferView`, `vk::AccelerationStructureKHR`
- Output is `DescriptorWriteRequest` with resolved set/binding metadata from `ShaderCursor`.

### Descriptor update submission

- `ShaderBindingPool::update(...)` in `src/rhi/nrDescriptor.ixx`
- Converts `DescriptorWriteRequest` payloads to Vulkan write structs
- Calls `vkUpdateDescriptorSets` through RAII device
- Validates set index and array element bounds
- Logical buffer descriptors carry explicit offset and range; renderer global frame uniforms use this to bind suballocations from the renderer-owned CPU-to-GPU uniform arena.

### RAII resource adaptation

- `ShaderResourceWriter` directly adapts project resource wrappers:
   - `bindUniformBuffer(const ShaderCursor&, const Buffer&, ...)`
   - `bindStorageBuffer(const ShaderCursor&, const Buffer&, ...)`
   - `bindSampledImage(const ShaderCursor&, const Image&, ...)`
   - `bindStorageImage(const ShaderCursor&, const Image&, ...)`
   - `bindCombinedImageSampler(const ShaderCursor&, const Image&, vk::Sampler, ...)`
   - texel-buffer overloads that can auto-create `BufferView` from `Buffer::addView(...)`
- This keeps VMA-backed resource details in resource/pipeline layer, not in cursor reflection layer.

### Command binding

Two layers are available:

1. Pipeline-layout aware helpers:
   - `CursorPipelineLayout::bindDescriptorSet(...)`
   - `CursorPipelineLayout::bindDescriptorSets(...)`
   - `CursorPipelineLayout::pushConstants(...)`

`src/rhi/nrCommand.ixx` currently only provides command-buffer begin/end recording helpers and does not expose descriptor/pipeline bind wrappers.

## Resource-Type Support Matrix (Code-Accurate)

This matrix reflects current behavior in `src/rhi/nrDescriptor.ixx`:

| Slang `BindingType` | Mapped to Vulkan descriptor | `ShaderResourceWriter` bind API present | `ShaderBindingPool::update` payload support | End-to-end runtime bindable now |
|---|---|---|---|---|
| `Sampler` | Yes (`eSampler`) | Yes (`bindSampler`) | Yes (`ImageDescriptorWrite`) | Yes |
| `CombinedTextureSampler` | Yes (`eCombinedImageSampler`) | Yes (`bindCombinedImageSampler`) | Yes (`ImageDescriptorWrite`) | Yes |
| `Texture` | Yes (`eSampledImage`) | Yes (`bindSampledImage`) | Yes (`ImageDescriptorWrite`) | Yes |
| `InputRenderTarget` | Yes (`eInputAttachment`) | Reuses `bindSampledImage` allow-list | Yes (`ImageDescriptorWrite`) | Yes |
| `MutableTexture` | Yes (`eStorageImage`) | Yes (`bindStorageImage`) | Yes (`ImageDescriptorWrite`) | Yes |
| `ConstantBuffer` | Yes (`eUniformBuffer`) | Yes (`bindUniformBuffer`) | Yes (`BufferDescriptorWrite`) | Yes |
| `ParameterBlock` | Yes (`eUniformBuffer`) | Yes (`bindUniformBuffer`) | Yes (`BufferDescriptorWrite`) | Yes |
| `TypedBuffer` | Yes (`eUniformTexelBuffer`) | Yes (`bindUniformTexelBuffer`) | Yes (`TexelBufferDescriptorWrite`) | Yes |
| `MutableTypedBuffer` | Yes (`eStorageTexelBuffer`) | Yes (`bindStorageTexelBuffer`) | Yes (`TexelBufferDescriptorWrite`) | Yes |
| `RawBuffer` | Yes (`eStorageBuffer`) | Yes (`bindStorageBuffer`) | Yes (`BufferDescriptorWrite`) | Yes |
| `MutableRawBuffer` | Yes (`eStorageBuffer`) | Yes (`bindStorageBuffer`) | Yes (`BufferDescriptorWrite`) | Yes |
| `RayTracingAccelerationStructure` | Yes (`eAccelerationStructureKHR`) | Yes (`bindAccelerationStructure`) | Yes (`AccelerationStructureDescriptorWrite`) | Yes |
| `InlineUniformData` | Yes (`eInlineUniformBlock`) | Yes (`bindInlineUniformData`) | Yes (`InlineUniformDescriptorWrite` + `VkWriteDescriptorSetInlineUniformBlock`) | Yes |
| `PushConstant` | Not descriptor-backed | N/A | N/A | Yes (via push constants path) |

## Redundancy and Optimization Notes

Two redundant traversal hotspots were optimized in code:

1. Descriptor-set grouping in `ShaderDescriptorLayout::create(...)`
   - Before: gather set indices, sort/unique, then rescan all bindings per set.
   - Now: single-pass grouping directly over `bindingBySetAndBinding_` (already ordered by `(set, binding)`).

2. Descriptor write pre-counting in `ShaderBindingPool::update(...)`
   - Before: four `count_if` passes (buffer/texel/image/AS) plus one build pass.
   - Now: single build pass with upfront reserve by `writeRequests.size()`.

These changes preserve external APIs and behavior while reducing repeated scans.

## Practical Usage Pattern

1. Build descriptor layout from Slang program.
2. Create pipeline layout and descriptor pool.
3. Allocate descriptor sets.
4. Build write requests from `ShaderCursor`.
5. Submit descriptor writes through `ShaderBindingPool::update(...)`.
6. Bind pipeline and descriptor sets on command buffer.
7. Push constants through layout or command helpers.

## Notes and Constraints

- `InlineUniformData` runtime writes follow Vulkan inline-uniform rules: write size and destination byte offset are in bytes and must both be multiples of 4.
- Device creation enables and validates `VK_EXT_inline_uniform_block` / `inlineUniformBlock` via the feature chain in `Device::makeDevice()`.
- Acceleration structure descriptors require device feature support; the current policy enforces required support during device extension enable flow (`Device::makeDevice()`), not via pipeline-time secondary capability validators.
- Multi-set bind helper in `CursorPipelineLayout` intentionally disallows shared dynamic offsets to avoid ambiguous offset routing.
- If dynamic UBO policy is needed later, add explicit API for binding-specific dynamic offset routing rather than changing default mapping.
