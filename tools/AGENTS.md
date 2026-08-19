# Tooling Rules

- `CheckSlangShader.cmake` and `nrShaderCompileCheck.cpp` must reuse `ShaderService::configure()`, the shared `compileProgramsByFile()` batch core, and `SlangProgram::valid()` so the check follows the renderer's single-entry link, reflection, cache, and bounded backend-worker path.
- Preserve `NR_SHADER_FILE`/`NR_SHADER_FILES` compatibility and on-demand construction of the `EXCLUDE_FROM_ALL` `nr_shader_compile_check` target; path and default-configuration behavior is defined in `shader/AGENTS.md`.
- The checker follows the single Slang compilation path and the no-named-entry-point-selection rule defined in `shader/AGENTS.md`, together with its default-variant limitation and required contract-test coverage documented there.
