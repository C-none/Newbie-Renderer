# Web

## Local deployment

Install Git LFS and Node.js 24, then run the following commands from this
directory:

```sh
git lfs install
git lfs pull
corepack enable
corepack prepare pnpm@11.22.0 --activate
pnpm install --frozen-lockfile
pnpm build
pnpm serve -- --host
```

For development with hot reload, run `pnpm dev` instead.

## GitHub Pages deployment

This project is deployed at the fixed GitHub Pages project path `/web/`. Its
public URL has the following form:

```text
https://<GitHub-account>.github.io/web/
```

Configure the repository as follows:

1. Open `Settings > Secrets and variables > Actions`.
2. Create a repository secret named `VITE_BASE_PUBLIC_PATH` with this exact
   value:

   ```text
   /web/
   ```

3. Push the project to the `main` branch. The workflow at
   `.github/workflows/cd.yml` builds the site and publishes it to the
   `gh-pages` branch.
4. Open `Settings > Pages`, select `Deploy from a branch`, choose the
   `gh-pages` branch and the `/ (root)` directory, and save the settings.

After the workflow completes, open the URL shown above and replace
`<GitHub-account>` with the repository owner's GitHub account name.
