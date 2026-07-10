---
name: update-submodules
description: How to update this repository's git submodules. Use when the user asks to update, bump, or sync submodules — the asset submodule (assets/glTF-Sample-Assets), the Slang submodule (src/extern/slang), or the DLSS submodule (src/extern/DLSS).
---

# Update Submodules

Three submodules (see `.gitmodules`):

- `assets/glTF-Sample-Assets` — sample asset data.
- `src/extern/slang` — Slang compiler; has its own nested submodules.
- `src/extern/DLSS` — NVIDIA DLSS SDK; has a nested submodule (`NVIDIAImageScaling`).

All commands run from the repository root unless noted. Verify with
`git submodule status` before and after.

## 1. Asset — update to HEAD

`assets/glTF-Sample-Assets` tracks the upstream `main` tip directly; no tag
pinning, no nested submodules.

```powershell
git submodule update --remote assets/glTF-Sample-Assets
```

## 2. Slang — latest formal tag, then recursive init

`src/extern/slang` is pinned to a formal release tag (e.g. `v2026.13`), not the
`master` tip. It has nested submodules, so after checking out the tag they must
be synced recursively.

```powershell
cd src/extern/slang
git fetch --tags --prune --force
# Pick the latest tag matching vYYYY.N (or vYYYY.N.M).
$tag = git tag --list 'v20*' --sort=-v:refname |
    Where-Object { $_ -match '^v\d{4}\.\d+(\.\d+)?$' } |
    Select-Object -First 1
git checkout $tag
git submodule update --init --recursive
cd ../..
```

## 3. DLSS — update to HEAD, then recursive init

`src/extern/DLSS` tracks the upstream `main` tip directly (no pinned release
tag). Update the top-level gitlink to HEAD first, then sync the nested
submodule (`NVIDIAImageScaling`) recursively.

```powershell
# 1. Move the DLSS gitlink to the remote default-branch tip.
git submodule update --remote src/extern/DLSS

# 2. Sync nested submodules recorded at the new HEAD.
git submodule update --init --recursive src/extern/DLSS
```

Equivalent manual form:

```powershell
cd src/extern/DLSS
git fetch origin
git checkout main
git pull --ff-only
git submodule update --init --recursive
cd ../..
```

## 4. Record the bump in the superproject

Updating submodules moves their gitlink; commit it in this repo:

```powershell
git add assets/glTF-Sample-Assets src/extern/slang src/extern/DLSS
git commit -m "chore: bump submodules"
```
