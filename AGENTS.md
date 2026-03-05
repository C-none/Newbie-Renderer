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

## 3. Module Structure

*   **`nrrhi`:** The Render Hardware Interface. Implements the abstraction over Vulkan.
*   **`src/extern`:** Contains wrappers and build logic for external dependencies (e.g., Slang, NVAPI, Aftermath).

## 4. Specific Agent Instructions

When generating code or refactoring:
1.  **Check Context:** Verify if the file is a module interface (`.ixx`) or implementation (`.cpp`).
2.  **Apply Constraints:** Ensure no raw loops are introduced if a range-based solution exists.
3.  **Safety:** Verify RAII compliance in `nrrhi` classes.
