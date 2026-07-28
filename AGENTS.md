# Newbie-Renderer AGENTS & Development Guidelines

This document outlines the development standards, architectural principles, and agent behaviors for the **Newbie-Renderer** project. All contributors and AI agents must adhere strictly to these guidelines.

## 1. Project Overview

**Newbie-Renderer** is a modern rendering engine built with C++26 modules and Vulkan, utilizing Slang as the shading language. The architecture emphasizes modularity, type safety, and modern C++ practices.

## 2. Core Constraints

### 2.1 Language Standards

*   **C++ Version:** strictly **C++26**.
*   **Shader Language:** strictly **Slang** compiling to **SPIR-V**. HLSL/GLSL are not to be used directly unless wrapped or transpiled via Slang.

### 2.2 Coding Style & Modern C++

*   **Modernization:** Prioritize modern C++ features over legacy practices.
*   **Pragmatic Design:** While fully leveraging modern C++ features, avoid redundant design and over-abstraction.
*   **Loops vs. Algorithms:** Replace raw `for` loops with **C++20+ Ranges and Views** (pipes) whenever possible.
    *   *Preferred:* `data | std::views::transform(...) | ...`
    *   *Avoid:* `for (int i = 0; i < n; ++i) { ... }` unless performance critical and unrolling is necessary or the range alternative is significantly more complex.
*   **Compile-Time Variant Policy:** For call paths that only differ by a small constant parameter (for example `LogLevel`, resource kind labels, or policy tags), prefer a single template with a non-type template parameter and `if constexpr` over multiple near-identical wrapper functions.
    *   *Preferred:* `template<LogLevel Level> void report(...);`
    *   *Avoid:* `reportInfo(...)`, `reportWarning(...)`, `reportError(...)`-style wrapper triplets when behavior differs only by compile-time constants.
*   **Associative Container Default:** When choosing an STL dictionary or tree container, default to `std::map` / `std::set`.
    *   *Use `std::unordered_map` / `std::unordered_set` only when the expected container size is greater than 50 in most cases.*
    *   *If the size is usually 50 or fewer elements, prefer `std::map` / `std::set` by default unless there is a clear, measured reason to do otherwise.*
*   **Default-Member Construction:** When an aggregate or object member already has a default member initializer, omit that member during construction if the constructed value would be identical to the default. Assign only members whose value changes from the default, unless an explicit assignment is needed to document an API boundary, disambiguate overloads, or intentionally reset an existing object after construction.
*   **Null-Equivalent Construction:** For optional or pointer-like objects that default to an empty/null state (for example `std::optional`, nullable external API pointers, Vulkan-Hpp optional handles, or RAII handle wrappers), do not explicitly assign `std::nullopt`, `nullptr`, or an equivalent empty value during aggregate/object construction when the value would remain unchanged. Use `{}` default member initialization for stored optional-like fields that need an explicit empty default. Use explicit empty assignments only when clearing an existing object, defining an API default argument, disambiguating overloads, or passing a required null at an external API boundary.
*   **Modules:** Use C++20 modules (`.ixx`) for internal code organization.
*   **Large Partition Split Policy:** When a module partition grows beyond a small declaration-only surface, split it into a module interface partition (`.ixx`) and a matching module implementation unit (`.cpp`). Keep exported types, declarations, templates, `constexpr`/`consteval` logic, default arguments, and necessary tiny inline helpers in the `.ixx`; move non-template, non-`constexpr`, non-essential runtime implementation into the `.cpp`. Small declaration-only/type-only partitions may remain as a single `.ixx`.

### 2.3 Resource Management (RAII)

*   **RAII Principle:** The `nr.rhi` (Newbie-Renderer RHI) module must strictly adhere to **RAII (Resource Acquisition Is Initialization)** principles.
*   **Ownership:** Resources (buffers, images, pipelines) should be owned by objects that manage their lifecycle.
*   **Cleanup:** Destructors must ensure proper release of Vulkan/API resources. Avoid manual `init()`/`destroy()` pairs where a constructor/destructor pair can suffice.

### 2.4 Dependency Management

