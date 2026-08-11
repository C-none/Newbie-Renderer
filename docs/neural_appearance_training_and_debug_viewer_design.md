# Neural Appearance Training and Debug Viewer Design

Status: superseded platform-sized proposal; preserved for historical context.

The implemented, deliberately smaller contract is
[Native GPU Neural Appearance Training](neural_appearance_gpu_training.md). The earlier
[Single-Scale Neural Specular Evaluation MVP](neural_appearance_mvp.md) is retained as a
historical NumPy golden. The sections below are proposal history, not current runtime
behavior or acceptance requirements.

This document defines the first training and visual-validation boundary for neural
appearance. It does not describe production renderer behavior.

Primary references:

- NVIDIA [Real-Time Neural Appearance Models](https://research.nvidia.com/labs/rtr/neural_appearance_models/);
- [SIGGRAPH 2026 Neural Shading course](https://shader-slang.org/landing/siggraph-26/)
  and [course code](https://github.com/shader-slang/neural-shading-s26);
- NVIDIA [Neural Appearance training pipeline](https://github.com/NVlabs/neuralappearance);
- the implemented
  [interaction contract](agent_interaction_and_automation_design.md).

## 1. V0 Boundary

| Concern | Decision |
|---|---|
| Host | Python handles configuration, dispatch, checkpoints, metrics, and publication |
| GPU code | Project-authored GPU work uses only Slang through Vulkan/SlangPy |
| Trainable state | One latent texture and one compact reflection decoder are trained together |
| Network | Four latent channels and a fixed two-hidden-layer, width-16 decoder |
| Target | Current base-surface `specularProjected` reflection |
| Latent access | One filtered sample from one `RGBA16F` image per shading vertex |
| Cached state | A direction-independent `NeuralMaterialContext`; UV is discarded |
| Sampling | Existing analytic GGX proposal and PDF |
| Asset | `SpecGlossVsMetalRough` metallic-roughness material |
| Validation | Standalone tiled-plane debug viewer |
| Production | No scene, material-table, path-tracer, or production payload integration |

V0 covers opaque isotropic base reflection, mip zero, linear HDR output, FP32 master
training state, and a portable FP16 artifact. Encoder baking, latent MIPs, learned frames,
normal maps, anisotropy, layered lobes, neural importance sampling, and formal renderer
integration are deferred.

## 2. Neural and Payload Contract

Here, **vertex** means a path/shading vertex or surface interaction, not a mesh vertex.
The contract is:

~~~text
UV + LOD
  -> sample RGBA16F latent z once
  -> discard UV
  -> build direction-independent context once
  -> evaluate(context, V, L) for each directional query
~~~

`sampleLatent()` must be invoked once for every valid surface interaction and compile to
one shader-visible image-sampling instruction. Hardware filtering may access neighboring
texels internally; the contract counts shader sampling operations, not cache transactions.
An eight-channel latent stored in two RGBA images would violate this V0 rule.

The first decoder layer is split as:

~~~text
firstLayer([z, d]) = Wz * z + Wd * d + b
spatialPrefix      = Wz * z + b
directionalQuery   = activation(spatialPrefix + Wd * encodeDirection(V, L))
~~~

`NeuralMaterialContext` stores the 16-wide FP16 `spatialPrefix` plus schema/model tags.
The prefix is direction-independent and costs at least 32 bytes. Final RGB, PDF, sampled
direction, and other direction-dependent results must not be cached as reusable context.

The authoritative output is an absolute, finite, non-negative RGB value:

~~~text
target(uv, V, L) =
    BaseSurfaceBsdfLobe.evaluate(material(uv), V, L).specularProjected
~~~

This keeps the current shading-normal cosine, reflection folding, Fresnel, GGX,
masking-shadowing, and energy-compensation semantics. Training and validation must share
one entry-point-free Slang reference helper. The analytic GGX sampler/PDF remains the
proposal; V0 replaces only reflection evaluation.

The standalone viewer uses a tool-local `DebugSurfacePayload` containing native surface
state, `NeuralMaterialContext`, and a debug-only copy of sampled `z[4]`. The current
128-byte production `ResolvedMaterialRayPayload` remains unchanged. Its future expansion
or repacking is a separate measured decision.

## 3. Training and Initial Asset

| Python owns | Slang on Vulkan owns |
|---|---|
| CLI/configuration | UV and direction sampling |
| Resource and dispatch organization | Source texture and reference PBR evaluation |
| Checkpoint/artifact I/O | Latent filtering and network forward pass |
| Metric readback and plots | Reverse AD, gradient accumulation, loss, and Adam |

Python must not generate the per-sample reference dataset or run GPU math through CUDA,
PyTorch, CuPy, JAX GPU, or another project-authored GPU language.

Training uses:

- directly optimized four-channel latent values and a `2 x 16` decoder;
- FP32 master latent, weights, biases, and Adam state;
- FP16 forward/deployment values where required;
- stable half/difference direction encoding;
- mapped L1 in linear HDR space;
- single-instance bring-up, followed by the SIGGRAPH 2026 `64 -> 16 -> 4 -> 1`
  schedule before declaring quality stability.

Differentiable bilinear filtering is an implementation gate: latent gradients require a
reviewed custom load/scatter derivative. CoopVec training layouts likewise require the
official primitives or explicit derivatives.

Implementation starts with a Slang/SlangPy/CoopVec probe that pins exact revisions,
checks compiler compatibility, queries device layout metadata, and proves scalar/CoopVec
agreement from one canonical row-major weight set. Shared Slang modules live under
`shader/include/neuralAppearance/`; training entry points and Python orchestration live
under `shader/training/neuralAppearance/` and `tools/neuralAppearance/`.

The initial fixture is
[`SpecGlossVsMetalRough`](../assets/glTF-Sample-Assets/Models/SpecGlossVsMetalRough/glTF/SpecGlossVsMetalRough.gltf).
Its metallic-roughness half is the only V0 training and native A/B reference because it
maps directly to the current `BaseSurfaceBsdfLobe`. The matched specular-glossiness half
is useful for offline diagnostics but must not be mixed into labels without a separately
defined and tested conversion/reference. Emissive and normal mapping are disabled.

## 4. Checkpoints and Portable Artifacts

A recoverable checkpoint retains FP32 master state, Adam moments, seeds, training
configuration, population state, and loss history.

The portable viewer artifact contains:

~~~text
manifest.json
weights.bin
latent.exr
metrics.json
~~~

The manifest fixes schema/version, endianness, dtype, alignment, content hashes, latent
shape/filter rules, network topology, row-major offsets, output activation/scale,
direction/frame conventions,
`NeuralMaterialContext` layout, output semantics, supported material flags, and
Slang/training revisions. Device-specific CoopVec layouts, descriptors, handles, and
native struct images are never persisted.

All quality thresholds are rerun on the final FP16 latent and FP16 weights. `metrics.json`
records both the FP32 baseline and FP16 result; a failing quantization delta requires
quantization-aware training or retraining.

Artifacts are immutable content-addressed generations under a fixed root such as
`build/neuralAppearance/artifacts/<hash>/`. Publication atomically replaces only a small
same-volume `current.json` pointer, not a non-empty directory.

Before allocation or upload, the loader enforces root containment, rejects traversal and
reparse-point escapes, validates bounded dimensions/offset arithmetic and exact lengths,
then verifies hashes. Each loaded generation is one RAII bundle. Switching occurs at a
frame boundary; older GPU resources remain alive until all referencing frame fences
retire. A failed load preserves the current valid generation.

## 5. Standalone Debug Viewer

The proposed `tools/neuralMaterialViewer/` executable reuses the existing Vulkan device,
render graph, camera, Dear ImGui, OptionSystem, WebSocket/Lua adapters, Present, and EXR
capture. It does not use the scene importer, TLAS/BLAS, production material tables, SBT,
or path tracer.

~~~text
NeuralMaterialPrepareNode
    -> NeuralMaterialCompareNode
    -> UiNode
    -> PresentNode
~~~

A fullscreen dispatch intersects camera rays with a mathematical plane and repeats the
material textures across it. Existing mouse-look and free-flight controls provide
interactive viewpoints.

The prepare stage computes plane UV, samples native PBR textures, performs the single
latent sample, builds the context, and writes `DebugSurfacePayload`. The compare stage
receives no UV and binds no source or latent texture; it evaluates native and neural
reflection from the same payload and directions.

Required views are native, neural, full-base comparison, split/wipe, flicker, absolute or
relative error, latent channel, and negative/non-finite highlighting. Metrics and
differences are computed in linear HDR before exposure and tone mapping. Camera, light,
tile scale, view mode, split, error gain, latent channel, reload, and EXR capture are
options.

Interaction follows the existing contract:

- `human`: mouse/keyboard and Dear ImGui;
- `agent`: authenticated loopback WebSocket;
- `offline-lua`: one allowlisted Lua coroutine.

These mutation authorities are mutually exclusive. WebSocket controls options and
capture requests; it does not stream framebuffer data or return capture bytes.

## 6. Validation and Delivery

Validation must cover:

- **Shader/payload:** one SPIR-V image-sampling instruction plus dynamic one-call
  instrumentation, no post-prepare UV, no compare-stage material textures, FP16 context
  round-trip, and cached-context equivalence across multiple directions.
- **Numerical:** mapped/relative error, P95/max, roughness/F0/grazing strata,
  non-finite/negative counts, reciprocity, hemispherical energy, and repeat seams.
- **Artifact/lifetime:** final-FP16 quality, malformed/oversized/out-of-root rejection,
  hash verification, failed-reload preservation, and fence-delayed generation retirement.
- **Visual:** native/neural/split/flicker/error/latent views plus deterministic EXR camera
  and light sweeps through local, WebSocket, and Lua control.

| Phase | Result |
|---|---|
| 0. Toolchain | Pin SlangPy/Slang, validate CoopVec, freeze ABI |
| 1. Shared shader | Reference helper, latent sampler, split decoder, contract tests |
| 2. Training | Joint latent/decoder training and atomic FP16 artifact |
| 3. Viewer | Prepare/compare graph, UI, hot reload, visual modes, EXR |
| 4. Automation | WebSocket/Lua sweeps and numerical/image regression thresholds |
| 5. Future gate | Measure production payload/register cost before renderer integration |

V0 is accepted when:

1. Python launches a Vulkan Slang training job with no second GPU language.
2. The latent and decoder converge on the authoritative metallic-roughness reflection.
3. Every valid plane interaction executes one latent image-sampling instruction and
   transports reusable direction-independent context without UV.
4. The final FP16 artifact passes numerical, visual, integrity, and reload tests.
5. Native/neural/error views work through human, WebSocket-agent, and offline-Lua modes.
6. The production renderer and production ray payload remain unchanged.

When production integration is implemented, update the architecture overview and affected
runtime documents in the same change.
