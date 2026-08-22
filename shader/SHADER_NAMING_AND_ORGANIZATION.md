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

1. `<shader_cache_root>/<compile-options-hash>`
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
- `shader/test/rt/minimalRtTriangle/raygen.slang` -> `test.rt.minimalRtTriangle.raygen`

So user code should write:

```slang
import renderer.pathTracing.core;
```

Current Slang version in this repo does not accept dotted `module a.b;` declarations.
Therefore primary files keep simple `module` declarations, while runtime module identity is still derived from `shader/`-relative path:

```slang
module utils;
```

## Single-Entry Shader File Rule

Every `.slang` file may declare at most one function carrying a `[shader(...)]` attribute. Files that contain shared declarations or implementation helpers declare no entry point. An entry-point file is a directly compilable primary module whose file name and leaf `module` declaration follow the normal module-name mapping rules.

## Cooperative Vector Entry Requirements

`VK_NV_cooperative_vector` is a renderer-wide device requirement, but a stage still declares the
capabilities it consumes. PathTracing `raygen.slang` and `closestHit.slang` use
`[require(spvCooperativeVectorNV)]`; the neural-training gradient root additionally requires
`spvCooperativeVectorTrainingNV` and the group operations it uses. Shared helper modules do not
declare stage requirements. A new CoopVec stage root must keep its requirements local to its one
entry point so reflection, shader caching, and pipeline diagnostics identify the actual consumer.

This is a project-level invariant, not an optional authoring convention:

- Different graphics stages and ray-tracing stages live in different files.
- A shader compile request identifies only the shader-root-relative source path. It never selects an entry point by name.
- `ShaderService` rejects a requested root module unless it exposes exactly one defined entry point after loading.
- Pipeline assembly records the entry point name reported by Slang for Vulkan and logical shader-group lookup; that discovered name is output metadata, not compile-request input.
- Link-time assignments belong only to the entry-point file whose code consumes them. A miss shader with no assignments therefore has no material or ray-generation variant identity.

For example, PathTracing uses separate roots for `raygen.slang`, material `miss.slang` / `anyHit.slang`, `shadowMiss.slang` / `shadowAnyHit.slang`, and `closestHit.slang` instead of collecting stages in one module. Entry-free `anyHitPolicy.slang` and `shadowPayload.slang` hold shared policy and payload declarations.

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

## Shader-Only Value-Struct Initialization and Construction

This policy applies to every value `struct` constructed locally in shader code, except for the `shader/include/share` ABI records listed below. A pipeline-visible layout does not by itself exempt a locally constructed record. Give every field a semantic default member initializer. Slang executes those member initializers before the body of an explicit constructor, so a constructor may override only the dynamic values it receives:

```slang
struct SurfaceSample
{
    float3 normal = float3(0.0, 0.0, 1.0);
    float weight = 1.0;

    __init(float3 shadingNormal, float sampleWeight)
    {
        normal = shadingNormal;
        weight = sampleWeight;
    }
}

SurfaceSample sample = { shadingNormal, sampleWeight };
```

For a commonly created locally constructed value struct, provide an explicit parameterized `__init(...)` and construct it with its dynamic inputs. Preserve the semantic defaults in the declaration even where that constructor normally overwrites them. This makes the default state visible, keeps constructor bodies limited to dynamic overrides, and gives future constructors a defined baseline.

After construction, write fields only when control flow or a state transition requires it: for example, a branch selects an optional lobe, a ray result updates traversal state, or an accumulating algorithm advances its state. Do not construct an object and then immediately assign its unconditional creation inputs field by field.

A runtime-indexed loop that computes an array-valued member is a necessary post-construction adjustment when supplying the completed array to `__init(...)` would require a duplicate temporary or duplicate computation. Default-initialize the record once, then fill only that computed array in the loop.

Do not add a factory or helper function solely to assemble a value struct. A constructor is the normal creation boundary; retain a helper only when it performs behavior beyond construction, such as a calculation, validation, resource access, coordinate conversion, sampling, or a reusable state transition.

The policy deliberately does not apply to the following cases:

- Declarations under `shader/include/share`: these are stable Slang/C++ ABI records and are absolutely data-only; they must not declare functions or constructors.
- Host- or pipeline-populated records that shader code never constructs locally may remain data-only. This includes GPU-buffer element records read only from buffers, `ConstantBuffer` and push-constant payloads, and stage-input structs.
- A stage-output, ray-payload, or other value record that shader code constructs locally is not an exception: use default member initializers plus an explicit parameterized constructor. Constructors and methods do not change the record's field layout.
- Slang built-in scalar, vector, matrix, resource, and other built-in types: use their ordinary constructors/conversions where needed.
- Array literals and aggregate data whose natural representation is an initializer list: retain the literal form rather than introducing a wrapper struct or constructor.
- Existing required `out`/`inout` reset paths: retain explicit reset/assignment when the caller-owned value must be cleared or reinitialized before it is written or passed onward.

