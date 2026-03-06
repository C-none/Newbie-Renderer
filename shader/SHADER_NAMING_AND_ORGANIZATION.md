# Shader Naming and Organization Guide

This document defines the enforced Slang module/file organization used by `ShaderService`.

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

