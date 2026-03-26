# RHI Command Invocation Execution Strategy

This strategy applies to `nrrhi` and `test/profile` command recording code.

## 1. Objective

Move command recording to Vulkan-Hpp RAII member functions and remove raw C API command invocation patterns.

## 2. Mandatory Rules

- Do not add custom PFN dispatch tables.
- Do not call raw `vkCmd*` entry points directly in project code.
- Use `vk::raii` object member functions for command recording.
- If a public RHI entry point is needed, add a thin typed wrapper in existing modules and forward directly to RAII member functions.

## 3. Minimal Implementation Pattern

For each required command:

1. Add a typed outward interface in an existing RHI partition (for example `nr.rhi:command`).
2. Accept `const vk::raii::CommandBuffer&` and typed Vulkan-Hpp parameters.
3. Forward directly to the matching RAII member function.
4. Keep wrapper logic minimal. No hidden state machines, no capability registries, no function-pointer caches.

## 4. Current Migration Scope

- `vkCmdBuildAccelerationStructuresKHR` -> `vk::raii::CommandBuffer::buildAccelerationStructuresKHR(...)`
- `vkCmdTraceRaysKHR` -> `vk::raii::CommandBuffer::traceRaysKHR(...)`
- Timeline semaphore sync API in `nr.rhi:sync` migrated from C API calls to RAII/C++ methods.

## 5. Verification Checklist

- No `dispatcher->vkCmd*` usage remains in migrated files.
- No `reinterpret_cast<PFN_vk...>` remains for migrated command paths.
- No `vkGetSemaphoreCounterValue` / `vkWaitSemaphores` C API calls remain in `src/rhi` sync path.
- Build and run target profile tests to confirm behavior equivalence.

## 6. Compile-Time Branch Strategy in `nr.rhi:resourceOps`

To reduce repetitive helper variants that only differ by fixed constants,
`nr.rhi:resourceOps` now follows one template entry point with a non-type template parameter.

Rules:

1. Use `enum class ImageTransitionBranch` to encode transition branch kind.
2. Use `makeImageTransitionBarrier<TBranch>(...)` as the primary implementation path.
3. Resolve destination layout/stage/access through compile-time `if constexpr` selectors.
4. Keep legacy `makeImageTo*` helpers as thin forwarders only when API compatibility is needed.
5. For queue-ownership adapters, select `release` or `acquire` requests through
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
