# RHI Upload/Readback Practical Limits Audit

This document covers investigation item 3 only: practical limitations in the
current upload/readback path.

The audit assumes a single Vulkan backend. The current staging-ring path is
valid and useful on discrete GPUs, but it is not yet a complete streaming,
partial-update, or diagnostic readback contract.

## Official Baseline

Primary references:

- Vulkan Guide image copy permutations:
  <https://docs.vulkan.org/guide/latest/image_copies.html>
- `VK_EXT_host_image_copy` proposal:
  <https://docs.vulkan.org/features/latest/features/proposals/VK_EXT_host_image_copy.html>
- `VK_KHR_map_memory2` proposal:
  <https://docs.vulkan.org/features/latest/features/proposals/VK_KHR_map_memory2.html>
- `VkBufferImageCopy` reference:
  <https://docs.vulkan.org/refpages/latest/refpages/source/VkBufferImageCopy.html>
- Vulkan copy command specification chapter:
  <https://docs.vulkan.org/spec/latest/chapters/copies.html>
- `vkFlushMappedMemoryRanges` reference:
  <https://docs.vulkan.org/refpages/latest/refpages/source/vkFlushMappedMemoryRanges.html>
- `vkInvalidateMappedMemoryRanges` reference:
  <https://docs.vulkan.org/refpages/latest/refpages/source/vkInvalidateMappedMemoryRanges.html>
- Vulkan synchronization examples:
  <https://docs.vulkan.org/guide/latest/synchronization_examples.html>
- Vulkan 1.4 reference:
  <https://docs.vulkan.org/refpages/latest/refpages/source/VK_VERSION_1_4.html>
- Vulkan Memory Allocator memory mapping notes:
  <https://gpuopen-librariesandsdks.github.io/VulkanMemoryAllocator/html/memory_mapping.html>

Important baseline facts:

- Vulkan has three relevant generations of image copy paths:
  Vulkan 1.0 command-buffer copies, Vulkan 1.3 copy commands 2, and Vulkan 1.4
  host image copy. The Vulkan Guide explicitly lists `copyBufferToImage2`,
  `copyImageToBuffer2`, `copyMemoryToImage`, and `copyImageToMemory` as the
  modern alternatives around image upload/readback.
- Traditional staging buffer upload/readback is still valid. It requires
  transfer usage flags, command-buffer copy, image layout transitions, and
  queue synchronization.
- `VK_EXT_host_image_copy`, promoted into Vulkan 1.4, exists to avoid the extra
  staging buffer for some image copies. It exposes `hostImageCopy`, supported
  source/destination layouts, host copy commands, and the
  `VK_IMAGE_USAGE_HOST_TRANSFER_BIT` requirement. It is a separate path, not a
  universal replacement for staging.
- `VkBufferImageCopy` row pitch fields are in texels. `bufferRowLength == 0`
  and `bufferImageHeight == 0` mean tightly packed rows/slices according to the
  copied extent. The aspect mask in a buffer-image copy must contain one aspect
  bit only.
- Copy size and addressing are format-sensitive. Compressed formats,
  multi-plane formats, packed depth/stencil formats, and block-size rules
  cannot be modeled correctly by a generic "bytes per pixel" table.
- Host writes to non-coherent memory require flush; device writes read by the
  host require invalidate. Vulkan defines the visibility region in terms of
  `nonCoherentAtomSize`. VMA's allocation flush/invalidate helpers handle that
  alignment internally.
- `VK_KHR_map_memory2`, promoted into Vulkan 1.4, adds extensible map/unmap
  structures. It does not change the basic coherency model by itself, but it is
  the modern extension point for future mapping behavior.
- Semaphore signaling/waiting can provide memory dependency for buffers or
  images whose layout does not change, but image layout transitions and queue
  ownership transfers still need explicit barriers.

## Current RHI Surface

### 1. Persistent mapped rings

Current status: implemented.

Evidence:

- `UploadReadbackContext` in `src/rhi/nrResourceOps.ixx` creates one upload
  ring and one readback ring, both defaulting to 64 MiB.
- The upload ring is `MemoryUsage::CpuToGpu`, `eTransferSrc`, persistently
  mapped, and exclusive to the transfer queue.
- The readback ring is `MemoryUsage::GpuToCpu`, `eTransferDst`, persistently
  mapped, and shared by graphics/compute queue families when needed.
- VMA allocation wrappers expose `flush(...)` and `invalidate(...)`, and local
  comments state that VMA handles non-coherent atom-size alignment.

Assessment:

- This is a sound staging foundation.
- The fixed-size ring model needs explicit behavior for large images,
  partial image updates, and high-throughput readback workloads.

### 2. Buffer upload

Current status: mostly practical.

Evidence:

- `uploadBuffer(...)` copies host bytes into the upload ring, flushes the ring
  allocation, records copy commands 2 through `copyBuffer2(...)`, and submits
  to the transfer queue.
- If the payload exceeds ring capacity, `uploadBuffer(...)` chunks it into
  ordered ring allocations and submissions.
- The final transfer submission records a release barrier to the destination
  queue ownership plan.

Assessment:

- Large buffer uploads are supported through chunking.
- The command recording path is now pNext-capable through copy commands 2.
- The caller must provide the ownership plan; there is no render-graph-level
  upload transaction abstraction.

### 3. Image upload

Current status: usable for one tight image payload.

Evidence:

- `uploadImage(...)` copies exactly one host byte span into the upload ring,
  transitions the image to `eTransferDstOptimal`, records
  `copyBufferToImage2(...)`, and transitions/releases to the requested final
  layout.
- `uploadImage(...)` accepts one optional `vk::BufferImageCopy` region.
- The implementation explicitly asserts that the image payload must fit within
  the upload ring.
- Scene texture upload currently creates one-mip, one-layer GPU images and
  uploads only the first CPU texture level.
- UI texture upload uses `UploadReadbackContext::uploadImage(...)`; the UI plan
  still lists partial texture updates as future work.

Assessment:

- The path is good for small to medium tightly packed textures.
- It is not a full texture streaming path: no chunked image upload, no region
  list, no multi-mip/multi-layer upload, and no host image copy route.

### 4. Buffer and image readback

Current status: implemented but intentionally synchronous at consumption.

Evidence:

- `readbackBuffer(...)` records a producer-to-transfer-read barrier, copies to
  the readback ring with `copyBuffer2(...)`, records
  transfer-write-to-host-read visibility, submits on the selected graphics or
  compute queue, and returns a `ReadbackTicket`.
- `readbackImage(...)` records a layout transition to
  `eTransferSrcOptimal`, copies to the readback ring with
  `copyImageToBuffer2(...)`, restores the original layout, and returns a
  `ReadbackTicket`.
- `readbackBytes(...)` waits for the ticket timeline value, invalidates the
  readback ring range, copies bytes into a new `std::vector<std::byte>`, and
  returns it.
- Readback queue role validation accepts graphics and compute only.

Assessment:

- This is sufficient for debugging captures and small CPU readbacks.
- It is not a zero-copy or non-blocking CPU consumption model.
- The shared readback timeline serializes readback submissions across graphics
  and compute roles.

### 5. Feature detection for modern copy paths

Current status: partially implemented.

Evidence:

- `Device::makeDevice()` includes `vk::PhysicalDeviceVulkan14Features` in the
  feature chain and exposes `hostImageCopy` through
  `Device::vulkan14Capabilities()`.
- `Device::vulkan14Properties()` snapshots the Vulkan 1.4 host-image-copy
  layout lists and `identicalMemoryTypeRequirements`.
- RHI upload/readback helpers use `copyBuffer2`, `copyBufferToImage2`, and
  `copyImageToBuffer2`.
- No helper currently calls `copyMemoryToImage` or `copyImageToMemory`.

Assessment:

- The current code can inspect whether a host-image-copy path is available.
- Staging remains the only implemented upload/readback route.

## Practical Limitations

### 1. Image upload/readback cannot handle payloads larger than the ring

Severity: high.

Evidence:

- `uploadBuffer(...)` chunks oversized buffer payloads.
- `uploadImage(...)` asserts when `payloadSize > uploadCapacity_`.
- `readbackImage(...)` reserves one readback allocation sized for the whole
  image copy and uses the same ring-capacity assertion through `reserveRing`.
- `UploadReadbackContext` defaults the upload and readback rings to 64 MiB.
- `SceneCreateInfo::uploadBudgetBytesPerFrame` defaults to 128 MiB, which is
  larger than the default image upload ring.

Impact:

- A single texture can be accepted by the scene upload budget but still fail
  the RHI image upload path.
- Large HDR targets, high-resolution screenshots, texture arrays, and 3D
  textures can exceed the ring even when total frame streaming budget is
  reasonable.
- Buffer upload has a behavior that image upload/readback does not, so callers
  cannot reason about "large uploads" uniformly.

Recommendation:

- Add image chunking by rows/slices/layers/mips using legal
  `vk::BufferImageCopy` regions.
- Keep a hard maximum for single-region copies only where Vulkan block-size
  rules require it.
