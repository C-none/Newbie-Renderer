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

- `renderer/pathTracing/core.slang`, `./renderer/pathTracing/core.slang`, and `renderer\\pathTracing\\core.slang` all normalize to `renderer.pathTracing.core`.
- Cache file path is generated from normalized module name: `<cache>/<module_path>.slang-module`.
- Always treat `shader/`-relative path as the source of truth for module identity.

## Module Name Mapping Rule

Module names are derived from paths relative to `shader/`:

- `shader/renderer/pathTracing/core.slang` -> `renderer.pathTracing.core`
- `shader/test/rt/minimalRtTriangle.slang` -> `test.rt.minimalRtTriangle`

So user code should write:

```slang
import renderer.pathTracing.core;
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

- File: `shader/renderer/pathTracing/core.slang`
- Declaration: `module core;`

### 2) `import` declaration (cross-module reference)

- Use **full shader-root-relative module name** in dotted form.
- Do not use leaf-only imports when the module is outside current folder hierarchy.
- Required style: all segments in lower camelCase.

Example:

```slang
import renderer.pathTracing.core;
import renderer.pathTracing.resources;
```

### 3) `implementing` declaration (implementing file)

- Must declare the **same module token** as the primary file's `module` declaration.
- Use leaf-name form consistent with the primary file (for example `implementing common;`).
- An implementing file may live under a different shader-root-relative directory. It is attached to the primary module through `__include`, while its `implementing` token identifies the module being extended.

Example:

- Primary: `shader/common.slang` -> `module common;`
- Implementing include: `shader/include/globalUniform.slang` -> `implementing common;`

### 4) Consistency rule between filesystem and symbols

- A primary module's directory path defines its fully qualified module identity (`renderer/pathTracing/core` -> `renderer.pathTracing.core`).
- `module` keeps the primary file's leaf token (`core`) for parser compatibility.
- `implementing` names the leaf token of the primary module being extended (`common` for files aggregated by `common.slang`).
- `import` must use the primary module's fully qualified dotted name (`renderer.pathTracing.core`).

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

Declarations that must be shared as a stable Slang/C++ ABI live under `shader/include/share`. These files are data-only: they may contain only `public static const` values, `enum : uint` declarations, and plain ABI `struct` declarations. They must not declare functions, resources, mutable globals, generics, interfaces/classes/extensions, nested namespaces, arrays, matrices, `bool`, `half`, or resource-typed fields. The dependency build runs `nr_shader_share_codegen` over `shader/common.slang`, reflects declarations whose source path is under `shader/include/share`, and emits the generated C++26 module `dependency.shaderShare` in namespace `nr::shader::share`.

For every `enum : uint` under `shader/include/share`, the codegen also emits enumerator-name reflection: a `SlangEnumMeta<Enum>` specialization plus a generic `slangEnumLiteral<Enum>(value)` helper in `nr::shader::share`. C++ that needs a Slang-side enum literal in generated link-time type assignments must call `slangEnumLiteral` on the translated enum instead of hand-writing a per-enum `switch`. This keeps the literal spelling in lockstep with the Slang source and lets any translated enum be reused directly rather than redefined in C++. The generated metadata is a deliberate stopgap: once the LLVM/clang toolchain implements C++26 static reflection (P2996, `<meta>`), the emitted `SlangEnumMeta` specializations should be deleted and `slangEnumLiteral` reimplemented on top of `std::meta::identifier_of`.

Link-time type variant declarations must also live under `shader/include` and be made visible through `shader/common.slang`. Generated specialization modules are intentionally narrow: they may only `import common;` and then export assignments from the C++ variant description. Scalar assignments generate `export static const <type> <name> = <literal>;` where `<type>` is the direct Slang declaration string (`bool`, `int`, `uint`, or `float`). Type assignments generate aliases such as `export struct T : I = Concrete;`, where the interface type and concrete RHS string are supplied directly by the owning node. Type policy implementations must be pure shader logic and must not declare new global bindable resources. Variant resources must come from the existing ABI or be passed as ordinary values.

Shader-side `extern` values used as link-time scalar assignments must never specify default values. The owning C++ node must populate the matching `SlangProgramVariantDesc` from its private compile key; UI state is staged separately and mapped to that compile key only by the owning node. Default values belong in C++ constants and node UI/input state, not in Slang `extern` declarations or separate variant metadata. An empty variant compile is only valid for shaders that do not declare required variant `extern` values.

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

All material texture types share this single runtime descriptor array. `shader/include/share/materialTextureIds.slang` defines the common `MaterialTextureSlot` enum, while `shader/include/materialTextureIds.slang` defines packed `uint16` ID unpack helpers used by raster, GBuffer, and RT shaders. Texture ID 0 is the renderer-owned neutral white fallback (1x1 linear RGBA(1,1,1,1)); resident scene texture IDs are renderer-assigned descriptor indices. RT materials store one dense texture reference per `MaterialTextureSlot`, so unauthored slots hold ID 0 and shaders always sample (a missing texture multiplies through as a neutral 1 factor, and a missing normal map decodes to tangent-space (0,0,1) via a zero effective normal scale) without a texture-presence branch. PSOs that consume this table install a linear immutable sampler during pipeline layout creation, so per-frame descriptor updates provide only image views and layouts.

`shader/include/sceneLights.slang` declares the global scene light buffers:

```slang
[[vk::binding(0, 6)]]
public ConstantBuffer<SceneLightGpuHeader> gSceneLightHeader;
[[vk::binding(1, 6)]]
public StructuredBuffer<SceneLightGpuRecord> gSceneLights;
[[vk::binding(2, 6)]]
public StructuredBuffer<SceneLightAliasGpuRecord> gSceneLightAliasTable;
```

`LightPrepareNode` publishes the matching C++ buffers as `frameResource::sceneLightHeader`, `frameResource::sceneLights`, and `frameResource::sceneLightAliasTable`. It prepends a warm default directional sun before scene-authored lights, so PathTracing normally sees at least one positive-energy alias-table entry even when the loaded asset has no lights. The lower-level alias-table helper still supports a valid zero-count header plus one-record dummy list/alias buffers for direct helper use. The same include provides helpers for evaluating glTF punctual `directional`, `point`, and `spot` light values from uploaded photometric records and for sampling the alias table built from Rec.709 luminance times glTF intensity. Point/spot shader evaluation applies inverse-square attenuation and the optional glTF range fade.

RT material shading declarations live in `shader/include/material`. The RT material layer set is a `[Flags] RtMaterialLayerFlag` combined mask (`none` == unlit; non-zero masks always contain `baseSurface` plus any subset of clearcoat/sheen/transmission). `payload.slang` owns the layer-flag specialized resolvers and BSDF variants: `resolveUnlitMaterialPayload(...)` for unlit terminal shading, `resolveLitMaterialPayloadVariant<let LayerFlags>(...)` for lit surfaces, and the `evaluate/pdf/sample` variant family whose optional lobes live inside `if (LayerFlags & flag)` static blocks so Slang link-time specialization prunes inactive layers. `pathTracing/chs.slang` exposes `MaterialCHS<let LayerFlags : RtMaterialLayerFlag>`, which the PathTracing node binds through the link-time `CHS` alias as `MaterialCHS<RtMaterialLayerFlag(N)>`.

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
- Call `ISession::loadModule("<module_path>")` (slash form, e.g. `renderer/pathTracing/core`).
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

- Resolve entry points from the root module.
- Build component array `[module, entryPoint...]`.
- For non-empty link-time variants, generate a deterministic synthetic component from sorted constants/type aliases, then append it to the component array.
- Create composite program with `ISession::createCompositeComponentType(...)`.
- Link with `IComponentType::link(...)` to produce a linked program.

### 6) Target code generation

- Generate target code using `linkedProgram->getEntryPointCode(...)`.
- Output is SPIR-V bytecode for the selected target/profile.
- Reflection/dependency information is queried from component interfaces as needed.

## Cache and Naming Invariants

- One module identity corresponds to one normalized shader-root-relative path.
- `import` identity is always fully qualified dotted name.
- Each `implementing` token must match the primary module it extends; filesystem co-location is not required when the file is attached through `__include`.
- Every module path segment must be lower camelCase (`useFlag`) and cannot contain `_` or `-`.
- Cache artifact path must be the normalized module path with `.slang-module` suffix.
- Search-path order is authoritative for cache-vs-source precedence.
- Linked program variants are process-memory cache entries keyed by session generation, compile options hash, module path, and variant hash. They are invalidated by `ShaderService::reloadSession()` and session reconfiguration.

## Cache Layout Rule

Cache layout under `<shader_cache_root>/<session_hash>/` mirrors `shader/` module layout:

- module binary: `<cache>/<module_path>.slang-module`

Example:

- source: `shader/main.slang`
- cache: `<cache>/main.slang-module`

