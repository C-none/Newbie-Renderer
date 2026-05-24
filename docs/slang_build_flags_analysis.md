# Slang Build Flags Analysis For NewbieRenderer

## Scope

This note analyzes the Slang build configuration used by NewbieRenderer when Slang is embedded as a submodule and linked only as a static library.

The target use case is narrow and explicit:

- NewbieRenderer links Slang as a library, not as the `slangc` executable.
- NewbieRenderer only needs Slang to compile Slang shaders to the SPIR-V backend.
- NewbieRenderer does not currently need DXIL, LLVM CPU/C++ backends, GFX samples, tests, language-server tooling, or WebGPU helper artifacts.

## Evidence From This Repository

The local renderer side already shows a SPIR-V-only integration:

- [`src/rhi/nrSlang.ixx`](../src/rhi/nrSlang.ixx) sets `SlangCompilerOptionName::EmitSpirvDirectly = 1`.
- [`src/rhi/nrSlang.ixx`](../src/rhi/nrSlang.ixx) uses `target = SLANG_SPIRV`.
- [`src/rhi/nrSlang.ixx`](../src/rhi/nrSlang.ixx) uses `profile = "SPIRV_1_6"`.
- The code path creates a Slang session directly through the API and does not rely on the `slangc` executable.

That makes the current integration materially different from a full upstream Slang developer build.

## Sources Consulted

- Official documentation:
  - <https://docs.shader-slang.org/en/latest/external/slang/docs/building.html>
- Vendored Slang source in this repo:
  - `src/extern/slang/CMakeLists.txt`
  - `src/extern/slang/external/CMakeLists.txt`
  - `src/extern/slang/source/slang/CMakeLists.txt`
  - `src/extern/slang/source/slang-glslang/CMakeLists.txt`
  - `src/extern/slang/source/slang-rt/CMakeLists.txt`
  - `src/extern/slang/tools/CMakeLists.txt`
  - `src/extern/slang/cmake/LLVM.cmake`
  - `src/extern/slang/cmake/SlangTarget.cmake`
- Vendored Slang revision:
  - tag: `v2026.5.2`
  - commit: `80b74a9f33c2a598dc37d13a87ed384f5d96f6f6`

## Safe Changes Already Applied

The following changes were applied directly in [`src/extern/CMakeLists.txt`](../src/extern/CMakeLists.txt), because they are aligned with the current renderer requirements and do not need a separate product decision:

| Item | Applied value | Why it is safe here |
| --- | --- | --- |
| `SLANG_LIB_TYPE` | `STATIC` | The renderer explicitly wants a static Slang library. |
| `SLANG_ENABLE_CUDA` | `OFF` | CUDA test/backend integration is not needed for Slang -> SPIR-V in this renderer. |
| `SLANG_ENABLE_OPTIX` | `OFF` | OptiX depends on CUDA and is unrelated to the current use case. |
| `SLANG_ENABLE_NVAPI` | `OFF` | NVAPI is only relevant to optional platform/backend integrations outside the current path. |
| `SLANG_ENABLE_AFTERMATH` | `OFF` | Slang-side Aftermath integration is tied to optional GFX/debug flows, not the compiler API path used here. |
| `SLANG_ENABLE_SLANGRT` | `OFF` | `slang-rt` is a separate runtime target and is not linked by the renderer’s current Slang API usage. |
| `SLANG_ENABLE_DXIL` | `OFF` | The renderer only targets SPIR-V output. |
| `SLANG_ENABLE_PREBUILT_BINARIES` | `OFF` | Avoids copying optional prebuilt Slang runtime binaries that are irrelevant to the static SPIR-V-only embed. |
| `SLANG_EXCLUDE_DAWN` | `ON` | Excludes WebGPU Dawn helper artifacts not used by the renderer. |
| `SLANG_EXCLUDE_TINT` | `ON` | Excludes `slang-tint` helper artifacts not used by the renderer. |
| `SLANG_SLANG_LLVM_FLAVOR` | `DISABLE` | LLVM support is not required for SPIR-V-only shader compilation in this project. |