- Align scene upload budget defaults with RHI ring capacity, or expose one
  shared upload budget/capacity configuration.

### 2. Image copy API is single-region and exact-size

Severity: high for streaming and UI updates.

Evidence:

- `uploadImage(...)` accepts one `vk::BufferImageCopy`, not a span of regions.
- `readbackImage(...)` accepts one region and returns only a byte ticket.
- `linearBufferImageCopySize(...)` computes one payload size and
  `uploadImage(...)` requires `data.size_bytes() == payloadSize`.
- The UI integration plan still lists dirty-rectangle upload as future work.

Impact:

- Partial texture updates need manual extraction into a tightly packed payload
  for one region.
- Multiple dirty rectangles require multiple submissions.
- Uploading a full mip chain or array texture requires external loops and
  repeated ownership/layout transitions.
- Readback callers get raw bytes but no helper-level row-pitch or subresource
  metadata.

Recommendation:

- Add multi-region upload/readback descriptors with spans of copy regions and
  source/destination byte spans.
- Add explicit row-pitch and slice-pitch metadata to readback results.
- Make UI dirty-rectangle upload use the same multi-region image API instead
  of a UI-only custom path.

### 3. Format handling is too narrow for general image transfer

Severity: high.

Evidence:

- `UploadReadbackContext::bytesPerPixel(...)` supports a small set of 8-bit,
  16-bit, and 32-bit scalar/vector formats.
- Unsupported formats assert with "unsupported format for readback size
  estimation".
- Default region normalization uses `inferAspectFlags(image.format())`.
  For depth/stencil formats this can produce depth|stencil, while
  `VkBufferImageCopy` requires a single aspect bit.
- Scene texture upload passes raw CPU level bytes to `uploadImage(...)` without
  a format-specific block layout object.

Impact:

- BC/ASTC/ETC compressed textures are not covered.
- Depth-only, stencil-only, combined depth/stencil, packed color formats,
  multi-plane formats, and some HDR formats are not covered.
- Mip tails, block-compressed row sizes, and partial-block edge rules cannot be
  represented by the current `bytesPerPixel` estimate.

Recommendation:

- Replace `bytesPerPixel(...)` with a format transfer descriptor:
  block extent, bytes per block or aspect plane, legal copy aspect, and row
  size calculation.
- Require callers to select a single aspect for depth/stencil image readback.
- Add validation for compressed-format block alignment and edge copies.

### 4. Copy commands use Vulkan 1.3 copy commands 2

Severity: resolved for current helper internals.

Evidence:

- `UploadReadbackContext` uses `copyBuffer2(...)`,
  `copyBufferToImage2(...)`, and `copyImageToBuffer2(...)`.
- Render graph implicit image copy uses `copyImage2(...)`.
- Narrow adapters still accept original `vk::BufferCopy`,
  `vk::BufferImageCopy`, and `vk::ImageCopy` structures where local call sites
  do not need pNext metadata yet.

Impact:

- The current helper internals are pNext-extensible.
- Future copy features, transform metadata, maintenance fields, or tool-facing
  copy annotations can use the copy-2 helpers directly.

Recommendation:

- Keep new code on the copy-2 helpers.
- Keep the original-struct adapters narrow and local to compatibility call
  sites.

### 5. No host image copy route

Severity: medium.

Evidence:

- `Device::vulkan14Capabilities()` exposes `hostImageCopy`.
- No image creation policy exposes `eHostTransfer`.
- No upload/readback helper calls `copyMemoryToImage`, `copyImageToMemory`, or
  host-side image layout transition commands.

Impact:

- The engine always pays the staging-ring memory copy for image upload and
  readback.
- Peak memory during large texture streaming includes image memory plus upload
  ring residency.
- Host-side copies cannot run independently of command-buffer allocation and
  queue submission.

Recommendation:

- Add a separate optional host-image-copy capability snapshot:
  `hostImageCopy`, copy source/destination layout lists, and
  `identicalMemoryTypeRequirements`.
- Add an explicit host-transfer image creation flag or usage intent.
- Select staging vs host image copy by feature support, image usage, format,
  layout, size, and measured platform behavior. Do not make host image copy the
  only path.

### 6. Readback is blocking at consumption and copies again on CPU

Severity: medium.

Evidence:

- `readbackBytes(...)` waits for the timeline value before returning.
- It invalidates the ring range and copies into a new vector.
- `ReadbackTicket` only stores offset, size, and signal value.

Impact:

- Callers that need asynchronous polling or persistent mapped access cannot
  avoid the blocking helper.
