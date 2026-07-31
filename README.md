# Newbie-Renderer

## Introduction

Newbie-Renderer is a research-oriented renderer built around C++26 modules, Slang, and Vulkan. The project is intentionally focused on a narrow target stack for now: Windows, Vulkan, and RTX-class NVIDIA hardware. The goal is not broad compatibility first, but a clean experimental platform for advanced rendering algorithms, modern resource management, and future neural rendering workflows.


## Key Features

- **Modular Architecture:** Host-side code is organized with C++26 modules, while shader code is organized around Slang modules and reusable multi-entrypoint shader programs. This keeps compile boundaries explicit and makes the renderer easier to scale without turning the codebase into a header jungle.

- **Shader Reflection:** Slang reflection is already used to inspect shader parameters, validate resource layouts, build descriptor bindings, and keep host/shader contracts synchronized with less manual bookkeeping.

- **Scene-Driven Ray Tracing:** Scene data drives cached BLAS rebuilds, per-frame TLAS construction, and the RT metadata and SBT plan consumed by ray-tracing passes.

- **Path Tracing + DLSS Ray Reconstruction:** Path tracing produces a fixed 1spp seven-resource RR input set. DLSS quality selects render resolution; DLAA renders at display resolution and is the only mode that permits output bypass.

- **Filter After Shading (FAS):** RT path-traced materials expose an off-by-default A/B graph option backed by ABI-stable Slang root variants. The path tracer reserves a fixed three-`rand4` schedule for eleven material texture semantics regardless of the selected policy or optional material layers; when enabled, FAS selects one bilinear reconstruction tap and performs one nearest LOD0 fetch per lookup. Changing the option resets temporal history, while alpha-mask any-hit remains deterministic. The implemented filtering path is intentionally mipless and does not use derivatives or ray cones.

- **Async Copy and Async Compute:** The multi-queue RDG implements asynchronous copy work on the transfer queue and asynchronous compute work on the compute queue, synchronized with graphics through timeline semaphores and queue-ownership transitions.

- **UI and HDR Presentation:** ImGui tooling integrates with SDR, HDR10, and scRGB presentation conversion, selectable tone mapping, screenshots, and readback.

## Todo List

