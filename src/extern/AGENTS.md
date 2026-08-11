# Vendored Dependency Boundary

- Treat third-party contents as vendored: do not apply project style refactors, formatting sweeps, or routine broad searches to them.
- Enter or edit vendored code only for explicit dependency integration, external build wiring, a concrete vendor patch, or a diagnostic originating there.
- Project-owned boundary files, including dependency adapters and integration build files, continue to follow the root and `src/AGENTS.md` policies.
- Expose third-party functionality through narrow `dependency.*` modules; do not place project business or renderer logic in vendored trees.