## Shared Common Shader Module

Every project shader imports the root `common` module:

```slang
import common;
```

`shader/common.slang` is the shared shader-side ABI entry and must stay an aggregation file only. It should contain explanatory comments plus `__include` lines, while actual declarations live under `shader/include`.

Declarations that must be shared as a stable Slang/C++ ABI live under `shader/include/share`. These files are data-only: they may contain only `public static const` values, `enum : uint` declarations, and plain ABI `struct` declarations. They must not declare functions, resources, mutable globals, generics, interfaces/classes/extensions, nested namespaces, arrays, matrices, `bool`, `half`, or resource-typed fields. The dependency build runs `nr_shader_share_codegen` over `shader/common.slang`, reflects declarations whose source path is under `shader/include/share`, and emits the generated C++26 module `dependency.shaderShare` in namespace `nr::shader::share`.

For every `enum : uint` under `shader/include/share`, the codegen also emits enumerator-name reflection: a `SlangEnumMeta<Enum>` specialization plus a generic `slangEnumLiteral<Enum>(value)` helper in `nr::shader::share`. C++ that needs a Slang-side enum literal in generated link-time type assignments must call `slangEnumLiteral` on the translated enum instead of hand-writing a per-enum `switch`. This keeps the literal spelling in lockstep with the Slang source and lets any translated enum be reused directly rather than redefined in C++. The generated metadata is a deliberate stopgap: once the LLVM/clang toolchain implements C++26 static reflection (P2996, `<meta>`), the emitted `SlangEnumMeta` specializations should be deleted and `slangEnumLiteral` reimplemented on top of `std::meta::identifier_of`.

Link-time type variant declarations must also live under `shader/include` and be made visible through `shader/common.slang`. Generated specialization modules are intentionally narrow: they may only `import common;` and then export assignments from the C++ variant description. Scalar assignments generate `export static const <type> <name> = <literal>;` where `<type>` is the direct Slang declaration string (`bool`, `int`, `uint`, or `float`). Type assignments generate aliases such as `export struct T : I = Concrete;`, where the interface type and concrete RHS string are supplied directly by the owning node. Type policy implementations must be pure shader logic and must not declare new global bindable resources. Variant resources must come from the existing ABI or be passed as ordinary values.

The project compiler session pins Slang language version 2026. Every primary source file must begin with its `module` declaration, and every generated specialization source must begin with a `module` declaration whose name exactly matches the name passed to Slang's module-loading API. Included implementation fragments continue to use `implementing common` because they form the `common` primary module rather than independent imported modules.

The project compiler session forces Slang row-major matrix layout and the same setting participates in the shader compile-options cache key. Host-visible matrix fields must also declare `row_major` explicitly. Project shader transform math uses row vectors (`mul(vector, matrix)`), matching the DirectXMath CPU convention; API-defined Vulkan ray-tracing built-ins remain behind their dedicated reconstruction helpers.

Shader-side `extern` values used as link-time scalar assignments must never specify default values. The owning C++ node must populate the matching `SlangProgramVariantDesc` from its private compile key; UI state is staged separately and mapped to that compile key only by the owning node. Default values belong in C++ constants and node UI/input state, not in Slang `extern` declarations or separate variant metadata. An empty variant compile is only valid for shaders that do not declare required variant `extern` values.

`shader/include/globalUniform.slang` is an `implementing common` file that declares the global frame uniform payload type and buffer:

```slang
public struct GlobalFrameUniforms { /* ... */ }
[[vk::binding(0, 3)]]
public ConstantBuffer<GlobalFrameUniforms> gFrame;
```

The global frame uniform uses Vulkan descriptor set 3, binding 0, and is a fixed 288-byte record: four `row_major float4x4` projective camera matrices (`viewProjection`, `inverseViewProjection`, `unjitteredViewProjection`, and `previousViewProjection`), camera world position, and frame state. `frameState.xy` carries the monotonic 64-bit sample-frame ordinal used for per-frame shader sampling, while `frameState.z` preserves the resource frame slot. Standalone view/projection fields are not uploaded. Projective transforms remain full 4x4; a validated affine host-visible transform may instead use `row_major float4x3` for 12 floats/48 bytes, as NormalBuffer does. Project shader resources follow one semantic set convention: standalone samplers use set 0, sampled or combined images use set 1, storage images use set 2, uniform/storage/texel buffers use set 3, and acceleration structures use set 4. In the shared set 3 ABI, `gFrame` owns binding 0 and the seven RT scene-sideband buffers own bindings 1 through 7. Set 5 is reserved by the common ABI for scene lights.

