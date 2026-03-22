# Newbie-Renderer

## Introduction

Newbie-Renderer is a research-oriented renderer built around C++23 modules, Slang, and Vulkan. The project is intentionally focused on a narrow target stack for now: Windows, Vulkan, and RTX-class NVIDIA hardware. The goal is not broad compatibility first, but a clean experimental platform for advanced rendering algorithms, modern resource management, and future neural rendering workflows.


## Key Features

- **Modular Architecture:** Host-side code is organized with C++23 modules, while shader code is organized around Slang modules and reusable multi-entrypoint shader programs. This keeps compile boundaries explicit and makes the renderer easier to scale without turning the codebase into a header jungle.

- **Shader Reflection:** Slang reflection is already used to inspect shader parameters, validate resource layouts, build descriptor bindings, and keep host/shader contracts synchronized with less manual bookkeeping.

- **Neural Materials(todo):** A long-term goal is to build a neural material system on top of Slang, so learned BRDF/BSDF evaluation, latent material storage, and neural texture representations can live inside the normal shader and resource pipeline instead of as an isolated prototype.

## Todo List

1. [x] RHI layer abstraction around Vulkan with modular RAII-style resource management.
2. [x] Slang compilation and reflection pipeline for reusable shader/module workflows.
3. [x] flecs integration as the scene-layer ECS runtime.
4. [x] Asset import and decode foundation for glTF-oriented content ingestion.
5. [ ] GPU scene upload and residency management for meshes, materials, textures, and instance data.
6. [ ] Scene-driven BLAS/TLAS build and update flow for ray tracing.
7.  [ ] Light BVH / Many-Light Sampling. Suggested reference: [Dynamic Many-Light Sampling for Real-Time Ray Tracing](https://research.nvidia.com/sites/default/files/pubs/2019-07_Dynamic-Many-Light-Sampling//MPC19.pdf)
8.  [ ] Neural Material System. Suggested reference: [Real-Time Neural Appearance Models](https://research.nvidia.com/labs/rtr/neural_appearance_models/)
9.  [ ] NTC. Suggested reference: [Random-Access Neural Compression of Material Textures](https://research.nvidia.com/labs/rtr/neural_texture_compression/)
10. [ ] Neural Radiance Caching. Suggested reference: [Real-time Neural Radiance Caching for Path Tracing](https://research.nvidia.com/publication/2021-06_real-time-neural-radiance-caching-path-tracing)
11. [ ] Filter After Shading. Suggested reference: [Filtering After Shading with Stochastic Texture Filtering](https://research.nvidia.com/labs/rtr/publication/pharr2024stochtex/)
12. [ ] Sample-Reuse Texture Filtering. Suggested reference: [Collaborative Texture Filtering with ReSTIR and Sample Reuse](https://research.nvidia.com/labs/rtr/publication/akeninemoller2025collaborative/collaborative_texfilt.pdf)
13. [ ] DLSS Integration. Suggested reference: [DLSS Developer Resources](https://developer.nvidia.com/dlss)
14. [ ] Ray-Cones and Texture LOD. Suggested reference: [Improved Shader and Texture Level of Detail Using Ray Cones](https://research.nvidia.com/publication/2021-04_improved-shader-and-texture-level-detail-using-ray-cones)
15. [ ] ReSTIR PT / GRIS. Suggested reference: [Generalized Resampled Importance Sampling: Foundations of ReSTIR](https://research.nvidia.com/publication/2022-07_generalized-resampled-importance-sampling-foundations-restir)


## Prerequisites

- Visual Studio 18 2026
- Vulkan SDK 1.4.341 or newer
- CMake 4.2 or newer
- Vcpkg with `VCPKG_ROOT` configured
- Git submodules initialized with `--recurse-submodules`

## Build

### Command Line (CMake Presets)

1. Clone the repository

   ```bash
   git clone https://github.com/C-none/Newbie-Renderer.git --recurse-submodules
   cd Newbie-Renderer
   ```

2. Configure

   ```bash
   cmake --preset msvc
   ```

3. Build

   ```bash
   # Release
   cmake --build --preset release --target main

   # Debug
   cmake --build --preset debug --target main
   ```

4. Run

   ```bash
   # Release
   cmake --build --preset run-release

   # Debug
   cmake --build --preset run-debug
   ```

## Packages

### Git Submodules and Vendored SDKs

| Name | Current State | Purpose |
| --- | --- | --- |
| Slang | `v2026.4.2` | Shader language, compilation, reflection, SPIR-V generation |
| flecs | `v4.1.5` | ECS runtime used by the scene layer |
| glTF-Sample-Assets | `c147d2fc` | Sample assets for import, testing, and regression cases |
| Nsight Aftermath SDK | `R590` | bundled under `src/extern/Aftermath` | Future GPU crash diagnostics and shader crash analysis |

### Vcpkg Manifest Packages

| Package | Purpose |
| --- | --- |
| `glm` | Math types and transforms |
| `imgui` | Debug UI and tooling overlays |
| `glfw3` | Window creation and Vulkan surface bootstrap |
| `vulkan-memory-allocator` | Vulkan memory allocation |
| `tracy` | Profiling hooks and future runtime instrumentation |
| `assimp` | Model and scene import |
| `stb` | Generic image decode fallback |
| `libjpeg-turbo` | Fast JPEG decode path |

### Notes

- Additional transitive dependencies used by Slang, SPIR-V tooling, and related build scripts are resolved through the Slang submodule itself.
- Vcpkg package versions are controlled by the active manifest/toolchain resolution in your local environment.
- If you want to update submodules, use normal Git workflows. Nsight Aftermath is the exception and should be updated manually from the NVIDIA Developer website when needed.
