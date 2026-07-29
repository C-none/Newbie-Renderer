# RHI Command Invocation Rules

This strategy applies to `nr.rhi` and project test command-recording code.

## 1. Objective

Define the project command-recording boundary in terms of Vulkan-Hpp RAII member functions.

## 2. Mandatory Rules

- Do not add custom PFN dispatch tables.
- Do not call raw `vkCmd*` entry points directly in project code.
- Use `vk::raii` object member functions for command recording.
- Public command-recording helper APIs should use `const vk::raii::CommandBuffer&` as the first-choice command buffer type.
- If a public RHI entry point is needed, add a thin typed wrapper in existing modules and forward directly to RAII member functions.

## 3. Minimal Implementation Pattern

For each required command:

1. Add a typed outward interface in an existing RHI partition (for example `nr.rhi:command`).
2. Accept `const vk::raii::CommandBuffer&` and typed Vulkan-Hpp parameters.
3. Forward directly to the matching RAII member function.
4. Keep wrapper logic minimal. No hidden state machines, no capability registries, no function-pointer caches.

## 4. Current RAII Command Paths

- Acceleration-structure builds use `vk::raii::CommandBuffer::buildAccelerationStructuresKHR(...)`.
- Ray tracing uses `vk::raii::CommandBuffer::traceRaysKHR(...)`.
- Timeline-semaphore synchronization in `nr.rhi:sync` uses Vulkan-Hpp RAII/C++ methods.

## 5. Verification Checklist

- No `dispatcher->vkCmd*` usage exists in project command paths.
- No `reinterpret_cast<PFN_vk...>` exists in project command paths.
- No `vkGetSemaphoreCounterValue` / `vkWaitSemaphores` C API calls exist in the `src/rhi` sync path.
- Build and run the relevant LLVM Debug tests to confirm behavior equivalence.

## 6. Compile-Time Branch Strategy in `nr.rhi:resourceOps`

Helper variants that differ only by fixed constants use one template entry point with
a non-type template parameter in `nr.rhi:resourceOps`.

Rules:

1. Use `enum class ImageTransitionBranch` to encode transition branch kind.
2. Use `makeImageTransitionBarrier<TBranch>(...)` as the primary implementation path.
3. Resolve destination layout/stage/access through compile-time `if constexpr` selectors.
4. For queue-ownership adapters, select `release` or `acquire` requests through
   `OwnershipBarrierPhase` compile-time dispatch, not duplicated runtime branches.

Example:

```cpp
auto barrier = nr::rhi::ops::makeImageTransitionBarrier<
	nr::rhi::ops::ImageTransitionBranch::TransferDst>(
	image,
	vk::ImageLayout::eUndefined,
	vk::PipelineStageFlagBits2::eTopOfPipe,
	{});
```
