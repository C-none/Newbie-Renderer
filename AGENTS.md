# Newbie-Renderer Agent Rules

This file contains repository-wide policy. More specific `AGENTS.md` files extend it for their directories; read the routed file before modifying a sibling test or tool.

## Rule Routing

- Before modifying project-owned C++ production code, tests, or tools, read `src/AGENTS.md`.
- Before modifying RHI/Vulkan code or tests, also read `src/rhi/AGENTS.md`.
- Renderer pass-builder work additionally reads `src/renderer/AGENTS.md`; render-pass node or test work reads `src/renderPasses/AGENTS.md`.
- Shader source, compiler, checker, or contract-test work additionally reads `shader/AGENTS.md`; checker implementation also reads `tools/AGENTS.md`.
- Dependency-boundary or vendored-source work reads `src/extern/AGENTS.md`.
- Tests read `test/AGENTS.md`; the architecture README and architecture topic documents read `docs/architecture/AGENTS.md`.

## Project and Toolchain

- Use C++26. Write comments in project-owned code in English.
- LLVM Clang++ selected by the `llvm` CMake preset is the supported full-project toolchain. Project code may use LLVM/Clang capabilities and must not be weakened solely for another host compiler.
- Native MSVC is best-effort only. Do not add MSVC compatibility branches or substitutes; the dedicated MSVC DLSS bridge publisher is an external ABI exception.
- Treat `src/extern` as vendored source: exclude it from routine discovery and broad searches unless the task concerns dependency integration, vendored patches, external build wiring, or a diagnostic originating there.
- Build, parse, serialize, and structurally edit dynamic JSON only through a JSON library: `dependency.json` (Boost.JSON) in C++, `string(JSON)` in CMake, and a standard or established library in host tools. Never hand-build or regex-parse dynamic JSON.

## Code Simplification

- Run simplification as a distinct pass after a change is functionally correct and verified, never while the behavior is still being established. Deliver only after the simplified result passes verification again.
- Preserve external behavior, public interfaces, and diagnostics. Work that changes observable behavior is a feature change and follows the normal implementation and verification flow instead.
- Remove duplication first: give shared logic one owner, reuse the existing shared core named by the routed `AGENTS.md`, and prefer a mature third-party library or tool over a project-local reimplementation unless a clear measured reason justifies owning the behavior.
- Prefer standard-library and modern C++26 facilities to hand-written equivalents. Prefer ranges and views to raw loops when they remain clear and are not performance-critical; use a loop when a ranges formulation is materially more complex or a measured hot path requires it.
- When call paths differ only by small compile-time constants, prefer one non-type-template-parameter implementation with `if constexpr` over near-identical wrappers.
- Reduce nesting and cognitive complexity with early returns, guard clauses, and small single-responsibility helpers. Rename an identifier when its current name no longer describes its responsibility.
- Delete redundant code instead of commenting it out or wrapping it. During construction, omit members whose values equal their default member initializers, including empty optional or nullable members; use `{}` when a stored optional-like member needs an explicit empty default. Assign an explicit empty value only to clear an existing object, define an API default, disambiguate an overload, document an external boundary, or intentionally reset state.
- Remove abstraction, indirection, dispatch, or wrapper layers that carry no current behavior. Do not introduce a new abstraction or a parallel system during simplification; route new shared behavior into the existing owning facility.

## Verification

- Closed-loop acceptance uses LLVM Debug: `cmake --build --preset debug`, `ctest --preset debug`, or an equivalent targeted LLVM Debug build/test.
- After a simplification pass, rebuild the affected LLVM Debug targets and rerun their tests. Run full `ctest --preset debug` when the pass touched shared code, public interfaces, or diagnostics.
- Run Release validation only when explicitly requested. It is additional unless the user expressly requests Release-only verification and thereby waives Debug acceptance.
- Unless Debug acceptance was explicitly waived, report an LLVM Debug environment, toolchain, file-lock, or permission blocker and the affected command instead of substituting MSVC or Release.
- In managed or sandboxed Windows sessions, request elevated execution before launching project programs, smoke tests, or tests that invoke external compiler or shader-toolchain executables.
- Let configure and build commands finish, fail, or time out before inspecting processes, locks, or build directories; do not poll them repeatedly while they run.

## Architecture Maintenance

- In the same patch, update `docs/architecture/README.md` and affected topic documents when a core module responsibility, primary cross-layer data flow, stable runtime boundary, resource ownership boundary, or major dependency placement changes.
