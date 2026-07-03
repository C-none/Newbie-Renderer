# Shader Naming and Organization Guide

This document defines the enforced Slang module/file organization used by `ShaderService`.

## Project Policy and Reference Docs

The files under `nrslang-doc/` are Slang reference material. They are useful for API background, but they do not override this repository's shader policy. In particular, even if an upstream Slang reference discusses bindable resources as entry-point parameters, shader code consumed by `nr.rhi` must follow the global-scope binding rule below.

## Mandatory Resource Binding Scope Policy

The following constraint is mandatory for all shader code consumed by `nr.rhi`:

- Bindable resources must be declared in **global scope** only.
- Entry-point parameter lists may only contain stage IO (for example `SV_DispatchThreadID`, varyings, and non-bindable value parameters).
- Any bindable resource declared directly on an entry point is forbidden.

In this repository, descriptor/push-constant reflection for runtime binding is intentionally derived from global scope only.
Entry-point resource bindings are rejected by RHI validation instead of being merged.

## Fixed Session Search Paths

`ISession` only uses two search paths:

1. `<shader_cache_root>/<session_hash>`
2. `shader/`

No other runtime search paths participate in Slang module lookup.

Search order is significant: module resolution follows path order in `slang::SessionDesc::searchPaths`.
If the same module exists in both paths, the first matching path wins.

## C++ Path Canonicalization Rules

`ShaderService` normalizes module/path strings in C++ before lookup and dependency analysis.

- `moduleNameToPath("a.b.c") -> "a/b/c"`
- `modulePathToName("a/b/c.slang") -> "a.b.c"`

`modulePathToName(...)` applies these normalization steps:

1. Convert `\\` to `/`
2. Strip trailing `.slang` if present
3. Remove leading `./` repeatedly
4. Convert `/` to `.`

Practical implications:

- `test/main/main.slang`, `./test/main/main.slang`, and `test\\main\\main.slang` all normalize to `test.main.main`.
- Cache file path is generated from normalized module name: `<cache>/<module_path>.slang-module`.
- Always treat `shader/`-relative path as the source of truth for module identity.

## Module Name Mapping Rule

Module names are derived from paths relative to `shader/`:

- `shader/test/utils/utils.slang` -> `test.utils.utils`
- `shader/test/main/main.slang` -> `test.main.main`

So user code should write:

```slang
import test.utils.utils;
```

Current Slang version in this repo does not accept dotted `module a.b;` declarations.
Therefore primary files keep simple `module` declarations, while runtime module identity is still derived from `shader/`-relative path:

```slang
module utils;
```

## Naming Conventions (`module` / `import` / `implementing`)

### 1) `module` declaration (inside a primary file)

- Use the **leaf name only** (no dots), matching current Slang limitation in this repo.
- Required style: lower camelCase identifier.
- Primary file name should match the leaf name.

Example:

- File: `shader/test/utils/useFlag.slang`
- Declaration: `module useFlag;`

### 2) `import` declaration (cross-module reference)

- Use **full shader-root-relative module name** in dotted form.
- Do not use leaf-only imports when the module is outside current folder hierarchy.
- Required style: all segments in lower camelCase.

Example:

```slang
import test.utils.useFlag;
import test.dep.dep;
```

### 3) `implementing` declaration (implementing file)

- Must declare the **same module token** as the primary file's `module` declaration.
- Use leaf-name form consistent with the primary file (for example `implementing utils;`).
- Implementing files must live in the same directory as the primary file.

Example:

- Primary: `utils.slang` -> `module utils;`
- Implementing: `constant.slang` -> `implementing utils;`

### 4) Consistency rule between filesystem and symbols

- Directory path defines fully qualified module identity (`test/utils/useFlag` -> `test.utils.useFlag`).
- `module`/`implementing` keep leaf token (`utils`) for parser compatibility.
- `import` must use fully qualified dotted name (`test.utils.utils`).

This split is intentional and required by current Slang behavior in this repository.

### 5) Shader helper function names

- Use normal lower camelCase helper names.
- Do not add project prefixes such as `nr` to shader helper functions.
- Keep domain context in the helper name when needed, for example `sceneLightAliasCount()` or `currentRtHitSurface()`.

## Shared Common Shader Module

Every project shader imports the root `common` module:

```slang
import common;
```

`shader/common.slang` is the shared shader-side ABI entry and must stay an aggregation file only. It should contain explanatory comments plus `__include` lines, while actual declarations live under `shader/include`.
`shader/include/globalUniform.slang` is an `implementing common` file that declares the global frame uniform payload type and buffer:

```slang
public struct GlobalFrameUniforms { /* ... */ }
[[vk::binding(0, 5)]]
public ConstantBuffer<GlobalFrameUniforms> gFrame;
```

The global frame uniform uses Vulkan descriptor set 5, binding 0, and carries current camera matrices, previous view data for future motion-vector work, camera world position, and frame state. `frameState.xy` carries the monotonic 64-bit sample-frame ordinal used for per-frame shader sampling, while `frameState.z` preserves the resource frame slot. Keep set 0-4 available for the existing semantic descriptor-array conventions and local fixed bindings unless a shader-specific ABI document says otherwise. Set 6 is reserved by the common ABI for scene lights.