1. [x] RHI layer abstraction around Vulkan with modular RAII-style resource management.
2. [x] Slang compilation and reflection pipeline for reusable shader/module workflows.
3. [x] flecs integration as the scene-layer ECS runtime.
4. [x] Asset import and decode foundation for glTF-oriented content ingestion.
5. [x] Multi-queue RDG execution with Async Copy and Async Compute.
6. [x] Scene-driven cached BLAS rebuilds with per-frame TLAS construction and RT metadata.
7.  [x] Alias-table many-light sampling for active punctual lights. Suggested reference: [Dynamic Many-Light Sampling for Real-Time Ray Tracing](https://research.nvidia.com/sites/default/files/pubs/2019-07_Dynamic-Many-Light-Sampling//MPC19.pdf)
8.  [ ] Light BVH.
9.  [ ] Neural Material System. Suggested reference: [Real-Time Neural Appearance Models](https://research.nvidia.com/labs/rtr/neural_appearance_models/)
10. [ ] NTC. Suggested reference: [Random-Access Neural Compression of Material Textures](https://research.nvidia.com/labs/rtr/neural_texture_compression/)
11. [ ] Neural Radiance Caching. Suggested reference: [Real-time Neural Radiance Caching for Path Tracing](https://research.nvidia.com/publication/2021-06_real-time-neural-radiance-caching-path-tracing)
12. [x] RT Filter After Shading (FAS) first stage: an ABI-stable root A/B variant, fixed random-dimension mapping for eleven material texture semantics, and one-sample stochastic bilinear reconstruction at LOD0. Suggested reference: [Filtering After Shading with Stochastic Texture Filtering](https://research.nvidia.com/labs/rtr/publication/pharr2024stochtex/)
13. [x] DLSS Ray Reconstruction with quality-driven render resolution and DLAA-only output bypass. Suggested reference: [DLSS Developer Resources](https://developer.nvidia.com/dlss)
14. [ ] ReSTIR PT / GRIS. Suggested reference: [Generalized Resampled Importance Sampling: Foundations of ReSTIR](https://research.nvidia.com/publication/2022-07_generalized-resampled-importance-sampling-foundations-restir)
15. [ ] NeuSample / neural material importance sampling. Suggested reference: [NeuSample: Importance Sampling for Neural Materials](https://cseweb.ucsd.edu/~viscomp/projects/neusample/)
16. [ ] Neural shading optimization stability. Suggested reference: [Taming Optimization Variance in Compact Neural Shading Networks](https://research.nvidia.com/labs/rtr/publication/bitterli2026taming/)
17. [ ] Comprehensive neural materials. Suggested reference: [Towards Comprehensive Neural Materials: Dynamic Structure-Preserving Synthesis with Accurate Silhouette at Instant Inference Speed](https://dl.acm.org/doi/full/10.1145/3721238.3730626)

## Prerequisites

- MSYS2 CLANG64 tools on `PATH` (`clang`, `clang++`, `clang-scan-deps`, `lld`, `llvm-ar`, `llvm-ranlib`, `llvm-objcopy`, `llvm-strip`, `clangd`, `lldb-dap`, `ninja`)
- A compiler toolchain with `import std;` support for C++26
- Vulkan SDK 1.4.341 or newer
- CMake 4.4 or newer
- Vcpkg with `VCPKG_ROOT` configured
- Git submodules initialized recursively for Slang, DLSS, sample assets, and Ninja tracing

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
   ./build/llvm/src/Debug/main.exe

   # Release
   ./build/llvm/src/Release/main.exe
   ```

### Single Shader Compile Check

After configuring the LLVM build, compile one shader through the same `ShaderService`
session, link, reflection, and per-entry-point SPIR-V generation path used by the
renderer:

```bash
cmake "-DNR_SHADER_FILE=renderer/embeddedTriangle.slang" -P tools/CheckSlangShader.cmake
```

`NR_SHADER_FILE` may be absolute or relative to the configured `NR_SHADER_ROOT_DIR`;
the `.slang` extension may be omitted.
The script incrementally builds the excluded `nr_shader_compile_check` helper with
LLVM Debug by default. Use `NR_SHADER_CHECK_BUILD_DIR` and
`NR_SHADER_CHECK_CONFIG` to select another configured build tree or configuration.
Shaders with required link-time assignments are checked with an empty variant and
therefore fail until a variant-aware checker invocation is added explicitly.

### Runtime Logs

The viewer owns one rotating NDJSON log session under `build/app/logs`:

- `engine.ndjson` contains `nr.utils:errorHandle` diagnostics. Informational records are
  file-only; warnings, errors, and assertions are also shown in the command window.
- `options.ndjson` contains every `nrCompactRecord`, including `NR_OPTION_V1` final
  outcomes and `NR_OPTION_ENDPOINT_V1` endpoint discovery.

Each active file rotates at 32 MiB and retains four older segments named
`engine.1.ndjson` through `engine.4.ndjson` or `options.1.ndjson` through
`options.4.ndjson`. Every new segment starts with an `NR_LOG_SESSION_V1` record that
identifies the viewer session and stream.

Agents and humans can read or tail the active files without capturing the process'
stdout/stderr. A tailer must detect replacement of the active file, reopen it, and scan
the new segment from its session marker after rotation. A Windows kernel-backed lease
exclusively owns the canonical log directory, while `.active-viewer` exposes that
ownership to humans and tools. A second current viewer fails before touching the fixed
paths. If a crash or reboot leaves an empty `.active-viewer` marker after the kernel
lease has gone away, the next viewer removes that stale marker and starts automatically;
non-empty or otherwise unexpected markers still fail closed for operator inspection.

For a quick PowerShell view, run
`Get-Content .\build\app\logs\options.ndjson -Wait`; restart that reader after a rotation
so it resumes from the new `NR_LOG_SESSION_V1` marker.

### MSVC / Visual Studio Generator

Per the CMake 4.4 C++ modules manual, `import std` is currently supported only with Ninja generators. The supported build and verification path in this repository is therefore the LLVM/Ninja preset above.

The available MSVC-oriented configure preset is `msvc-vs`, which uses the Visual Studio 18 2026 generator and is kept for IDE-oriented workflows. It is not the supported `import std` verification path under CMake 4.4.

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
| Slang | `v2026.13` | Shader language, compilation, reflection, SPIR-V generation |
| glTF-Sample-Assets | `2bac6f8c` | Sample assets for import, testing, and regression cases |
| NVIDIA DLSS SDK | `310.7.0` | NGX headers, static loader input for MSVC bridge publication, and the Release Ray Reconstruction feature DLL |
| Ninja tracing | `fc292457` | Converts Ninja build logs into Chrome trace JSON through the project trace target |
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
| `assimp` | Model and scene import |
| `stb` | Generic image decode fallback |
| `libjpeg-turbo` | Fast JPEG decode path |
| `openexr` | HDR environment-map decode |
| `flecs` | ECS runtime used by the scene layer |
| `boost-asio` | Networking and asynchronous I/O foundation |
| `boost-beast` | WebSocket transport for the authenticated agent controller |
| `boost-json` | All project-owned C++ JSON parsing and serialization through `dependency.json` |
| `nr-lua` | Project-packaged Lua runtime for offline automation |

### Notes

- Third-party C/C++ headers are surfaced to engine code through narrow `dependency.*` C++ modules under `src/extern`; `src/extern/exportDependency.ixx` remains a compatibility umbrella. Internal project sources should import the narrow module they need instead of adding raw third-party includes.
- The Slang git submodule is kept under `src/extern/slang`, configured as a local build-tree CMake package, and consumed by the engine through `find_package(slang)` / `slang::slang`.
- The Ninja tracing submodule is pinned to an explicitly reviewed commit; do not float `tools/ninjatracing` with `git submodule update --remote`.
- Additional transitive dependencies used by Slang, SPIR-V tooling, and related build scripts are resolved through the Slang submodule itself.
- Vcpkg package versions are controlled by the active manifest/toolchain resolution in your local environment.
- If you want to update submodules, use normal Git workflows. Nsight Aftermath is the exception and should be updated manually from the NVIDIA Developer website when needed.