Only shaders that actually reference `gFrame` require a matching C++ descriptor binding. Pass code should bind it through the reflection-backed `shaderCursor` path, normally via renderer pass builders such as:

```cpp
.uniform("gFrame", context.globalResources.get().frameUniform, "Renderer.GlobalFrameUniforms")
```

`shader/include/sceneTextures.slang` declares the global scene material texture table:

```slang
[[vk::binding(2, 1)]]
public Sampler2D<float4> gSceneTextures[];
```

Set 1 bindings 0 and 1 are reserved for pass-local fixed sampled images, keeping this variable descriptor array at the numerically largest binding in every pipeline that imports `common`. The independent AppUi ABI uses set 1, binding 0 for `gUiTextures[]` because that array is its only set 1 binding.

All material texture types share this single runtime descriptor array. `shader/include/share/materialTextureIds.slang` defines the common `MaterialTextureSlot` order. Production RT materials use one dense texture reference per slot; the legacy packed-pair helpers are test-local to `shader/test/rt/materialTextureIdsRt/abi.slang` and are not part of `common`. Texture ID 0 is the renderer-owned neutral white fallback (1x1 linear RGBA(1,1,1,1)); resident scene texture IDs are renderer-assigned descriptor indices. RT materials store one dense texture reference per slot with UV0/UV1 selection and an identity-default row-major 2x2 affine transform plus offset, so unauthored slots hold ID 0 and shaders always sample (a missing texture multiplies through as a neutral 1 factor, and a missing normal map decodes to tangent-space (0,0,1) via a zero effective normal scale) without a texture-presence branch. An authored but unavailable anisotropy texture is the explicit exception: RT keeps ID 0 and decodes the semantic fallback `(1, 0.5, 1)` instead of treating generic white as anisotropy data. Occlusion R, scalar-specular A, and specular-color RGB reuse this shared lookup path; AO is applied after current-hit direct-light/emissive evaluation and scales only continuation scatter weights. PSOs that consume this table install a nearest immutable sampler clamped to LOD0 during pipeline layout creation, so per-frame descriptor updates provide only image views and layouts. Raster shaders retain their authored texture coordinates but now observe the same nearest scene sampler. RT FAS-off reads sample those transformed coordinates directly at LOD0; FAS-on reads use `material/stochasticTextureFiltering.slang` to select one bilinear reconstruction tap with a semantic-specific scalar, move to that texel center, and perform the same nearest LOD0 fetch. The first-stage implementation has no mip, derivative, or ray-cone path. The PathTracing environment uses a separate linear immutable sampler.

`shader/include/sceneLights.slang` declares the global scene light buffers:

```slang
[[vk::binding(0, 5)]]
public ConstantBuffer<SceneLightGpuHeader> gSceneLightHeader;
[[vk::binding(1, 5)]]
public StructuredBuffer<SceneLightGpuRecord> gSceneLights;
[[vk::binding(2, 5)]]
public StructuredBuffer<SceneLightAliasGpuRecord> gSceneLightAliasTable;
```

`LightPrepareNode` publishes the matching C++ buffers as `frameResource::sceneLightHeader`, `frameResource::sceneLights`, and `frameResource::sceneLightAliasTable`. It prepends a warm default directional sun before scene-authored lights, so PathTracing normally sees at least one positive-energy alias-table entry even when the loaded asset has no lights. The lower-level alias-table helper still supports a valid zero-count header plus one-record dummy list/alias buffers for direct helper use. The same include provides helpers for evaluating glTF punctual `directional`, `point`, and `spot` light values from uploaded photometric records and for sampling the alias table built from Rec.709 luminance times glTF intensity. Point/spot shader evaluation applies inverse-square attenuation and the optional glTF range fade.