Only shaders that actually reference `gFrame` require a matching C++ descriptor binding. Pass code should bind it through the reflection-backed `shaderCursor` path, normally via renderer pass builders such as:

```cpp
.uniform("gFrame", context.globalResources.get().frameUniform, "Renderer.GlobalFrameUniforms")
```

`shader/include/sceneTextures.slang` declares the global scene material texture table:

```slang
[[vk::binding(0, 1)]]
public Sampler2D<float4> gSceneTextures[];
```

All material texture types share this single runtime descriptor array. `shader/include/materialTextureIds.slang` defines the common material texture slot constants and packed `uint16` ID unpack helpers used by raster, GBuffer, and RT shaders. Texture ID 0 is the renderer-owned purple fallback; resident scene texture IDs are renderer-assigned descriptor indices.

`shader/include/sceneLights.slang` declares the global scene light buffers:

```slang
[[vk::binding(0, 6)]]
public ConstantBuffer<SceneLightGpuHeader> gSceneLightHeader;
[[vk::binding(1, 6)]]
public StructuredBuffer<SceneLightGpuRecord> gSceneLights;
[[vk::binding(2, 6)]]
public StructuredBuffer<SceneLightAliasGpuRecord> gSceneLightAliasTable;
```

`LightPrepareNode` publishes the matching C++ buffers as `frameResource::sceneLightHeader`, `frameResource::sceneLights`, and `frameResource::sceneLightAliasTable`. It prepends a warm default directional sun before scene-authored lights, so PathTracing normally sees at least one positive-energy alias-table entry even when the loaded asset has no lights. The lower-level alias-table helper still supports a valid zero-count header plus one-record dummy list/alias buffers for direct helper use. The same include provides helpers for evaluating glTF punctual `directional`, `point`, and `spot` light energy from uploaded records and for sampling the alias table built from Rec.709 luminance times intensity; runtime influence range is infinite, so shader evaluation does not apply finite range cutoff.

RT material shading declarations live in `shader/include/material`. `base.slang` defines the no-data `IMultiLayerMaterial` interface, and concrete material implementations such as `gltfMultiLayer.slang` receive RT material headers, layer records, texture references, and interpolated surface data explicitly.

RT resource declarations and hit reconstruction helpers live in importable modules under `shader/include/rt`. `resources.slang` declares the shared RT scene sideband bindings consumed by RT shaders, while `hitSurface.slang` depends on ray-tracing builtins and reconstructs hit-surface data from those resources. These modules are not included by `common`; RT shaders import them explicitly so raster and compute shaders do not inherit RT sideband bindings, `InstanceID()`, `GeometryIndex()`, `PrimitiveIndex()`, or object-to-world RT helper dependencies.

## End-to-End Shader Build Pipeline

This section describes the runtime pipeline used by `ShaderService` from source discovery to final SPIR-V generation.

### 1) Session initialization

- Create `IGlobalSession` and `ISession`.
- Configure search paths in fixed order:
  1. `<shader_cache_root>/<session_hash>`
  2. `shader/`

### 2) Module load and source/cache resolution

- Convert module name to query path using module mapping rules.
- Call `ISession::loadModule("<module_path>")` (slash form, e.g. `test/utils/useFlag`).
- Slang resolves the module by search-path order.
- When an up-to-date `.slang-module` exists in cache, it can be loaded directly.
- Otherwise Slang loads/parses source from `shader/` and builds module state.

### 3) Dependency expansion

- Root module dependencies are resolved via `import` and `__include`.
- `import` uses full dotted module names.
- `__include` uses shader-root-relative token form.
- Implementing files are associated with their primary module via `implementing <leaf>;`.

### 4) Cache write-back

- After module load succeeds, module binary is persisted to:
  - `<shader_cache_root>/<session_hash>/<module_path>.slang-module`
- Cache directory layout mirrors `shader/` layout.
- Cache keys are path-derived (normalized module identity) plus session context.

### 5) Program composition and link

- Resolve entry point via `IModule::findAndCheckEntryPoint(...)`.
- Build component array `[module, entryPoint]`.
- Create composite program with `ISession::createCompositeComponentType(...)`.
- Link with `IComponentType::link(...)` to produce a linked program.

### 6) Target code generation

- Generate target code using `linkedProgram->getEntryPointCode(...)`.
- Output is SPIR-V bytecode for the selected target/profile.
- Reflection/dependency information is queried from component interfaces as needed.

## Cache and Naming Invariants

- One module identity corresponds to one normalized shader-root-relative path.
- `import` identity is always fully qualified dotted name.
- `module` and `implementing` token must match each other within the module folder.
- Every module path segment must be lower camelCase (`useFlag`) and cannot contain `_` or `-`.
- Cache artifact path must be the normalized module path with `.slang-module` suffix.
- Search-path order is authoritative for cache-vs-source precedence.

## Cache Layout Rule

Cache layout under `<shader_cache_root>/<session_hash>/` mirrors `shader/` module layout:

- module binary: `<cache>/<module_path>.slang-module`

Example:

- source: `shader/main.slang`
- cache: `<cache>/main.slang-module`