## Non-Flag Build-Graph Adjustment Already Applied

This is not a Slang top-level CMake option, but it is important for a minimal embedded build:

- `slang-neural-module` is now excluded from default builds.

Why:

- The vendored Slang standard module target is `ALL` by default.
- When `SLANG_ENABLE_SLANGC=OFF`, that target can fall back to an external/system `slangc`.
- That behavior is unnecessary for NewbieRenderer’s current use case and can create fragile toolchain coupling.

## User Decisions Still Open

The following items are deliberately not forced yet because they are legitimate tradeoffs rather than pure dead-weight removal:

| Flag | Current state | Why I left it for you |
| --- | --- | --- |
| `SLANG_ENABLE_RELEASE_DEBUG_INFO` | upstream default behavior | Turning it off can speed up Release builds and reduce PDB/debug output size, but it also makes Release-side Slang debugging harder. |
| `SLANG_ENABLE_SPLIT_DEBUG_INFO` | upstream default behavior | Mostly affects executable/shared/module debug artifact generation; it is less important after the current trimming, but whether to disable it depends on your packaging/debugging expectations. |
| `SLANG_USE_SCCACHE` | `OFF` | Good for rebuild speed if you already use `sccache`, but it introduces an external tooling dependency and auto-disables PCH. |
| `SLANG_USE_SYSTEM_*` family | unchanged | These are dependency sourcing policy choices, not unconditional wins. They may simplify or complicate integration depending on how strictly you want Slang to share host packages. |

## Flag-By-Flag Analysis

### 1. Core Output, Tooling, And Build-Graph Flags

