---
name: update-submodules
description: How to update this repository's git submodules. Use when the user asks to update, bump, or sync glTF sample assets, Slang, DLSS, or the pinned Ninja tracing tool.
---

# Update Submodules

Four submodules are recorded in `.gitmodules`:

- `assets/glTF-Sample-Assets` — sample asset data.
- `src/extern/slang` — Slang compiler; has its own nested submodules.
- `src/extern/DLSS` — NVIDIA DLSS SDK and runtime feature binaries.
- `tools/ninjatracing` — pinned Ninja-log to Chrome-trace converter.

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

## 3. DLSS — checkout an explicitly selected release

DLSS is a release input to the tracked MSVC bridge and feature-DLL deployment flow. Do not
float it to the remote branch tip. Fetch tags, inspect the available formal releases, and
checkout only the release requested by the user:

```powershell
cd src/extern/DLSS
git fetch --tags --prune
git tag --list "v*" --sort=-v:refname
git checkout <reviewed-release-tag>
git submodule update --init --recursive
cd ../../..
```

After changing the pin, follow the repository's bridge publication and validation policy;
moving the gitlink alone does not make the tracked bridge compatible with a new SDK.

## 4. Ninja tracing — pinned commit only

`tools/ninjatracing` is deliberately pinned. Never run
`git submodule update --remote tools/ninjatracing` and do not make it track a floating
remote branch. Update it only when the user supplies or approves a reviewed commit:

```powershell
cd tools/ninjatracing
git fetch origin
git checkout <reviewed-commit>
cd ../..
```

## 5. Record the bump in the superproject

Updating submodules moves their gitlink; commit it in this repo:

```powershell
git add assets/glTF-Sample-Assets src/extern/slang src/extern/DLSS tools/ninjatracing
git commit -m "chore: bump submodules"
```
