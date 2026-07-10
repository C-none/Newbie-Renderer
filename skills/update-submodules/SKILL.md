---
name: update-submodules
description: How to update this repository's git submodules. Use when the user asks to update, bump, or sync submodules — the asset submodule (assets/glTF-Sample-Assets) or the Slang submodule (src/extern/slang).
---

# Update Submodules

Two submodules (see `.gitmodules`):

- `assets/glTF-Sample-Assets` — sample asset data.
- `src/extern/slang` — Slang compiler; has its own nested submodules.

## 1. Asset — update to HEAD

Update straight to the remote default branch tip:

```powershell
git submodule update --remote assets/glTF-Sample-Assets
```

Equivalent manual form:

```powershell
cd assets/glTF-Sample-Assets
git fetch origin
git checkout main
git pull --ff-only
cd ../..
```

## 2. Slang — checkout latest formal tag, then recursive init

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

git checkout $latest

# Slang pulls many nested submodules (glslang, spirv-tools, ...) — init recursively.
git submodule update --init --recursive

cd ../..
```

## 3. Record the bump in the superproject

Updating submodules moves their gitlink; commit it in this repo:

```powershell
git add assets/glTF-Sample-Assets src/extern/slang
git commit -m "chore: bump submodules"
```