| Flag | Upstream default | Detailed function | Relevance to NewbieRenderer | Recommendation |
| --- | --- | --- | --- | --- |
| `SLANG_LIB_TYPE` | `SHARED` | Chooses whether the primary Slang compiler library is built as a shared or static library. | The renderer explicitly wants a static embed. | Set `STATIC`. Applied. |
| `SLANG_EMBED_CORE_MODULE` | `ON` | Embeds a compiled binary form of the Slang core module into the library. At least one of this flag and `SLANG_EMBED_CORE_MODULE_SOURCE` must remain enabled. | Binary embedding is not required for the current embed and can introduce extra generator/bootstrap work. | Keep `OFF` together with source embedding `ON`. Already in project. |
| `SLANG_EMBED_CORE_MODULE_SOURCE` | `ON` | Embeds core module source text into the library. This is the lighter fallback when binary embedding is disabled. | Needed because both embed flags cannot be `OFF`, and source embedding keeps the build simpler than binary embedding. | Keep `ON`. Already in project. |
| `SLANG_ENABLE_DXIL` | `ON` | Enables DXIL generation support through the DXC-facing code paths. | Current renderer integration only targets SPIR-V. DXIL support is unused dead surface. | Set `OFF`. Applied. |
| `SLANG_ENABLE_GFX` | `ON` | Builds legacy `gfx` targets and pulls in more platform/backend code. | Not needed for API-only shader compilation. | Keep `OFF`. Already in project. |
| `SLANG_ENABLE_SLANG_RHI` | `ON` | Builds the `slang-rhi` dependency used mainly by tests/examples. | NewbieRenderer has its own RHI and does not use Slang RHI. | Keep `OFF`. Already in project. |
| `SLANG_ENABLE_SLANGRT` | `ON` | Builds the separate `slang-rt` runtime library target. | Not required by the current `slang` API-only SPIR-V compilation flow. | Set `OFF`. Applied. |
| `SLANG_ENABLE_SLANG_GLSLANG` | `ON` | Builds the runtime-loadable `slang-glslang` bridge plus bundled `glslang` and `SPIRV-Tools`. It is mainly used for SPIR-V-via-GLSL paths and glslang-backed downstream work. | The renderer sets `EmitSpirvDirectly=1` and targets `SPIRV_1_6`, so direct SPIR-V emission is the active path. | Keep `OFF`. Already in project. Revisit only if you need `-emit-spirv-via-glsl` or legacy SPIR-V 1.0 to 1.2 compatibility. |
| `SLANG_ENABLE_SLANGD` | `ON` | Builds the Slang language server executable. | Not needed for runtime/compiler-library embedding. | Keep `OFF`. Already in project. |
| `SLANG_ENABLE_SLANGC` | `ON` | Builds the standalone `slangc` command-line compiler. | The renderer uses the Slang API directly rather than shelling out to `slangc`. | Keep `OFF`. Already in project. |
| `SLANG_ENABLE_SLANGI` | `ON` | Builds the Slang interpreter executable. | Not used by the renderer. | Keep `OFF`. Already in project. |
| `SLANG_ENABLE_REPLAYER` | `ON` | Builds the `slang-replay` tool for record/replay workflows. | Not used by the renderer. | Keep `OFF`. Already in project. |
| `SLANG_ENABLE_TESTS` | `ON` | Builds Slang tests. Requires extra targets and more dependencies. | Not needed for the engine’s embedded third-party build. | Keep `OFF`. Already in project. |
| `SLANG_ENABLE_EXAMPLES` | `ON` | Builds example applications. | Not needed for the engine. | Keep `OFF`. Already in project. |
| `SLANG_STANDARD_MODULE_DEVELOP_BUILD` | `ON` | Enables development-oriented behavior for standard modules, including `UNIT_TEST`-style paths. | The project wants stable embedded behavior, not Slang standard-module development iteration. | Keep `OFF`. Already in project. |
| `SLANG_ENABLE_PREBUILT_BINARIES` | `ON` | Copies optional prebuilt runtime binaries from `external/slang-binaries` into the output. This is mostly packaging/runtime convenience, not core library functionality. | Unnecessary for a static-library-only embed that only needs the in-process compiler API. | Set `OFF`. Applied. |
| `SLANG_EXCLUDE_DAWN` | `OFF` | Prevents optional WebGPU Dawn artifacts from being fetched/copied into the build/install graph. | Dawn is irrelevant to the current SPIR-V-only integration. | Set `ON`. Applied. |
| `SLANG_EXCLUDE_TINT` | `OFF` | Prevents optional `slang-tint` artifacts from being fetched/copied. | WGSL/Tint translation is outside the current use case. | Set `ON`. Applied. |
| `SLANG_SLANG_LLVM_FLAVOR` | `FETCH_BINARY_IF_POSSIBLE` | Chooses whether Slang should fetch/build/consume LLVM support, or disable it entirely. | LLVM is only needed for LLVM-backed targets and workflows, not direct SPIR-V emission. | Set `DISABLE`. Applied. |
| `SLANG_SLANG_LLVM_BINARY_URL` | platform-dependent | Overrides where a prebuilt `slang-llvm` binary is fetched from. | Irrelevant once LLVM is disabled. | Do not set. |
| `SLANG_GENERATORS_PATH` | empty | Supplies already-built host generators for cross-compilation. | This repo is not cross-compiling Slang itself. | Leave unset. |
| `SLANG_VERSION` | derived from git tags | Overrides the version metadata used by the Slang build. | Not a build-speed lever and risky to override in a vendored submodule. | Do not override. |

### 2. Optional Platform, Backend, And Test Integration Flags