RT material shading declarations live in `shader/include/material`. The `[Flags] RtMaterialLayerFlag` value combines four physical layer bits with the `anisotropicBaseLobe` static-shading modifier (`none` == unlit; non-zero masks always contain `baseSurface`). The eight physical lit masks each have an isotropic form without the modifier and an anisotropic form with it. `payload.slang` owns the single material-variant dimension used by its specialized resolvers and BSDF variants; optional lobes remain inside static layer-flag blocks, while `BaseGgxDistribution<LayerFlags>` derives the base reflection/thin-transmission/volume-transmission GGX implementation from the same combined flag value. `pathTracing/chs.slang` exposes only `MaterialCHS<LayerFlags>`, which the PathTracing node binds through the link-time `CHS` alias. The common-visible extern bool `kEnableFilterAfterShading` is assigned only by the closest-hit entry variant and is used as that entry's compile-time filtering policy. It is a closest-hit codegen dimension, but it does not become another CHS generic or material/SBT key dimension. One CHS can sample fourteen shading texture semantics. `payload.slang` and `sampling.slang` map them onto four packets drawn from a by-value `RandomSequence` copy in fixed order, leaving packet 2 W and packet 3 W as padding; `stochasticTextureFiltering.slang` owns the scalar-remapped bilinear tap selection. Raygen advances the live path sequence by the same four packets for every material segment. Optional layers therefore do not shift later random dimensions. Any-hit alpha coverage uses the deterministic non-variant nearest sample and has no FAS input. Anisotropy texture presence, UV selection, affine transform, RG direction, B strength, and scalar rotation remain runtime material data, and clearcoat remains isotropic. Base dielectric reflection combines IOR with `KHR_materials_specular` factor/color data and preserves diffuse energy using the maximum RGB Fresnel component. The PathTracing specular-albedo guide carries the independently resolved F0 and F90 through its split-sum approximation rather than deriving grazing reflectance from F0. Base and clearcoat GGX use joint correlated Smith masking-shadowing. Opaque base reflection and clearcoat construct UE-style `Spec.W` and `Spec.E` from the resource-free analytic split-sum directional-albedo fit: `Spec.W` compensates missing rough microfacet multiple scattering, while `Spec.E` drives diffuse/lower-layer preservation and lobe selection. Active thin and volume transmission deliberately retain their existing energy model because UE applies distinct reflection/transmission and eta-dependent glass compensation. Sheen squares perceptual roughness into Charlie `alpha_g` and attenuates lower layers with a resource-free directional-albedo fit validated against deterministic importance quadrature of the same Charlie visibility model. Base, sheen, and clearcoat reflection keep their independently adjusted shading normals, then push their sampled directions forward onto the view-facing geometry hemisphere by mirroring directions below its tangent plane. Their evaluation and PDF paths sum the exterior direction and mirrored preimage with the same unit solid-angle Jacobian, so the fold preserves the rough-lobe probability and projected estimator kernel instead of absorbing the portion below the geometry normal. Diffuse remains in the raw shading frame, while transmission retains the complementary geometry-boundary test and is not folded. `RtMaterialLayerRecord.layer` remains restricted to the four physical single-bit layer values.

RT resource declarations and hit reconstruction helpers live under `shader/include/rt`. `resources.slang` is an `implementing common;` fragment aggregated by `common.slang` and declares the shared RT scene-sideband bindings, so entry roots obtain that stable global ABI by importing `common` rather than importing the implementing file directly. `hitSurface.slang` remains an importable helper module that depends on ray-tracing builtins and reconstructs hit-surface data from those resources; only RT helper/entry code imports and calls it.

PathTracing has two ray types in one Vulkan RT pipeline. `RtRayType` is a shared generated ABI with `material = 0`, `shadow = 1`, and `count = 2`; every trace call uses those names for SBT offset, geometry stride, and miss index. Material traversal uses the 128-byte `MaterialRayPayload`, `msMaterial`, optional `ahMaterialPolicy`, and a material closest-hit permutation. Visibility traversal uses the four-byte `ShadowRayPayload`, `msShadow`, and the fixed `ahShadow` group, with no closest-hit stage. Both any-hit entries call the payload-independent policy in `anyHitPolicy.slang`, which rejects single-sided back faces and failed alpha masks. Do not reintroduce a shared ray-kind field or numeric `0, 1, 0` routing literals.

## End-to-End Shader Build Pipeline

This section describes the runtime pipeline used by `ShaderService` from source discovery to final SPIR-V generation.

### 1) Session initialization

- Create `IGlobalSession` and `ISession`.
- Configure search paths in fixed order:
  1. `<shader_cache_root>/<compile-options-hash>`
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
  - `<shader_cache_root>/<compile-options-hash>/<module_path>.slang-module`
- Cache directory layout mirrors `shader/` layout.
- Cache keys are path-derived (normalized module identity) plus session context.

### 5) Single-entry composition and link

- Resolve the root module's sole defined entry point and reject zero-entry or multi-entry roots.
- Build the component array from the module, its sole entry point, and only that entry point's deterministic link-time assignment component when one is required.
- Create the composite with `ISession::createCompositeComponentType(...)` and link it into an immutable single-entry program.
- Precompute reflection/layout and Slang's authoritative `getEntryPointHash(0, 0)` on the serialized frontend lane.

