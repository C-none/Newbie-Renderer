# Newbie-Renderer

## Introduction

Newbie-Renderer is a research-oriented renderer built around C++26 modules, Slang, and Vulkan. The project is intentionally focused on a narrow target stack for now: Windows, Vulkan, and RTX-class NVIDIA hardware. The goal is not broad compatibility first, but a clean experimental platform for advanced rendering algorithms, modern resource management, and future neural rendering workflows.


## Key Features

- **Modular Architecture:** Host-side code is organized with C++26 modules, while shader code is organized around Slang modules and reusable multi-entrypoint shader programs. This keeps compile boundaries explicit and makes the renderer easier to scale without turning the codebase into a header jungle.

- **Shader Reflection:** Slang reflection is already used to inspect shader parameters, validate resource layouts, build descriptor bindings, and keep host/shader contracts synchronized with less manual bookkeeping.

- **Neural Materials(todo):** A long-term goal is to build a neural material system on top of Slang, so learned BRDF/BSDF evaluation, latent material storage, and neural texture representations can live inside the normal shader and resource pipeline instead of as an isolated prototype.

## Todo List

1. [x] RHI layer abstraction around Vulkan with modular RAII-style resource management.
2. [x] Slang compilation and reflection pipeline for reusable shader/module workflows.
3. [x] flecs integration as the scene-layer ECS runtime.
4. [x] Asset import and decode foundation for glTF-oriented content ingestion.
5. [x] Asynchronous GPU physical queue submission and synchronization framework with timeline semaphores and CPU/GPU sync primitives.
6. [ ] Scene-driven BLAS/TLAS build and update flow for ray tracing.
7.  [ ] Light BVH / Many-Light Sampling. Suggested reference: [Dynamic Many-Light Sampling for Real-Time Ray Tracing](https://research.nvidia.com/sites/default/files/pubs/2019-07_Dynamic-Many-Light-Sampling//MPC19.pdf)
8.  [ ] Neural Material System. Suggested reference: [Real-Time Neural Appearance Models](https://research.nvidia.com/labs/rtr/neural_appearance_models/)
9.  [ ] NTC. Suggested reference: [Random-Access Neural Compression of Material Textures](https://research.nvidia.com/labs/rtr/neural_texture_compression/)
10. [ ] Neural Radiance Caching. Suggested reference: [Real-time Neural Radiance Caching for Path Tracing](https://research.nvidia.com/publication/2021-06_real-time-neural-radiance-caching-path-tracing)
11. [ ] Filter After Shading. Suggested reference: [Filtering After Shading with Stochastic Texture Filtering](https://research.nvidia.com/labs/rtr/publication/pharr2024stochtex/)
12. [ ] Sample-Reuse Texture Filtering. Suggested reference: [Collaborative Texture Filtering with ReSTIR and Sample Reuse](https://research.nvidia.com/labs/rtr/publication/akeninemoller2025collaborative/collaborative_texfilt.pdf)
13. [x] DLSS Ray Reconstruction integration. Suggested reference: [DLSS Developer Resources](https://developer.nvidia.com/dlss)
14. [ ] Ray-Cones and Texture LOD. Suggested reference: [Improved Shader and Texture Level of Detail Using Ray Cones](https://research.nvidia.com/publication/2021-04_improved-shader-and-texture-level-detail-using-ray-cones)
15. [ ] ReSTIR PT / GRIS. Suggested reference: [Generalized Resampled Importance Sampling: Foundations of ReSTIR](https://research.nvidia.com/publication/2022-07_generalized-resampled-importance-sampling-foundations-restir)


## Prerequisites

- MSYS2 CLANG64 tools on `PATH` (`clang`, `clang++`, `clang-scan-deps`, `lld`, `llvm-ar`, `llvm-ranlib`, `llvm-objcopy`, `llvm-strip`, `clangd`, `lldb-dap`, `ninja`)
- A compiler toolchain with `import std;` support for C++26
- Vulkan SDK 1.4.341 or newer
- CMake 4.3.3 or newer
- Vcpkg with `VCPKG_ROOT` configured
- Git submodules initialized recursively for Slang, DLSS, and sample assets

Optional MSVC toolchains:

- Visual Studio 18 2026

## Build

### Command Line (CMake Presets)

1. Clone the repository

   ```bash
   git clone --recurse-submodules https://github.com/C-none/Newbie-Renderer.git
   cd Newbie-Renderer
   ```

   For an existing non-recursive checkout, initialize every nested submodule before configuring:

   ```bash
   git submodule update --init --recursive
   ```

2. Configure

   ```bash
   cmake --preset llvm
   ```

3. Build

   ```bash
   # Debug
   cmake --build --preset debug --target main

   # Release
   cmake --build --preset release --target main
   ```

4. Run

   ```bash
   # Debug
   ./build/llvm/Debug/main.exe

   # Release
   ./build/llvm/Release/main.exe
   ```

### MSVC / Visual Studio Generator

Per the CMake 4.3.3 C++ modules manual, `import std` is currently supported only with Ninja generators. The supported build and verification path in this repository is therefore the LLVM/Ninja preset above.

The available MSVC-oriented configure preset is `msvc-vs`, which uses the Visual Studio 18 2026 generator and is kept for IDE-oriented workflows. It is not the supported `import std` verification path under CMake 4.3.3.

```bash
cmake --preset msvc-vs
cmake --build --preset debug-msvc --target main
```

MSVC also owns the project-local NGX bridge build. Debug builds create a local
`nr_dlss_bridge.dll` in the build tree, but Debug artifacts are never publishable.
Only the dedicated MSVC Release preset may refresh the Git-tracked bridge:

```bash
cmake --preset msvc-dlss-bridge
cmake --build --preset publish-dlss-bridge-msvc
```

The publish target updates only
`src/extern/dlssBridge/artifacts/windows-x86_64/release/nr_dlss_bridge.dll`
and its manifest. It never writes generated files into the DLSS submodule.

The normal LLVM clang++/libc++ presets do not require Visual Studio or the MSVC
Redistributable. They validate and deploy that tracked, statically linked Release
bridge, while `nvngx_dlssd.dll` comes from the recursively cloned DLSS submodule.
Both Debug and Release LLVM builds use the same Release bridge/feature-DLL pair.
Configuration fails if the bridge is missing, tampered with, or stale relative to
its C ABI, source, or bundled NGX SDK inputs.

`compile_commands.json` for clangd is generated in the isolated `build/llvm-clangd` tree. The main LLVM build tree intentionally keeps `CMAKE_EXPORT_COMPILE_COMMANDS=OFF`, and `.clangd` points clangd at `build/llvm-clangd`.

The LLVM presets keep vcpkg `buildtrees` and temporary `packages` staging under `build/vcpkg`, while source downloads and compiled binary packages use vcpkg's default global caches. Installed dependencies remain under `build/llvm/vcpkg_installed`.

## App Session

For application-style entry points, prefer `nr::app::AppSession`.
It keeps renderer lifetime, optional scene lifetime, and the application-side interactive camera in one place.
The scene is destroyed before renderer shutdown, so scene-owned GPU resources are released while the Vulkan device and VMA allocator are still alive.
When a scene is available, `AppSession` can seed its camera from the scene primary camera; otherwise it falls back to a default viewer camera.

```cpp
auto app = nr::app::AppSession{};
app.initialize({...});
auto& scene = app.createScene();
auto& renderer = app.renderer();
app.resetCameraFromSceneOrDefault();
...
app.shutdown();
```

## Packages

### Git Submodules and Vendored SDKs

| Name | Current State | Purpose |
| --- | --- | --- |
| Slang | `v2026.10.2` | Shader language, compilation, reflection, SPIR-V generation |
| glTF-Sample-Assets | `2bac6f8c` | Sample assets for import, testing, and regression cases |
| NVIDIA DLSS SDK | `310.7.0` | NGX headers, static loader input for MSVC bridge publication, and the Release Ray Reconstruction feature DLL |
| Nsight Aftermath SDK `R590` | bundled under `src/extern/Aftermath` | Future GPU crash diagnostics and shader crash analysis |
| Nsight Graphics SDK `0.9.0` | bundled under `src/extern/NsightGraphics/0.9.0` | Env-driven Graphics Capture, GPU Trace, and SDK frame boundaries |

Slang update cmd:

```bash
cd src/extern/slang
git fetch --tags
git checkout v202*.*.*
git submodule update --init --recursive
```

### Vcpkg Manifest Packages

| Package | Purpose |
| --- | --- |
| `glm` | Math types and transforms |
| `imgui` | Debug UI and tooling overlays |
| `glfw3` | Window creation and Vulkan surface bootstrap |
| `vulkan-memory-allocator` | Vulkan memory allocation |
| `tracy` | Profiling hooks and runtime instrumentation support |
| `assimp` | Model and scene import |
| `stb` | Generic image decode fallback |
| `libjpeg-turbo` | Fast JPEG decode path |
| `flecs` | ECS runtime used by the scene layer |

### Notes

- Third-party C/C++ headers are surfaced to engine code through narrow `dependency.*` C++ modules under `src/extern`; `src/extern/exportDependency.ixx` remains a compatibility umbrella. Internal project sources should import the narrow module they need instead of adding raw third-party includes.
- The Slang git submodule is kept under `src/extern/slang`, configured as a local build-tree CMake package, and consumed by the engine through `find_package(slang)` / `slang::slang`.
- Additional transitive dependencies used by Slang, SPIR-V tooling, and related build scripts are resolved through the Slang submodule itself.
- Vcpkg package versions are controlled by the active manifest/toolchain resolution in your local environment.
- If you want to update submodules, use normal Git workflows. Nsight Aftermath is the exception and should be updated manually from the NVIDIA Developer website when needed.