| Flag | Upstream default | Detailed function | Relevance to NewbieRenderer | Recommendation |
| --- | --- | --- | --- | --- |
| `SLANG_ENABLE_CUDA` | `AUTO` | Enables CUDA-related testing/backend integration if CUDA is found. The official docs note this does not affect the core Slang targets supported by the compiler itself. | The renderer only needs SPIR-V output and does not use CUDA-backed Slang testing. | Set `OFF`. Applied. |
| `SLANG_ENABLE_OPTIX` | `AUTO` | Enables OptiX integration/tests; requires CUDA. | Unrelated to the current use case. | Set `OFF`. Applied. |
| `SLANG_ENABLE_NVAPI` | `AUTO` on Windows | Enables NVAPI-related support in optional backend layers. | Not used by the current embedded compiler flow. | Set `OFF`. Applied. |
| `SLANG_ENABLE_AFTERMATH` | `AUTO` | Enables Slang-side Aftermath integration in GFX and adds the Aftermath crash example. | Not relevant when GFX/examples are disabled and the renderer uses Slang only as a compiler library. | Set `OFF`. Applied. |
| `SLANG_ENABLE_XLIB` | `AUTO` on Linux / `OFF` elsewhere | Adds Xlib support for Linux windowed app paths. | Windows-only renderer integration. | Leave as-is; effectively irrelevant here. |
| `SLANG_ENABLE_DX_ON_VK` | `OFF` | Enables DX11/DX12-on-Vulkan testing paths via dxvk/vkd3d-proton. | Not relevant to this project’s Slang embed. | Keep `OFF`. |

### 3. Build-Speed, Debugging, And Instrumentation Flags

| Flag | Upstream default | Detailed function | Relevance to NewbieRenderer | Recommendation |
| --- | --- | --- | --- | --- |
| `SLANG_ENABLE_PCH` | `ON` | Enables precompiled headers for targets that request them. In Slang this is wired into several large targets and usually improves build time. | This is a genuine build-speed optimization, not feature bloat. | Keep `ON`. Do not disable unless you adopt `sccache`. |
| `SLANG_USE_SCCACHE` | `OFF` | Uses `sccache` as compiler launcher for faster rebuilds. Slang auto-disables PCH when this is enabled because of a known incompatibility. | Helpful only if your environment already standardizes on `sccache`. | Needs your decision. |
| `SLANG_ENABLE_RELEASE_DEBUG_INFO` | `ON` | Adds debug info even for `Release` builds. In `SlangTarget.cmake`, this expands the set of configs that receive MSVC `/DEBUG` or `-g`. | Good for postmortem or stepping into Slang in Release, but it can increase build time and artifact size. | Needs your decision. |
| `SLANG_ENABLE_SPLIT_DEBUG_INFO` | `ON` | Generates separate debug artifacts for executable/shared/module outputs when supported. | Less impactful after trimming most tools/modules, but still a packaging/debug tradeoff rather than dead code. | Leave unchanged unless you want to optimize symbol generation aggressively. |
| `SLANG_ENABLE_RELEASE_LTO` | `OFF` | Enables LTO/IPO for `Release` and `RelWithDebInfo`. | Can improve runtime code quality but slows builds and links. | Keep `OFF` for faster embedded dependency builds. |
| `SLANG_ENABLE_ASAN` | `OFF` | Enables ASan and, where possible, UBSan. | Valuable for upstream debugging, not for normal third-party embedding. | Keep `OFF`. |
| `SLANG_ENABLE_COVERAGE` | `OFF` | Adds compiler/linker coverage instrumentation. | Testing-only overhead. | Keep `OFF`. |
| `SLANG_ENABLE_TIME_TRACE` | `OFF` | Enables Clang time-trace profiling for build analysis. | Useful only when profiling Slang build performance itself. | Keep `OFF`. |
| `SLANG_ENABLE_FULL_IR_VALIDATION` | `OFF` | Enables full IR validation and is explicitly marked slow. | Upstream compiler debugging aid, not needed in the renderer dependency build. | Keep `OFF`. |
| `SLANG_ENABLE_IR_BREAK_ALLOC` | `OFF` | Enables IR allocation debug instrumentation. | Debug-only internal aid. | Keep `OFF`. |
| `SLANG_IGNORE_ABORT_MSG` | `OFF` | Suppresses Windows modal abort dialogs in built executables to make unattended workflows easier. | Nice for CI/automation ergonomics, but not a build-speed or feature-trimming requirement. | Leave unchanged unless you want headless tooling behavior. |

