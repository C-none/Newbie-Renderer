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

## Verification

- Closed-loop acceptance uses LLVM Debug: `cmake --build --preset debug`, `ctest --preset debug`, or an equivalent targeted LLVM Debug build/test.
- Run Release validation only when explicitly requested. It is additional unless the user expressly requests Release-only verification and thereby waives Debug acceptance.
- Unless Debug acceptance was explicitly waived, report an LLVM Debug environment, toolchain, file-lock, or permission blocker and the affected command instead of substituting MSVC or Release.
- In managed or sandboxed Windows sessions, request elevated execution before launching project programs, smoke tests, or tests that invoke external compiler or shader-toolchain executables.
- Let configure and build commands finish, fail, or time out before inspecting processes, locks, or build directories; do not poll them repeatedly while they run.

## Architecture Maintenance

- In the same patch, update `docs/architecture/README.md` and affected topic documents when a core module responsibility, primary cross-layer data flow, stable runtime boundary, resource ownership boundary, or major dependency placement changes.