*   **Third-Party Libraries:** All third-party non-module libraries (legacy headers/libs) must be encapsulated at the `src/extern` boundary and introduced through narrow **`dependency.*`** modules (for example `dependency.vulkan`, `dependency.vma`, `dependency.window`, `dependency.math`, `dependency.ui`, `dependency.assets`, `dependency.slang`, `dependency.ecs`, and `dependency.nsight`). The top-level **`dependency`** module is a compatibility umbrella that may re-export those narrow modules.
*   **Third-Party Tool Preference:** When a mature third-party library or tool already provides the needed domain behavior (for example parsing, transcoding, image processing, asset conversion, shader tooling, or specialized math), add or reuse that dependency through the `src/extern` and `dependency.*` boundary instead of hand-writing a project-local implementation, unless there is a clear measured reason to own a specialized implementation.
*   **Isolation:** Do not include raw third-party headers directly in core logic modules; import the adapted `dependency.*` module interfaces instead.
*   **JSON Library Policy:** All project-owned JSON parsing, serialization, dynamic generation, and structural editing must use a library-backed JSON API. C++ code must import the narrow `dependency.json` adapter backed by Boost.JSON; CMake code must use `string(JSON)`; Python and other host-language tooling must use their standard or an established JSON library. Do not hand-write JSON escaping, concatenate dynamic JSON structure, or interpret JSON through textual substring/regular-expression searches. Exact static JSON fixtures and intentionally malformed parser-test inputs may remain raw literals, but any fixture containing dynamic values must be produced by a JSON library.
*   **Vendored Source Scan Policy:** Treat `src/extern` as third-party vendored source and skip it during routine repository discovery, broad text search, and codebase orientation. Inspect it only when the task explicitly concerns dependency integration, vendored source patches, external build wiring, or a concrete diagnostic that originates inside `src/extern`.

### 2.5 Platform & Hardware Targeting (RHI Scope)

*   **OS Scope:** RHI design and implementation only target **Windows**.
*   **Graphics API Scope:** RHI only targets **Vulkan**.
*   **GPU Scope:** RHI only targets high-end **NVIDIA GeForce RTX 5070 Ti** class hardware.
*   **Extension Strategy:** For future RHI optimization, required Vulkan extensions/features may be assumed supported on the target platform/hardware.
*   **Fallback Policy:** Do **not** add fallback paths for unsupported extensions, unsupported vendors, or non-target hardware in the RHI layer.
*   **Failure Mode:** If a required capability is unexpectedly unavailable at runtime, fail fast with clear diagnostics instead of adding compatibility fallbacks.

### 2.6 Vulkan Command Invocation Policy

*   **No Dispatch Tables in Project Code:** Do **not** introduce custom PFN dispatch tables or per-command function-pointer caches in `nr.rhi` and project test code.
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

### 2.8 Error Reporting Policy

*   **Single Error Facility:** Use `nr.utils:errorHandle` as the only in-project error reporting entrypoint.
*   **No Module-Local Diagnostic Systems:** Do **not** introduce per-module custom diagnostics buffers/entry structs for production error reporting paths.
*   **Scene Rule:** Scene import/bridge/runtime errors and warnings must be emitted through `nrLog`/`nrInfo`/`nrVulkan`/`nrAssert` from `errorHandle`.
*   **Extensibility Rule:** If a module needs additional reporting behavior, extend `errorHandle` first, then reuse it everywhere instead of adding a new ad-hoc reporting API.

### 2.9 Exception Handling Policy

*   **No try/catch for In-Project Error Paths:** Project code must **not** use `try`/`catch` as a general error-handling mechanism. Errors must be detected, reported in-place via `nrInfo`/`nrAssert`, and handled locally (early return, sentinel value, or `std::exit`).
*   **External Library Boundary Rule:** A small, focused `try`/`catch` is permitted **only** at the immediate call site of an external library API that is documented or known to throw non-`std::exception` exceptions (e.g., `Slang::InternalError`). The catch block must:
    1.  Emit the error in-place via `nrInfo<LogLevel::error>` with enough context to identify the call site and the failing operation.
    2.  Either terminate (`nrAssert(false, ...)`) or return a clearly invalid sentinel value — do **not** silently swallow or re-throw.
*   **No Propagation Wrappers:** Do **not** wrap entire functions in `try`/`catch` just to translate or re-throw. Catching, logging, and immediately re-throwing (`throw;`) is only acceptable if the sole purpose is diagnostic logging and the exception will be caught and terminated at the nearest external boundary.
*   **`noexcept` on Internal Helpers:** Internal helper functions that call only project code (no external library calls) should be marked `noexcept` where correct. Removing `noexcept` to accommodate unhandled external exceptions is not acceptable — handle the exception at the external call site instead.

### 2.10 RenderPass Resource Binding Policy

*   **RenderPasses Binding:** In `renderPasses`, except for vertex/index buffers and pipeline-fixed resources, all bindable resources (descriptor-set describable resources and push constants) must be driven by `nrslang` reflection and the binding relations recorded via `shaderCursor`.
*   **Binding Location:** In RDG parallel execution, descriptor-backed resources must be updated in the prepare stage and only bound/pushed in the record stage. Render-pass nodes should normally use renderer-side `RasterPassBuilder` / `ComputePassBuilder`, which own the calls to `updateResourcesForBindingSnapshot(...)`, `bindPreparedResourcesToCommandBuffer(...)`, and `pushConstantsToCommandBuffer(...)` (see `src/rhi/nrPipeline.ixx`). Any lower-level shader-visible `addPass` path must preserve the same prepare/record split.
*   **Avoid Manual Vulkan Binding:** Do not manually construct/update/bind Vulkan descriptor sets or push constants from `renderPasses` code outside of the `shaderCursor`-based pathway.

### 2.11 GPU Upload Policy