### 4. Dependency Source Selection Flags

These do not primarily change feature surface. They control whether Slang consumes bundled third-party code or system-provided packages. They are not unconditional speed wins.

| Flag | Upstream default | Detailed function | Relevance to NewbieRenderer | Recommendation |
| --- | --- | --- | --- | --- |
| `SLANG_USE_SYSTEM_MINIZ` | `OFF` | Uses a system Miniz package instead of the vendored copy. | Policy choice, not a guaranteed speed improvement. | Leave unchanged. |
| `SLANG_USE_SYSTEM_LZ4` | `OFF` | Uses a system LZ4 package instead of the vendored copy. | Policy choice. | Leave unchanged. |
| `SLANG_USE_SYSTEM_VULKAN_HEADERS` | `OFF` | Uses system Vulkan headers instead of the vendored copy. | Mostly relevant if you want strict dependency unification across the whole super-project. | Leave unchanged unless you explicitly want that policy. |
| `SLANG_USE_SYSTEM_SPIRV_HEADERS` | `OFF` | Uses system SPIR-V headers instead of the vendored copy. | SPIR-V headers are required by the direct SPIR-V emitter, but choosing system vs bundled is a dependency-policy call. | Leave unchanged. |
| `SLANG_USE_SYSTEM_UNORDERED_DENSE` | `OFF` | Uses system `unordered_dense` instead of the vendored copy. | Only matters if you standardize third-party sourcing. | Leave unchanged. |
| `SLANG_USE_SYSTEM_SPIRV_TOOLS` | `OFF` | Uses a system SPIRV-Tools package instead of the vendored copy. | Irrelevant while `SLANG_ENABLE_SLANG_GLSLANG=OFF`, because SPIRV-Tools is only pulled in through that path here. | Leave unchanged. |
| `SLANG_USE_SYSTEM_GLSLANG` | `OFF` | Uses system `glslang` instead of the vendored copy. | Irrelevant while `SLANG_ENABLE_SLANG_GLSLANG=OFF`. | Leave unchanged. |
| `SLANG_ENABLE_SPIRV_TOOLS_MIMALLOC` | `ON` on Windows, else `OFF` | Enables `mimalloc` for SPIRV-Tools to improve compile performance. This only matters when Slang actually builds bundled SPIRV-Tools. | No effect in the current configuration because `SLANG_ENABLE_SLANG_GLSLANG=OFF`. | Leave unchanged; currently irrelevant. |
| `SLANG_SPIRV_HEADERS_INCLUDE_DIR` | empty | Supplies a custom SPIR-V headers location. | Only useful for custom dependency layouts. | Leave unset. |

### 5. Override Path Flags

These are all integration escape hatches. They are useful if you intentionally relocate dependencies, but they are not trimming levers by themselves.

