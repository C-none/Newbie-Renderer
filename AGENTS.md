# Newbie-Renderer AGENTS & Development Guidelines

This document outlines the development standards, architectural principles, and agent behaviors for the **Newbie-Renderer** project. All contributors and AI agents must adhere strictly to these guidelines.

## 1. Project Overview

**Newbie-Renderer** is a modern rendering engine built with C++23 modules and Vulkan, utilizing Slang as the shading language. The architecture emphasizes modularity, type safety, and modern C++ practices.

## 2. Core Constraints

### 2.1 Language Standards

*   **C++ Version:** strictly **C++23**.
*   **Shader Language:** strictly **Slang** compiling to **SPIR-V**. HLSL/GLSL are not to be used directly unless wrapped or transpiled via Slang.

### 2.2 Coding Style & Modern C++

*   **Modernization:** Prioritize modern C++ features over legacy practices.
*   **Pragmatic Design:** While fully leveraging modern C++ features, avoid redundant design and over-abstraction.
*   **Loops vs. Algorithms:** Replace raw `for` loops with **C++20/23 Ranges and Views** (pipes) whenever possible.
    *   *Preferred:* `data | std::views::transform(...) | ...`
    *   *Avoid:* `for (int i = 0; i < n; ++i) { ... }` unless performance critical and unrolling is necessary or the range alternative is significantly more complex.
*   **Modules:** Use C++20 modules (`.ixx`) for internal code organization.

### 2.3 Resource Management (RAII)

*   **RAII Principle:** The `nrrhi` (Newbie-Renderer RHI) module must strictly adhere to **RAII (Resource Acquisition Is Initialization)** principles.
*   **Ownership:** Resources (buffers, images, pipelines) should be owned by objects that manage their lifecycle.
*   **Cleanup:** Destructors must ensure proper release of Vulkan/API resources. Avoid manual `init()`/`destroy()` pairs where a constructor/destructor pair can suffice.

### 2.4 Dependency Management

*   **Third-Party Libraries:** All third-party non-module libraries (legacy headers/libs) must be encapsulated and introduced through the **`dependency`** module (e.g., `src/extern` or equivalent wrapper modules).
*   **Isolation:** Do not include raw third-party headers directly in core logic modules; use the adapted module interfaces.

### 2.5 Platform & Hardware Targeting (RHI Scope)

*   **OS Scope:** RHI design and implementation only target **Windows**.
*   **Graphics API Scope:** RHI only targets **Vulkan**.
*   **GPU Scope:** RHI only targets high-end **NVIDIA GeForce RTX 5070 Ti** class hardware.
*   **Extension Strategy:** For future RHI optimization, required Vulkan extensions/features may be assumed supported on the target platform/hardware.
*   **Fallback Policy:** Do **not** add fallback paths for unsupported extensions, unsupported vendors, or non-target hardware in the RHI layer.
*   **Failure Mode:** If a required capability is unexpectedly unavailable at runtime, fail fast with clear diagnostics instead of adding compatibility fallbacks.

### 2.6 Vulkan Command Invocation Policy

*   **No Dispatch Tables in Project Code:** Do **not** introduce custom PFN dispatch tables or per-command function-pointer caches in `nrrhi` and test/profile code.
*   **No Raw C API Command Calls:** Prefer Vulkan-Hpp RAII object member functions over raw `vkCmd*` / `vk*` C API entry points.
    *   *Preferred:* `vk::raii::CommandBuffer::buildAccelerationStructuresKHR(...)`, `vk::raii::CommandBuffer::traceRaysKHR(...)`.
    *   *Avoid:* manual `getProcAddr`, `dispatcher->vkCmd*`, `reinterpret_cast<PFN_vk...>`, or raw-handle `vkCmd*` calls.
*   **Thin RHI Interfaces Only:** If an external API is needed, expose a small typed wrapper in existing RHI modules that directly forwards to RAII member functions without extra indirection layers.

### 2.7 Modern C++ Ownership & Reference Policy

*   **No Raw Owning Pointers:** Do not use raw pointers to model ownership. Avoid `new`/`delete` in project code except in tightly scoped low-level wrappers that immediately hand off to RAII types.
*   **Required Non-Owning Dependency:** If an object must exist and cannot be null, use `T&` / `const T&`.
*   **Rebindable Non-Owning Handle:** If a non-owning reference must be stored and reassigned, use `std::reference_wrapper<T>` / `std::reference_wrapper<const T>`.
*   **Optional Non-Owning Dependency:** If a non-owning dependency may be absent, use `std::optional<std::reference_wrapper<T>>`.
*   **Observed Shared Lifetime:** If ownership is shared elsewhere and only observation is needed, use `std::weak_ptr<T>`, and always `lock()` before dereference.
*   **Smart Pointer Ownership Rules:** Use `std::unique_ptr<T>` for exclusive ownership. Use `std::shared_ptr<T>` only when shared ownership is required by explicit design.
*   **Public API Rule:** Do not expose nullable raw pointers in module interfaces for ownership or lifetime control. Prefer references, `weak_ptr`, `reference_wrapper`, or `optional<reference_wrapper>` depending on semantics.
*   **Boundary Exception:** Raw pointers are allowed only at external C API boundaries (for example Vulkan/C libraries). Convert immediately to safe local abstractions and do not propagate raw-pointer ownership semantics into internal module APIs.

## 3. Module Structure

*   **`nrrhi`:** The Render Hardware Interface. Implements the abstraction over Vulkan.
*   **`src/extern`:** Contains wrappers and build logic for external dependencies (e.g., Slang, NVAPI, Aftermath).

## 4. Specific Agent Instructions

When generating code or refactoring:
1.  **Check Context:** Verify if the file is a module interface (`.ixx`) or implementation (`.cpp`).
2.  **Apply Constraints:** Ensure no raw loops are introduced if a range-based solution exists.
3.  **Safety:** Verify RAII compliance in `nrrhi` classes.
4.  **Language:** All code comments must be in English.
5.  **Ownership Semantics:** Enforce Section 2.7 and avoid introducing raw-pointer ownership patterns.
