# Web

The showcase page for the renderer. It presents the supported features, the
todo list, and a browser EXR tone mapping demo.

## Site content

- `src/data/project.js` owns the feature cards and todo entries. It is the source
  of truth for both lists; the repository README only links to this page.
- `src/pages/index.vue` renders the hero, `#features`, `#roadmap`, and `#demo`
  sections.
- `src/components/ExrViewer.vue` owns the three.js EXR viewer used by `#demo`.

## Local deployment

This directory is a self-contained package. Every `pnpm` command below must run
inside `docs/web`; running them from the repository root fails with
`ERR_PNPM_NO_PKG_MANIFEST` because the root has no `package.json`.

Install Git LFS and Node.js 24, then:

```sh
git lfs install
git lfs pull
cd docs/web
corepack enable
corepack prepare pnpm@11.22.0 --activate
pnpm install --frozen-lockfile
pnpm build
pnpm serve --host
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
