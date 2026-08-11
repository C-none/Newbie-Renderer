# Architecture Documentation Rules

The root `AGENTS.md` defines when code changes require an architecture update. These rules also apply to architecture topic documents routed here by the root file.

- Keep `README.md` concise and architectural; move detailed records, enums, algorithms, and implementation contracts to topic documents or source-of-truth code and link them.
- Preserve the overview order: `rhi`, `load`, `resources`, `scene`, `renderer`, `renderpasses`, `Overall`.
- Describe current code and accepted boundaries, not abandoned plans or speculative designs. When evidence supports only a narrower claim, write the narrower claim.
- Use terminology that matches the code and topic documents. Every new or changed stable boundary must have at least one concrete code reference.
- Update only the affected scope, but re-check adjacent layers so the end-to-end flow remains coherent.
- Keep coding policy in `AGENTS.md` rather than duplicating it in the architecture overview.

Before finishing an architecture-relevant change, verify that the main flow still matches the code, changed boundaries have code references, and linked topic documents remain aligned.