### 6) Persistent SPIR-V lookup and backend code generation

- Use the opaque Slang entry-point hash as the persistent target-artifact identity. Do not recreate Slang's dependency, build-version, option, or specialization hash in project code.
- Probe the project-owned persistent SPIR-V cache before requesting backend code generation.
- On a miss, dispatch only `linkedProgram->getEntryPointCode(0, 0)` to the dedicated bounded backend pool, copy the returned blob into project-owned storage on that worker, and publish it to the persistent cache atomically.
- The backend pool defaults dynamically to the device hardware-concurrency limit, capped by `nr::maxThreads`. Configuration may lower the limit, including one worker for diagnostics, but active backend jobs never exceed the configured limit.
- Identical hashes within one `ShaderService` batch/process share one in-flight result, so the local backend pool compiles each artifact at most once. Separate processes may race, but each uses atomic publication and validates the winning artifact instead of relying on a cross-process in-flight registry.

### 7) Batch completion and pipeline construction

- Runtime initialization and the CMake shader checker both submit source-path requests to the same `ShaderService` batch compiler.
- `Renderer::installGraph(...)` first collects the ordered static requirements returned by every node's `shaderRequests()`, submits one flattened batch, and gives each node only its matching `NodeInitContext::shaderPrograms` slice after the whole batch succeeds.
- Frontend load/specialize/composite/link work remains serialized because it shares one Slang session; only fully linked backend code generation runs concurrently.
- `ShaderCompileBatchStats` reports request/memory-hit/persistent-hit/backend-compile/corrupt-entry counts, the effective worker limit, and separate frontend, backend, and total elapsed times so startup diagnostics can distinguish serialized preparation from target codegen and cache I/O. The batch's single info summary additionally reports high-precision `phaseMs`: frontend configuration, request resolution/cache lookup, module load, entrypoint load, variant-module creation, composition, linking, reflection, entry-hash preparation, and publication; it also reports backend wall time plus cache-read/codegen/artifact worker-work sums and maxima. Worker-work totals are aggregate parallel work, not batch wall-clock durations.
- Pipeline and PSO construction starts only after every required single-entry artifact in the batch is ready.
- Scene-derived permutations that cannot be known at graph installation, such as PathTracing closest-hit variants, are collected by their owning node on a pipeline miss and use the same batch-before-PSO rule.
- A multi-file pipeline designates one canonical reflection root for descriptor sets, push constants, and cursor fields: graphics uses the first program in the ordered stage span, compute uses its sole program, and ray tracing passes an explicit reflection program. The canonical root must import and expose the complete global ABI used by every physical stage. `PipelineService` validates descriptor and push-constant coverage against every stage program before PSO creation; shader contract tests also pin the root field names consumed by renderer bindings.
- The CMake checker supplies no alternate compiler path and performs no named-entry selection; it is a thin host for the same batch core used by runtime initialization.

## Cache and Naming Invariants

- One module identity corresponds to one normalized shader-root-relative path.
- Every `.slang` file declares zero or one shader entry point, and every compiled root module exposes exactly one defined entry point.
- `import` identity is always fully qualified dotted name.
- Each `implementing` token must match the primary module it extends; filesystem co-location is not required when the file is attached through `__include`.
- Every module path segment must be lower camelCase (`useFlag`) and cannot contain `_` or `-`.
- Cache artifact path must be the normalized module path with `.slang-module` suffix.
- Search-path order is authoritative for cache-vs-source precedence.
- Linked single-entry variants are process-memory entries invalidated by `ShaderService::reloadSession()` and session reconfiguration. Persistent SPIR-V identity is Slang's opaque entry-point hash and does not include the process-local session generation.

## Cache Layout Rule

Cache layout under `<shader_cache_root>/<compile-options-hash>/` mirrors `shader/` module layout:

- module binary: `<cache>/<module_path>.slang-module`

Persistent backend artifacts use a schema-versioned content-addressed tree inside the same compile-options cache namespace:

- SPIR-V artifact: `<shader_cache_root>/<compile-options-hash>/spirv/v1/<hash-prefix>/<entry-point-hash>.nrspv`

Artifact files carry a project cache magic/schema, the opaque Slang key, payload length, integrity data, and aligned SPIR-V bytes. Reads validate the envelope and SPIR-V magic. Writes use a same-directory temporary file followed by atomic publication so renderer and CMake checker processes can share the cache safely without a central mutable index.

Example:

- source: `shader/main.slang`
- cache: `<cache>/main.slang-module`

