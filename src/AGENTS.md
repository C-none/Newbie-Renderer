# Project C++ Rules

These rules apply to project-owned C++ production code, tests, and tools. Vendored code under `src/extern` is exempt; its project-owned boundary files follow `src/extern/AGENTS.md`.

## Modules and Style

- Organize internal code as named C++ modules. Use `.ixx` for interfaces and keep exported declarations, templates, `constexpr`/`consteval` logic, default arguments, and necessary tiny inline helpers there.
- Split a substantial partition into a matching `.ixx` interface and `.cpp` implementation; move non-template, non-`constexpr` runtime implementation to the `.cpp`. Small declaration-only partitions may remain `.ixx`-only.
- Default to `std::map` and `std::set`. Use unordered containers when the normal size exceeds 50 or a clear measured workload justifies them.
- Loop-versus-ranges choice, `if constexpr` consolidation of near-identical wrappers, and omission of default-valued members during construction follow the root `AGENTS.md` `## Code Simplification` rules.

## Ownership and Lifetime

- Model ownership with RAII. Use `std::unique_ptr` for exclusive ownership and `std::shared_ptr` only when shared ownership is an explicit design requirement; do not use raw owning pointers or `new`/`delete` except in a tightly scoped external wrapper that immediately transfers ownership to RAII.
- Use `T&` or `const T&` for required borrows, `std::reference_wrapper` for stored rebindable borrows, and `std::optional<std::reference_wrapper<T>>` for optional borrows.
- Use `std::weak_ptr` only to observe shared lifetime that is not otherwise guaranteed, and always `lock()` before dereference.
- Do not expose nullable raw pointers for ownership or lifetime control. Raw pointers required by an external C API must stay at that boundary and be converted immediately to a safe local abstraction.

## Dependencies

- Encapsulate third-party non-module C++ libraries at `src/extern` and expose them through narrow `dependency.*` modules. The top-level `dependency` module may remain a compatibility umbrella.
- Outside the dependency boundary, do not include raw third-party headers; import the adapted `dependency.*` module instead.
- Library-versus-reimplementation preference follows the root `AGENTS.md` `## Code Simplification` rules.

## Diagnostics and Exceptions

- Production diagnostics use only `nr.utils:errorHandle` through `nrLog`, `nrVulkan`, and `nrAssert`. Do not add module-local diagnostic facilities; extend `errorHandle` first when new reporting behavior is required.
- Pass a compile-time checked format string followed by its arguments; do not pre-format messages. Pass a runtime message as an argument to a literal format such as `"{}"`.
- Use compile-time `nrLog<Level, Channel>(...)`, with `Channel` defaulting to `"LOG"`; do not add runtime level/channel overloads or a separate `nrInfo` entry point.
- A recoverable external or project condition uses `LogLevel::warning` or lower and follows the call site's explicit local result. Continue only when the failed operation is optional and program state remains valid.
- `nrLog<LogLevel::error>(...)` is fatal regardless of `NR_LOG_LEVEL` and must have no expected continuation. For contextual detail followed by termination, log the detail at warning level and then use `nrAssert(false, ...)`. `nrVulkan<Level>(...)` only forwards validation messages and never terminates.
- Do not use exceptions for in-project control flow. Catch only immediately around an external API known to throw, whether it uses standard or non-standard exceptions; report context at warning level and convert the failure locally into a valid recovery, explicit failure value, or termination. Never silently swallow, rethrow, or wrap an entire function merely to translate an exception.
- Mark internal helpers `noexcept` when their semantics and callees are non-throwing. Convert external exceptions at the immediate boundary instead of weakening correct internal `noexcept` contracts.
