# Native GPU Neural Appearance Training

Status: current scaled minimal implementation for the standalone neural-material viewer.

This is the repository's smallest native Vulkan/Slang reproduction of the training and
evaluation core from NVIDIA's neural appearance work. Training, packing, and inference
run as Slang compute shaders through the existing RHI. The implementation is tool-only:
it does not change scene materials, the production ray payload, SBT selection, or
PathTracing.

This is deliberately not the paper's training scale. The live MVP evaluates 64 decoder
samples per logical step for 32,768 steps, or 2,097,152 decoder samples. The paper uses
65,000 decoder samples per iteration for 300,000 iterations, or 19.5 billion decoder
samples, plus a separate sampler batch. The MVP also omits the paper's hierarchical
latent and learned importance-sampling system.

Primary references:

- NVIDIA [Real-Time Neural Appearance Models](https://research.nvidia.com/labs/rtr/neural_appearance_models/)
- repository paper copy at
  [paper/nvidia_neural_materials_author_paper.pdf](paper/nvidia_neural_materials_author_paper.pdf)
- the superseded offline baseline in
  [neural_appearance_mvp.md](neural_appearance_mvp.md)

## Implemented boundary

| Concern | Current decision |
|---|---|
| Execution | Native Vulkan compute through `nr.rhi` and `ShaderService` |
| Shader roots | Six: initialize, target, gradient, optimizer, pack, and viewer |
| Training | A parallel reference-target reduction followed by one Slang reverse-AD invocation and one disjoint gradient row per decoder sample |
| Logical budget | Batch 64 and 32,768 one-based logical steps |
| Display cadence | One active training triple per new display ordinal through step 2,048; up to eight afterward |
| Optimizer | Bias-corrected Adam, beta1 0.9, beta2 0.999, epsilon `1e-7`, no weight decay |
| Model schedule | Cosine learning rate from `1e-3` to `1e-4` across the logical budget |
| Latent schedule | Frozen through step 2,048; afterward uses 0.1 times the model learning rate |
| Master state | Device-resident FP32 model, latent, and Adam first/second moments |
| Latent | Periodic `64 x 64 x 8`, represented as two `float4` values per texel |
| Deployment latent | Two repeat/bilinear `RGBA16F` images, exactly one mip, explicitly sampled at LOD 0 |
| Direction input | Ordered local light and view vectors in two learned frames, 12 scalars |
| Decoder | `z8 + d12 -> 32 ReLU -> 32 ReLU -> RGB exp(logit - 3)` with an `RGBA16F`-safe upper cap |
| Target | Tool-local anisotropic `BaseSurfaceBsdfLobe::evaluate(...).specularProjected` fixture |
| Objective | RGB-mean safe-log L1 |
| Recovery | Two rotating full-state checkpoint slots, saved every 64 rendered training frames and resumable through `--resume` |
| Integration | Standalone training/viewer and GPU contract only; production material and RT paths are unchanged |

The node exposes no neural-appearance options or feature UI. The ordinary interactive
mode still uses the renderer's UI and Present plumbing, but it adds no NeuralAppearance
controls. The `--train-and-save` mode omits the UI node entirely and constructs
NeuralAppearance with comparison disabled. Its completion frame therefore stays on the
progress presentation while checkpoint and artifact publication finish instead of
switching to the fullscreen native/neural/error comparison.

## Six roots and a stable 27-pass graph

The node compiles six compute shader roots in this order:

```text
initializeTraining
evaluateTargets
evaluateGradients
optimizeTraining
packLatent
viewer
```

Those programs are instantiated as a stable 27-pass graph:

```text
NeuralAppearance.InitializeTraining
    -> for Pair0 ... Pair7:
           NeuralAppearance.EvaluateTargets.PairN
        -> NeuralAppearance.EvaluateGradients.PairN
        -> NeuralAppearance.OptimizeTraining.PairN
    -> NeuralAppearance.PackLatent
    -> NeuralAppearance.Viewer
```

`Viewer` remains one pass in that fixed topology. Its reflected push-constant record is
24 bytes, and each invocation records one full-grid `8 x 8`-thread dispatch. Before
completion, and whenever comparison is disabled, that dispatch writes progress. After
comparison-enabled completion, the same dispatch writes the native, neural, and
amplified absolute-error panels.

The Viewer shader keeps latent sampling, runtime material-context construction, and
runtime neural-network evaluation in a `[noinline]` helper. This isolates the NVIDIA
driver device-loss-prone code-generation path without changing viewer pixels or the
reflected shader ABI.

`InitializeTraining` services the one-time reset request and initializes the FP32 model,
latent, both Adam moment planes, and status records. Each active target pass produces one
reference record per decoder sample. Its gradient pass reads those records, invokes Slang
reverse AD, and writes disjoint per-sample model and bilinear-latent gradients. The
following optimizer pass reduces the rows deterministically and updates the model and,
after warmup, the latent. This split requires neither floating-point atomics nor an
atomic-float feature.

The host owns a one-based `nextTrainingStep` counter independently of the display ordinal.
Only a strictly newer `OptionFrameSnapshot::frameIndex` advances training; duplicate or
retrograde snapshots activate no slots. While the next step is at most 2,048, only Pair0
is active. Later frames activate up to eight consecutive logical steps, limited by the
remaining budget. With normally advancing ordinals, the full budget therefore occupies
2,048 one-step frames plus 3,840 eight-step frames, or 5,888 rendered frames.

All eight triples remain present on every graph build. An inactive slot keeps the last
scheduled step, pushes batch size zero, and records zero X dispatch groups for target,
gradient, and optimizer. This preserves pass ordinals, binding owners, scratch shape, and
compiled topology before, during, and after training.

`PackLatent` also remains present but records zero X/Y work before step 32,768. During
training, `Viewer` reads only the status record and draws a cheap fullscreen progress
display; it returns before latent sampling, MLP evaluation, or reference evaluation.
At completion, Pack converts the FP32 master latent into two transient `RGBA16F` mip-zero
images. A comparison-enabled interactive Viewer then uses its single full-grid dispatch
to draw the native, neural, and amplified absolute-error panels; the no-UI training mode
keeps comparison disabled and continues with its single progress dispatch. The interactive
three-panel animation uses the display ordinal, not the logical training step.

Persistent state consists of the model, model moments, latent, latent moments, training
control, and status buffers. The model and latent moment buffers each store all Adam
first moments contiguously in the first half and all second moments contiguously in the
second half. Gradient rows, texel indices, metrics, the 1,024-byte sample-target buffer,
packed latent images, and viewer output are graph-transient.

For trainable-state data, training begins with the 16-byte control upload or a complete
checkpoint restore. Model, latent, moments, targets, gradients, and optimization remain
GPU-resident between checkpoints. The no-UI training path reads back the complete
recoverable state every 64 successfully rendered training frames and once more at
completion; ordinary interactive viewing does not perform those checkpoint readbacks.

## Dispatch and performance shape

`EvaluateTargets` uses `[numthreads(256, 1, 1)]` and dispatches one workgroup per active
decoder sample. On the target NVIDIA hardware this is eight 32-lane warps. The group
shares UV, directions, seed, sample count, and mollification angle; its lanes evaluate the
complete deterministic reference set in parallel and combine the 256 `float4` slots with
a groupshared tree reduction. It writes `target.rgb` plus the scheduled angle to one
`float4` record. Reference evaluation is therefore outside the AD graph and is not a
serial 256-evaluation loop inside every gradient lane.

`EvaluateGradients` uses 32 threads, one NVIDIA warp per workgroup. One lane owns one
decoder sample and its large differentiable model state, so a production batch of 64
dispatches two groups. `OptimizeTraining` uses 64 threads per group and 64 active groups,
covering the 4,096 latent texels while also updating the smaller model parameter range.
Each optimizer invocation performs a deterministic batch reduction; no cross-sample
float atomic is used.

The cadence follows the actual target cost. Through the first 2,048 logical steps, the
mollification phase is limited to one active triple per display frame. The cone reaches
zero at step 2,048; afterward each target record needs one sharp reference evaluation, so
up to eight triples run per display frame. The dispatch geometry and phase split are
implementation facts, not a frame-rate claim. The recorded full-budget Release run
completed 32,768 GPU steps across 5,888 rendered frames in 11,095.270 seconds. That
single measurement establishes elapsed time only for its run environment; it is not an
interactive viewer frame-rate guarantee or a broader performance characterization.

## Model and learned-frame contract

Each latent lookup supplies eight spatial features. The decoder derives two learned
shading frames from that state. For each frame, the learned normal `N` and tangent `T`
are normalized independently, not orthogonalized. The bitangent is the normalized
`cross(N, T)`, with a deterministic fallback for a degenerate cross product.

World-space light and view directions are transformed into both learned frames. Their
ordered local XYZ values form the 12 directional inputs, preserving azimuth and avoiding
an assumed reciprocity relation. A direction-independent context caches the 32-wide
spatial prefix and both learned frames for reuse across directional evaluations without
retaining UV.

The first hidden layer combines that spatial prefix with the 12 directional inputs and
applies ReLU. A second 32-wide affine layer also applies ReLU. The RGB logits use
`exp(min(logits - 3, 11))`; `exp(11)` remains below the maximum finite `RGBA16F` value.
The FP32 training path and the packed-latent inference path share this decoder definition.

The reference fixture varies F0 and roughness spatially, uses a tilted physical shading
normal, and rotates its anisotropy tangent. The authoritative label is the existing
anisotropic base-surface projected-specular evaluator. Physical directions and the
physical material frame are used for this label. Learned frames encode only decoder
inputs; they never replace the physical normal, tangent, or projection term.

## Sampling, mollification, loss, and Adam

Training samples `theta_h` and `theta_d` uniformly over `[0, pi/2)` and `phi_h` and
`phi_d` uniformly over `[0, 2pi)` in Rusinkiewicz coordinates. Reconstruction uses up to
64 deterministic rejection attempts and accepts only pairs whose view and light remain
in the physical upper hemisphere; a deterministic valid pair is the bounded fallback.

The reference target mollifies the sampled light direction while holding view fixed. Its
cone half-angle follows a cosine schedule from 10 degrees to zero by logical step 2,048.
While the angle is nonzero, the target workgroup averages 256 directions sampled uniformly
in solid angle within that cone. At zero angle it evaluates the exact sharp light
direction once; the remaining lanes contribute zero to the same reduction.

For prediction `p` and target `t`, the differentiated objective is:

```text
mean_rgb(abs(log(1 + max(p, 0)) - log(1 + max(t, 0))))
```

Adam applies no weight decay. The model learning rate follows the cosine schedule above.
The latent is strictly unchanged for steps 1 through 2,048, then receives Adam updates at
0.1 times the scheduled model rate; its bias-correction step starts at one after warmup.
The optimizer publishes loss/finite telemetry together with the model rate, actual latent
rate, mollification angle, and active batch size.

## Checkpoint, resume, and final binary artifact

The standalone executable accepts:

```powershell
neuralMaterialViewer --train-and-save <artifact.bin>
neuralMaterialViewer --train-and-save <artifact.bin> --checkpoint <checkpoint.bin>
neuralMaterialViewer --resume <checkpoint.bin> --train-and-save <artifact.bin>
```

Without an explicit checkpoint path, the checkpoint base is `<artifact.bin>.checkpoint`.
Ordinary `--train-and-save` automatically restores the newest valid slot when either
slot exists and starts fresh only when neither exists. `--resume` requires a valid
checkpoint base to exist; it does not silently start a new run.

Each checkpoint base owns physical slots `<checkpoint.bin>.0` and
`<checkpoint.bin>.1`. The tool saves after every 64 successfully rendered training
frames and at completion. A save waits for device idle, reads the complete training
state, writes a temporary sibling, and replaces the missing or older-step slot. Loading
validates both slots and restores the newest valid completed step, falling back to the
other slot if one is missing or invalid.

Each slot contains a 52-byte versioned header followed by all state needed to resume:
7,744 bytes of model parameters, 15,488 bytes of packed model Adam first/second
moments, 131,072 bytes of latent values, 262,144 bytes of packed latent Adam
first/second moments, 32 bytes of status, and 16 bytes of training control. The total
slot size is 416,548 bytes. Restore uploads all six buffers and schedules the next
one-based logical step after the stored completed step.

At step 32,768 the tool publishes a final checkpoint before attempting the artifact.
Only after the artifact write succeeds does it remove both checkpoint slots. An artifact
failure therefore leaves the completed recoverable checkpoint in place. Closing the
window before completion or encountering a render, present, readback, checkpoint, or
artifact error returns failure.

The separately requested artifact is one fixed native binary sequence:

| Order | Payload | Size |
|---:|---|---:|
| 1 | Nine-`uint32` header | 36 bytes |
| 2 | 484 model `float4` chunks in FP32 | 7,744 bytes |
| 3 | 8,192 latent `float4` chunks in FP32 | 131,072 bytes |
| 4 | Two status `float4` records in FP32 | 32 bytes |

The header fields are magic `NART` (`0x4E415254`), version 1, latent width 64, latent
height 64, latent channel count 8, model chunk count 484, latent chunk count 8,192,
status chunk count 2, and completed step 32,768. The complete file is 138,884 bytes.

This artifact is a final repository-internal snapshot, not the resumable checkpoint or a
portable deployment format. It omits Adam moments and training control, has no manifest,
hashes, compatibility policy, loader, or cross-platform representation, and is written
only after the full budget. Its writer opens the destination with truncation and is not
an atomic publication mechanism, so a failed write may leave an incomplete artifact
while the final checkpoint remains recoverable. The production-facing target remains a
deliberately designed frozen-inference boundary rather than treating this debug dump as
that boundary.

## Recorded full-budget Release result

One complete Release run reached GPU step 32,768 after 5,888 rendered frames and
11,095.270 seconds, then published
`artifacts/neuralAppearance/full_training.bin`.

| Observation | Recorded value |
|---|---|
| Artifact size | 138,884 bytes |
| Header | Magic `0x4E415254`, version 1, `64 x 64`, 8 channels, 484 model chunks, 8,192 latent chunks, 2 status chunks |
| Non-finite scalar count | 0 |
| Current / initial loss | `0.01673351` / `0.06148338` |
| Model / latent learning rate | `1e-4` / `1e-5` |
| Mollification angle / active batch | `0` / `64` |
| SHA-256 | `44CF72FE85EDB79F7333AC357421E1E45D8CD50407EA744BE6DF47E34A9C9AA9` |

This establishes full-budget completion, the expected fixed artifact ABI, finite stored
values, and final training telemetry for that run. It does not establish final viewer
quality at production resolution: no production-resolution interactive comparison or
perceptual acceptance gate has yet been recorded.

## Validation boundary

The GPU contract compiles a test-only `qualityContract.slang` entry point and evaluates
the actual two-image `RGBA16F` inference path over 256 held-out, stratified samples:
128 uniform Rusinkiewicz pairs, 64 highlight-focused pairs, and 64 grazing pairs. The
host aggregates mean and P95 mapped and safe-log errors overall and for the highlight and
grazing strata, checks finite/non-negative output and deterministic target metadata, and
checks mean/P95 native-versus-neural mapped RGB closeness in the headless viewer. The
16-round smoke also requires the fixed training-batch loss to fall by at least 2%, each
held-out stratum mean to remain within 5% of initialization, and the overall mean to stay
within 5% of the zero-prediction baseline. Full convergence and final-quality acceptance
belong to a formal 32,768-step training run, not CTest.

The registered contract deliberately scales training work to 16 updates, batch 32,
eight mollification references, a one-step mollification schedule and latent warmup, and a
512-texel optimized latent subset dispatched as eight optimizer groups. It still uses
full-sized state buffers, initializes and packs all 4,096 texels, and evaluates all 256
held-out quality samples.
Each logical update is submitted and completed separately so the reverse-AD workload stays
inside the Windows GPU watchdog window. The tail is likewise staged into final
target-and-gradient, latent-pack, held-out-quality, model-contract, comparison-viewer, and
progress-viewer submissions; shader state, barriers, step count, and quality gates are
unchanged by that test-only scheduling boundary.
The headless viewer contract uses a `48 x 16` output: its one `8 x 8` full-grid dispatch
preserves 256 corresponding pixels for each native/neural/error panel and the exact
progress-track rows.
Those settings exercise target-to-gradient barriers, shader ABI, AD, Adam moment packing,
latent updates, packing, stratification, metric plumbing, and viewer agreement; they are
not the live settings of 32,768 steps, batch 64, 256 references, a 2,048-step warmup,
4,096 optimized texels, and 64 optimizer groups. A smoke result must not be cited as
proof of full-budget trained quality.

In Debug builds, `neuralMaterialViewer` and the Neural Appearance GPU contract explicitly
disable GPU-Assisted Validation (GPU-AV) and DebugPrintf shader instrumentation.
Instrumenting the large reverse-AD compute pipeline makes pipeline creation prohibitively
slow. Core, Synchronization, and Object validation remain enabled, as do normal debug callbacks.
`RendererCreateInfo` defaults shader instrumentation on, so other programs keep the
existing GPUAV/DebugPrintf behavior unless they explicitly opt out.

Main implementation points:

- [../shader/include/neuralAppearance/model.slang](../shader/include/neuralAppearance/model.slang)
- [../shader/include/neuralAppearance/trainingSampling.slang](../shader/include/neuralAppearance/trainingSampling.slang)
- [../shader/include/neuralAppearance/reference.slang](../shader/include/neuralAppearance/reference.slang)
- [../shader/renderer/neuralAppearance/evaluateTargets.slang](../shader/renderer/neuralAppearance/evaluateTargets.slang)
- [../shader/renderer/neuralAppearance/evaluateGradients.slang](../shader/renderer/neuralAppearance/evaluateGradients.slang)
- [../src/renderPasses/NeuralAppearance/nrNeuralAppearanceNode.ixx](../src/renderPasses/NeuralAppearance/nrNeuralAppearanceNode.ixx)
- [../src/renderPasses/NeuralAppearance/nrNeuralAppearanceNode.cpp](../src/renderPasses/NeuralAppearance/nrNeuralAppearanceNode.cpp)
- [../tools/neuralMaterialViewer/neuralMaterialViewer.cpp](../tools/neuralMaterialViewer/neuralMaterialViewer.cpp)
- [../shader/test/neuralAppearance/qualityContract.slang](../shader/test/neuralAppearance/qualityContract.slang)
- [../test/unit/rhi/nr_rhi_neural_appearance_shader_contract_test.cpp](../test/unit/rhi/nr_rhi_neural_appearance_shader_contract_test.cpp)

## Deliberate omissions

This slice has no hierarchical latent, learned importance sampler, encoder baking,
CoopVec path, portable artifact loader, hot reload, scene material
conversion, SBT variant, or production ray-tracing integration. It evaluates reflection
only and does not provide the sampling/PDF pair required to replace a production
path-tracing BSDF.

The NumPy trainer remains a deterministic historical golden. It does not create,
initialize, or update the current runtime state.

The next production-facing boundary is inference: a stable consumer for a frozen model
and packed latent plus transport-level validation. That future boundary must not make
training throughput, hierarchical filtering, or importance sampling claims based on this
minimal live tool.

## Verification commands

These commands are the intended LLVM Debug paths. The measured evidence above is a
completed full-budget Release run; it is not a claim that this Debug contract command
set has been rerun:

```powershell
cmake --build --preset debug --target neuralMaterialViewer nr_rhi_neural_appearance_shader_contract_test

foreach ($shader in @(
    "renderer/neuralAppearance/initializeTraining.slang",
    "renderer/neuralAppearance/evaluateTargets.slang",
    "renderer/neuralAppearance/evaluateGradients.slang",
    "renderer/neuralAppearance/optimizeTraining.slang",
    "renderer/neuralAppearance/packLatent.slang",
    "renderer/neuralAppearance/viewer.slang",
    "test/neuralAppearance/autodiffContract.slang",
    "test/neuralAppearance/modelContract.slang",
    "test/neuralAppearance/qualityContract.slang"
)) {
    cmake "-DNR_SHADER_FILE=$shader" -P tools/CheckSlangShader.cmake
}

ctest --preset debug -R "^nr_rhi_neural_appearance_shader_contract_test$" --output-on-failure
```

The following commands illustrate fresh/automatic-resume and explicit-resume execution.
The recorded Release result above used the full budget; these Debug command lines are
examples, not additional completed runs:

```powershell
.\build\llvm\tools\neuralMaterialViewer\Debug\neuralMaterialViewer.exe --train-and-save .\build\neuralAppearance\artifact.bin --checkpoint .\build\neuralAppearance\training.checkpoint
.\build\llvm\tools\neuralMaterialViewer\Debug\neuralMaterialViewer.exe --resume .\build\neuralAppearance\training.checkpoint --train-and-save .\build\neuralAppearance\artifact.bin
```
