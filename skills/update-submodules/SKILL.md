---
name: update-submodules
description: Safely discover, update, bump, or sync this repository's Git submodules while preserving local work and repository version contracts. Use for glTF sample assets, Slang, DLSS, Ninja tracing, or newly added submodules.
---

# Update Submodules

## 1. Discover and review

Always derive the current inventory from `.gitmodules`; do not assume a fixed list:

```powershell
git config --file .gitmodules --get-regexp '^submodule\..*\.path$'
git diff --submodule=log
git status --short
```

Stop before updating any submodule with local modifications or a pre-existing gitlink
change that is outside the requested work. Never discard, clean, or overwrite that work.

The currently known policies are:

- `assets/glTF-Sample-Assets` — track its configured remote branch.
- `src/extern/slang` — use the latest formal release tag.
- `src/extern/DLSS` — use the repository-compatible explicit release.
- `tools/ninjatracing` — use only an approved commit.

For every path not covered above, inspect its `.gitmodules` URL/branch settings and search
the superproject for consumers and version contracts. Choose an explicit pin or tracking
policy with the user, then add a dedicated section to this skill before updating it.

## 2. Asset — track the configured remote

Initialize the submodule and update it to the tip selected by its configured branch or the
remote default branch:

```powershell
git submodule update --init --remote -- assets/glTF-Sample-Assets
```

Do not manually checkout `main` unless the remote and repository policy have been verified
to use `main`; it is not universally equivalent to `git submodule update --remote`.

## 3. Slang — checkout latest formal tag, then recursive init

Formal release tags use `v{year}.{version}` (e.g. `v2024.1.34`). Exclude
pre-release tags (`-rc`, `-pre`, `-test`, old `v0.x`).

```powershell
cd src/extern/slang

# Fetch tags first.
git fetch --tags --prune --force

# Pick latest formal tag (year-based, no pre-release suffix).
$latest = git tag --list "v20*" --sort=-v:refname |
  Where-Object { $_ -match '^v\d{4}\.\d+(\.\d+)?$' } |
  Select-Object -First 1

if ([string]::IsNullOrWhiteSpace($latest)) {
  throw "No formal year-based Slang release tag was found."
}

# A tag checkout intentionally leaves the submodule in detached-HEAD state.
git checkout --detach $latest

# Slang pulls many nested submodules (glslang, spirv-tools, ...) — init recursively.
git submodule update --init --recursive

cd ../..
```

## 4. DLSS — keep the SDK and bridge contract synchronized

DLSS is a release input to the tracked MSVC bridge and feature-DLL deployment flow. Do not
float it to the remote branch tip. The directly supported pin is currently `v310.7.0`,
matching the SDK and bridge `310.7.0` contract in `CMakeLists.txt`,
`cmake/NrDlssBridgeArtifact.cmake`, and `cmake/PublishDlssBridge.cmake`:

```powershell
cd src/extern/DLSS
git fetch --tags --prune
git checkout --detach v310.7.0
git submodule update --init --recursive
cd ../../..
```

Treat any requested DLSS version other than `v310.7.0` as a coordinated upgrade. Update and
validate the bridge publication, artifact manifest validation, and CMake SDK version
contract together; never move only the DLSS gitlink.

## 5. Ninja tracing — pinned commit only

`tools/ninjatracing` is deliberately pinned. Never run
`git submodule update --remote tools/ninjatracing` and do not make it track a floating
remote branch. Update it only when the user supplies or approves a reviewed commit:

```powershell
cd tools/ninjatracing
git fetch origin
git checkout <reviewed-commit>
cd ../..
```

## 6. Review and record only the requested bumps

Review both submodule contents and superproject gitlinks before staging:

```powershell
git status --short
git diff --submodule=log
```

Stage only the gitlink paths intentionally changed in this task. Never use a fixed all-
submodule list, because it can stage unrelated user changes:

```powershell
git add -- <intentionally-updated-submodule-path> [additional-reviewed-paths...]
git diff --cached --submodule=log -- <intentionally-updated-submodule-path> [additional-reviewed-paths...]
git commit -m "chore: bump submodules"
```