*   **Direct CPU Writes:** Direct `Buffer::writeMappedAndFlush(...)` writes are allowed only for resources intentionally allocated as CPU-writable GPU-visible memory, such as per-frame uniforms or dynamic vertex/index buffers that are directly consumed by the GPU.
*   **Unified Staging Ring:** If the upload target is not directly CPU-writable GPU-visible memory, route CPU data through the device-level `nr::rhi::ops::UploadReadbackContext` upload ring (`uploadBuffer(...)` / `uploadImage(...)`) instead of creating node-local or module-local staging buffers.
*   **Queue Ownership:** Uploads that use the staging ring must preserve the transfer-to-destination queue ownership handoff. The destination queue acquire barrier must complete before the resource is exposed as resident or imported for shader-visible graph use.
*   **RenderPass Rule:** `renderPasses` code must not add persistent per-node `TransferSrc` staging buffers for GPU-only resources. Use the RHI upload ring for those uploads, while keeping truly direct CPU-visible frame resources as direct mapped writes.

## 3. Module Structure

*   **`nr.rhi`:** The Render Hardware Interface. Implements the abstraction over Vulkan.
*   **`src/extern`:** Contains wrappers and build logic for external dependencies (e.g., Slang, NVAPI, Aftermath).

## 4. Specific Agent Instructions

When generating code or refactoring:
1.  **Check Context:** Verify if the file is a module interface (`.ixx`) or implementation (`.cpp`).
2.  **Apply Constraints:** Ensure no raw loops are introduced if a range-based solution exists.
3.  **Safety:** Verify RAII compliance in `nr.rhi` classes.
4.  **Language:** All code comments must be in English.
5.  **Ownership Semantics:** Enforce Section 2.7 and avoid introducing raw-pointer ownership patterns.

### 4.1 Closed-Loop Verification Build Rules

*   **Default Build Preset:** Closed-loop build and test verification must use the LLVM Debug configuration (`cmake --build --preset debug`, `ctest --preset debug`, or an equivalent LLVM Debug target build).
*   **No Release Substitute:** Do not use Release builds for verification unless the user explicitly requests Release validation.
*   **Blocked Debug Verification:** If LLVM Debug verification is blocked by environment, toolchain, or file-lock issues, report that blocker and the affected command instead of switching to Release as a substitute.
*   **Configure/Build Wait Policy:** After launching configure or build commands, including CMake configure and `cmake --build` tasks, do not repeatedly poll process lists, lock files, or build directories while the command is still running. Wait for the command to return and report the final result; inspect locks or processes only after the command has completed, failed, or timed out.

### 4.2 Architecture Context Maintenance Rules

When code changes affect architecture-facing behavior, agents must keep the architecture context documents synchronized, especially `docs/architecture/README.md`.

Update the context documents in the same change whenever one or more of the following is true:

1.  A core module's responsibility changes.
2.  The primary data flow between `rhi`, `load`, `resource`, `scene`, `renderer`, and `renderPasses` changes.
3.  A stable runtime boundary changes, such as:
    - scene -> renderer bridge shape
    - renderer -> renderPasses node lifecycle contract
    - resource ownership/lifetime boundaries
    - load -> scene bridge responsibilities
4.  A major dependency framework is added, removed, or moved to a different layer.
5.  A topic document linked from `docs/architecture/README.md` becomes outdated because of the code change.

Agents must follow these rules to preserve consistency:

1.  **Update docs in the same patch set.** Do not leave architecture document updates as follow-up work when the code change already changes the architecture story.
2.  **Keep the global context high-level.** `docs/architecture/README.md` must stay concise and architectural. Do not copy detailed enums, record layouts, or low-level implementation logic into it unless that detail is required to explain module boundaries.
3.  **Push details down to source-of-truth files.** Put detailed contracts in the relevant code files or topic documents, then link to them from the global context.
4.  **Preserve section order.** Keep the architecture overview ordered as:
    - `rhi`
    - `load`
    - `resources`
    - `scene`
    - `renderer`
    - `renderpasses`
    - `Overall`
5.  **Describe reality, not intention.** The context document must reflect the current codebase and accepted project direction, not abandoned plans or speculative future designs.
6.  **Update only the affected scope, but verify neighbors.** If one layer changes, update its section and re-check adjacent sections so the end-to-end flow still reads correctly.
7.  **Maintain terminology consistency.** Use the same module names and runtime terms that appear in code and topic documents.
8.  **Do not duplicate coding-policy content.** If a rule belongs in `AGENTS.md`, keep it here and reference it from architecture docs instead of duplicating it there.
9.  **Prefer narrower claims when unsure.** If the code does not fully support a broader architectural statement, document the smaller true statement and link the reader to the implementation file.

Minimum verification after an architecture-relevant change:

1.  Check that `docs/architecture/README.md` still matches the current main flow.
2.  Check that every newly introduced or changed stable boundary has at least one concrete code reference.
3.  Check that linked topic documents still describe the same design direction as the code.
