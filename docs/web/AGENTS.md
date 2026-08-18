# Web Agent Instructions

These instructions apply to the complete `web` subtree.

## Self-contained resource boundary

- Treat this directory as an independent repository and deployment unit.
- Never import, read, copy, serve, or otherwise depend on files outside this
  directory at build time, test time, or runtime.
- Do not use parent-directory paths that escape this directory, absolute host
  filesystem paths, `/@fs/` URLs, external-path symlinks, or Vite
  `server.fs.allow` entries for parent directories.
- Keep every image, font, shader, model, media file, fixture, and other static
  resource used by the application inside this directory.
- Do not load runtime assets from third-party URLs or CDNs. Ordinary navigation
  links and documentation links are allowed, but they must not be application
  resource dependencies.
- `assets/exr/chess.exr` is a web-owned Git LFS asset. Files under the parent
  repository's `assets` directory belong to the C++ application and are never
  web resources.

## Toolchain and verification

- Use the Node.js version declared in `.node-version` and the exact pnpm version
  declared by `packageManager` in `package.json`.
- Update `pnpm-lock.yaml` whenever dependencies change.
- Before handing off changes, run `pnpm lint` and `pnpm build` from this
  directory.
- A production build must contain only resources owned by this directory.
