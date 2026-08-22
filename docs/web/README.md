# Web

The renderer's single-page showcase. It presents real captures, evidence-scoped
capabilities, architecture, and the roadmap at `/`. Detailed
content expands in place with native disclosure panels; this package has no
secondary application route.

## Site content

- `src/data/project.js` owns support copy, case studies, capabilities,
  architecture, and roadmap status.
- `src/data/gallery.js` owns capture metadata and pairs each lightweight poster
  with its web-owned OpenEXR source.
- `src/pages/index.vue` renders the only page: `#gallery`, `#capabilities`,
  `#architecture`, and `#roadmap`.
- `src/components/ExrViewer.vue` shows a poster first. It downloads and decodes
  the selected OpenEXR only when the visitor starts the HDR inspector.
- `public/og.png` is the repository and social preview. `public/robots.txt` and
  `public/sitemap.xml` describe the same canonical page.

The supported user target is deliberately narrow: Windows on NVIDIA Ada or
Blackwell. Do not broaden the copy to generic RTX, NVIDIA, Vulkan, or desktop
hardware without a corresponding project support-policy change.

## Capture posters

The OpenEXR files are the source images. Regenerate the checked-in ACES Filmic
PNG posters after replacing a capture:

```sh
pnpm generate:posters
```

The generator lives in `tools/generate-gallery-posters.mjs`; it uses the local
three.js EXR decoder and does not fetch runtime assets.

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