- Large screenshots or GPU diagnostics incur an extra CPU copy.
- A caller cannot safely hold a view into the readback ring because ring
  lifetime/reuse is hidden and only vector extraction is exposed.

Recommendation:

- Keep `readbackBytes(...)` as a convenience path.
- Add non-blocking ticket status query and a scoped mapped-read view that pins
  the ring allocation until released.
- Return structured metadata for image readbacks: format, extent, row pitch,
  slice pitch, layers, and mips.

### 7. Readback queue routing is limited

Severity: medium.

Evidence:

- `isReadbackQueueRoleSupported(...)` accepts only graphics and compute.
- Readback copies are submitted on the source resource queue selected by the
  caller.
- The shared readback timeline waits on the previous readback submission for
  every later readback submission.

Impact:

- A transfer-only readback path cannot be used even when a dedicated transfer
  queue would be preferable.
- Graphics and compute readbacks are globally serialized through one timeline.
- Cross-queue readback of resources owned by another queue requires the caller
  to build a higher-level ownership transfer outside the helper.

Recommendation:

- Add an explicit readback route policy: source queue, optional transfer queue,
  and destination host visibility.
- Split timelines or track ring regions independently if cross-queue readbacks
  need concurrency.
- Integrate readback routes with render graph resource ownership transitions.

### 8. Synchronization remains caller-sensitive

Severity: medium.

Evidence:

- Upload ownership is expressed through `BufferUploadOwnershipPlan`.
- Readback requires a caller-provided `ReadbackSyncPlan`.
- Scene and UI code manually submit acquire barriers after upload tickets.
- Render graph copy passes exist, but upload/readback operations are not first-
  class graph passes.

Impact:

- The low-level helper is correct only if each caller picks the right producer
  stage/access and ownership plan.
- Adding new render passes or async readback features can duplicate sync rules.
- Debugging data hazards is harder because upload/readback work sits beside
  the graph rather than inside it.

Recommendation:

- Add renderer/render-graph upload and readback pass descriptors that produce
  `BufferUploadOwnershipPlan` and `ReadbackSyncPlan` from graph resource
  states.
- Keep the low-level RHI helpers, but make most higher-level code use graph
  transactions.

### 9. Scene texture upload is not full texture residency

Severity: medium.

Evidence:

- `uploadTextureAsset(...)` selects the first texture level with pixels.
- GPU texture creation sets `mipLevels = 1` and `arrayLayers = 1`.
- The destination usage is transfer-dst, transfer-src, and sampled.

Impact:

- CPU assets with mip chains, arrays, cube maps, or multiple slices are reduced
  to one level/layer at upload time.
- `uploadImage(...)` limitations are currently masked by a scene policy that
  narrows textures before they reach RHI.

Recommendation:

- Decide the texture residency contract first: full imported mip chain, runtime
  generated mips, or first-level-only.
- If full residency is required, implement multi-subresource image upload in
  RHI before widening scene texture creation.

## Recommended Work Order

### P0: make current staging path reliable for common production assets

1. Add a format transfer descriptor and remove generic `bytesPerPixel(...)`
   from image transfer sizing.
2. Add image upload/readback chunking for payloads larger than the ring.
3. Add multi-region image upload/readback descriptors.
4. Align scene upload budget with RHI ring capacity or expose shared
   configuration.
5. Add tests for large image upload/readback, compressed formats, depth/stencil
   aspect selection, and dirty-rectangle uploads.

### P1: modernize Vulkan copy plumbing

1. Expose copy-region arrays and pNext-capable structures through thin typed
   wrappers.
2. Add an explicit host-transfer image usage policy.
3. Add a measured staging-vs-host-copy policy for target Windows/Linux
   NVIDIA-class hardware.

### P2: make readback usable as a runtime service

1. Add non-blocking ticket status and scoped mapped-read views.
2. Add row/slice pitch metadata for image readback.
3. Add transfer-queue readback routing where ownership transitions justify it.
4. Integrate upload/readback operations into render graph state tracking.

## Current Conclusion

The current RHI upload/readback implementation is a good low-level staging-ring
foundation. Buffer uploads are already chunked and practical. Image
upload/readback is the constrained area: one region, one payload, fixed ring
capacity, narrow format sizing, no host image copy path, and blocking CPU
extraction for readback bytes.

The next useful step is not to replace staging wholesale. First make the
staging path robust for large images, partial updates, mip/layer regions, and
format block rules. Then add an optional Vulkan 1.4 host image copy route gated
by the existing Vulkan 1.4 capability/property snapshots.
