# Web

## Local deployment

Install Git LFS and Node.js 24, then run these commands from `docs/web`:

```sh
git lfs install
git lfs pull
corepack enable
corepack prepare pnpm@11.22.0 --activate
pnpm install --frozen-lockfile
pnpm build
pnpm serve -- --host
```

Open the local URL printed by Vite. For development with hot reload, run
`pnpm dev` instead.

## GitHub Pages deployment

The site uses the fixed repository path `/Newbie-Renderer/` and is published at:

```text
https://c-none.github.io/Newbie-Renderer/
```

The Vite base path is fixed in `vite.config.js`; no repository secret or
environment variable is required.

To enable deployment:

1. Open the `Newbie-Renderer` repository on GitHub.
2. Go to `Settings > Pages`.
3. Under `Build and deployment`, set `Source` to `GitHub Actions`.
4. Push the `master` branch. The root workflow at
   `.github/workflows/deploy-web.yml` installs, lints, builds, and deploys
   `docs/web`.
5. Open the URL above after the `Deploy web to GitHub Pages` workflow succeeds.

The workflow can also be started manually from the repository's `Actions` tab.
