# Shader Rules

- Project-owned shader sources use Slang and compile to SPIR-V. Do not add direct HLSL or GLSL compilation paths; imported sources require an approved Slang translation path.
- A shared/library `.slang` module declares no stage entry point and must not be submitted as a stage compilation unit.
- Every `.slang` file submitted as a stage compilation unit declares exactly one stage entry point. Compilation and variants remain file-scoped; do not add named-entry-point selection or bundle multiple stage entry points in one file.

## Validation

- After configuring the LLVM build, validate an isolated default-variant shader through the runtime-equivalent checker:

  ```powershell
  cmake "-DNR_SHADER_FILE=renderer/embeddedTriangle/vertex.slang" -P tools/CheckSlangShader.cmake
  ```

- The input may be absolute or relative to the configured shader root, and the `.slang` extension may be omitted. The checker defaults to `build/llvm` and LLVM Debug.
- The checker uses an empty/default `SlangProgramVariantDesc`. A shader that requires link-time constants or type assignments must use its variant-aware contract test.
- This checker is a focused pre-check, not a replacement for registered reflection, descriptor, cursor, ABI, variant, cache, SPIR-V, pipeline/SBT, or GPU-runtime contract tests.