| Flag | Default | Detailed function | Relevance to NewbieRenderer | Recommendation |
| --- | --- | --- | --- | --- |
| `SLANG_OVERRIDE_LZ4_PATH` | `OFF` | Uses a user-specified LZ4 source root. | Not needed in the current vendored-submodule layout. | Leave unset. |
| `SLANG_OVERRIDE_MINIZ_PATH` | `OFF` | Uses a user-specified Miniz source root. | Not needed. | Leave unset. |
| `SLANG_OVERRIDE_UNORDERED_DENSE_PATH` | `OFF` | Uses a user-specified `unordered_dense` source root. | Not needed. | Leave unset. |
| `SLANG_OVERRIDE_VULKAN_HEADERS_PATH` | `OFF` | Uses a user-specified Vulkan headers source root. | Not needed. | Leave unset. |
| `SLANG_OVERRIDE_SPIRV_HEADERS_PATH` | `OFF` | Uses a user-specified SPIR-V headers source root. | Not needed. | Leave unset. |
| `SLANG_OVERRIDE_SPIRV_TOOLS_PATH` | `OFF` | Uses a user-specified SPIRV-Tools source root. | Irrelevant while `SLANG_ENABLE_SLANG_GLSLANG=OFF`. | Leave unset. |
| `SLANG_OVERRIDE_GLSLANG_PATH` | `OFF` | Uses a user-specified `glslang` source root. | Irrelevant while `SLANG_ENABLE_SLANG_GLSLANG=OFF`. | Leave unset. |
| `SLANG_OVERRIDE_GLM_PATH` | `OFF` | Uses a user-specified `glm` source root for optional targets that need it. | Optional tooling/examples path only. | Leave unset. |
| `SLANG_OVERRIDE_IMGUI_PATH` | `OFF` | Uses a user-specified `imgui` source root for optional targets that need it. | Optional tooling/examples path only. | Leave unset. |
| `SLANG_OVERRIDE_SLANG_RHI_PATH` | `OFF` | Uses a user-specified `slang-rhi` source root. | Irrelevant while `SLANG_ENABLE_SLANG_RHI=OFF`. | Leave unset. |
| `SLANG_OVERRIDE_TINYOBJLOADER_PATH` | `OFF` | Uses a user-specified `tinyobjloader` source root. | Optional example/tool path only. | Leave unset. |
| `SLANG_OVERRIDE_LUA_PATH` | `OFF` | Uses a user-specified Lua path for generator tooling. | Not needed in the current submodule layout. | Leave unset. |
| `SLANG_OVERRIDE_MIMALLOC_PATH` | `OFF` | Uses a user-specified `mimalloc` source root. | Only relevant if SPIRV-Tools is built with `mimalloc`. | Leave unset. |
| `SLANG_OVERRIDE_CMARK_PATH` | `OFF` | Uses a user-specified `cmark` source root. | Not needed in the current layout. | Leave unset. |

### 6. Miscellaneous Build Variables

| Flag / variable | Default | Detailed function | Relevance to NewbieRenderer | Recommendation |
| --- | --- | --- | --- | --- |
| `SLANG_GITHUB_TOKEN` | empty | Supplies a token for GitHub REST API access when fetching release artifacts. | Irrelevant once Dawn/Tint/LLVM fetches are disabled. | Leave unset. |

## Why `SLANG_ENABLE_SLANG_GLSLANG=OFF` Is Safe Here

This flag deserves a special note because it can look scary when the end goal is SPIR-V.

Why it is safe in the current renderer:

- NewbieRenderer requests direct SPIR-V emission through the Slang API.
- The current profile is `SPIRV_1_6`.
- The vendored Slang build uses `slang-glslang` only for the glslang-backed downstream path, not for the core direct SPIR-V emitter.

When to reconsider it:

- If you need `-emit-spirv-via-glsl`.
- If you need glslang as a comparison/reference backend.
- If you need older SPIR-V compatibility paths that rely on glslang rather than Slang’s direct SPIR-V backend.

## Why `SLANG_ENABLE_DXIL=OFF` Is Still A Reasonable Trim

This flag does not remove all DXC-related source files from compilation, so the build-time win is not as dramatic as turning off tests or glslang.

It is still worth disabling because:

- it removes an unused output capability surface from the embedded compiler;
- it aligns the produced dependency with the renderer’s declared backend needs;
- it reduces the chance of accidental future reliance on DXIL-only behavior from this embedded Slang build.

## Practical Recommendation Set

If the goal remains:

- embed Slang as a static library,
- compile only to SPIR-V,
- keep the embedded third-party build as lean and deterministic as possible,

then the current applied configuration is the right baseline.

The only remaining meaningful product-level choices are:

1. whether to keep `SLANG_ENABLE_RELEASE_DEBUG_INFO=ON`;
2. whether to adopt `SLANG_USE_SCCACHE=ON` in your local/CI toolchain;
3. whether you want Slang to consume system packages through the `SLANG_USE_SYSTEM_*` family.

Everything else important to feature trimming is already either disabled or intentionally left in the minimal required state.
