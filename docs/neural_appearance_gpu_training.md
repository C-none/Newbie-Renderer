# Native GPU Neural Appearance Training

Status: V3 architecture is implemented on the training and preview-viewer side. The learned
target is the reflective part of the base surface lobe, and the model is now a single universal
network driven by material parameters rather than a per-material latent texture. There is no
PathTracing integration in this generation: the V2 ray-tracing path and its `.nart` consumer
were retired together with the latent-texture ABI.

This document defines the current neural-material boundary. It supersedes the V2 description:
the viewer/trainer is the only owner, and a published V3 artifact is scene independent.

Primary references:

- NVIDIA [Real-Time Neural Appearance Models](https://research.nvidia.com/labs/rtr/neural_appearance_models/)
- repository paper copy at
  [paper/nvidia_neural_materials_author_paper.pdf](paper/nvidia_neural_materials_author_paper.pdf)
- SIGGRAPH 2026 Neural Shading course material, `github.com/shader-slang/neural-shading-s26`

## Global Cooperative Vector Contract

`VK_NV_cooperative_vector` is a global RHI requirement, not an optional neural feature. Device
creation rejects an adapter unless all of the following are admitted:

- `cooperativeVector` and `cooperativeVectorTraining`
- `shaderFloat16`, `storageBuffer16BitAccess`, and `uniformAndStorageBuffer16BitAccess`
- the Vulkan memory model and `VK_EXT_shader_replicated_composites`, both required by
  the SPIR-V emitted for the cooperative-vector arithmetic used by this implementation
- FP16 `TrainingOptimal` accumulation, at least 32 vector components, and the required
  all-FP16 transposed tuple

[`nr.rhi:cooperativeVector`](../src/rhi/nrCooperativeVector.ixx) exposes the admitted snapshot
and the only typed matrix-conversion API. `TrainingOptimal` is device-specific opaque storage:
its required size is queried from Vulkan, its device address is 64-byte aligned, and it is never
serialized or described in a portable file. RenderGraph uses
`cooperativeVectorConvertRead/Write` for conversion passes, which compile to the dedicated
cooperative-vector conversion stage with transfer read/write access.

## Learned Target

The supervised target is `BaseSurfaceBsdfLobe<baseSurface | anisotropicBaseLobe>::evaluate()`
evaluated on a geometry frame with `+Y` up. Both reflective components are learned:

| Head | Target | Analytic source |
|---|---|---|
| A | `diffuseProjected` | Lambert albedo scaled by the split-sum energy complement |
| B | `specularProjected` | folded anisotropic GGX reflection |

Transmission is excluded at compile time: the reference template omits
`RtMaterialLayerFlag.transmission`, so the whole transmission block is removed and the returned
components are pure reflection. Sheen and clearcoat are separate top-level lobes and are outside
this boundary.

## Model, Training, and Inference ABI

The model is a three-segment network. Segment A encodes resolved material parameters into a
latent vector, segment B predicts one learned shading frame, and segment C decodes both lobes.
V2's two learned frames are unnecessary here because the decoder already separates the lobes.

Encoder input is the 12-value material vector:

| Component | Width | Range |
|---|---:|---|
| `baseColor.rgb` | 3 | `[0,1]` |
| `metallic` | 1 | `[0,1]` |
| `roughness` | 1 | `[kMaterialMinRoughness,1]` |
| `shadingNormal` in the geometry frame | 3 | unit |
| `anisotropyTangent` in the geometry frame | 3 | unit |
| `anisotropyStrength` | 1 | `[0,1]` |

Dielectric `F0` is the hard-coded `0.04` of the analytic lobe, so IOR is deliberately absent.
`baseColor.a`, `alphaCutoff`, `doubleSided`, `frontFace`, `emissive`, and occlusion do not enter
reflection and are also absent.

Directions use the Rusinkiewicz parameterization. The half vector and the difference vector are
projected into the learned frame, and the two geometry-frame cosines are appended unrotated so
the decoder still observes the folding horizon:

```text
dirFeat[8] = [ wh_local.xyz, wd_local.xyz, lightDirection.y, viewDirection.y ]
```

Hidden layers use LeakyReLU with slope `0.01`. Both output heads decode with
`exp(logMax) / (1 + exp(logMax - logit))`, which equals `exp(logit)` in the small regime and
saturates smoothly at `exp(logMax)`. The specular cap uses `logMax = 11`, preserving the finite
`rgba16f` bound that V2's `exp(min(logit - 3, 11))` guaranteed; the diffuse cap uses `logMax = 1`.

The eight affine layers are:

| Layer | Shape (out x in) | FP16 weight offset | FP16 row stride | FP16 bias offset |
|---|---:|---:|---:|---:|
| E1 | 32 x 12 | 0 | 24 | 768 |
| E2 | 32 x 32 | 832 | 64 | 2880 |
| E3 | 8 x 32 | 2944 | 64 | 3456 |
| F | 6 x 8 | 3520 | 16 | 3648 |
| S | 32 x 8 | 3712 | 16 | 4224 |
| D | 32 x 8 | 4288 | 16 | none |
| H | 32 x 32 | 4800 | 64 | 6848 |
| O | 6 x 32 | 6912 | 64 | 7296 |

Every matrix starts on a 64-byte boundary and every row is tightly packed at
`inputCount * 2` bytes. The canonical model blob is 7,360 bytes: 7,176 bytes of defined FP16
parameter values (3,588 scalars) plus four zero-filled alignment ranges `[3472,3520)`,
`[3616,3648)`, `[3660,3712)`, and `[7308,7360)`. `layout.slang` publishes this as one layer
descriptor table, and the scalar-to-byte mapping, bias classification, and padding predicate are
all derived from it.

Training retains FP32 master parameters and FP32 Adam first/second moments. Forward affine reads
use the FP16 mirror so training observes the same quantized values as inference. Each affine
reverse path accumulates weight gradients in a per-layer FP16 `TrainingOptimal` matrix, reduces
its bias gradient where applicable, and converts the resulting matrix back to canonical row-major
FP16 before the FP32 optimizer updates masters and refreshes the mirror. There is no latent
state, so no per-sample gradient reduction and no float atomic capability are required.

Sampling and loss:

- material samples are drawn from the joint parameter-space distribution above;
- directions are uniform in `(thetaH, thetaD, phiH, phiD)` and rejected unless both directions
  keep `y >= 1e-3`, which is the domain the folded reflection is defined on;
- the optimization loss is `L1` in the cube-root domain `3 * (x^(1/3) - 1)` with a `1e-6` floor,
  weighted `0.5/0.5` across the two heads;
- the acceptance metric is the bounded safe-log `L1` of both heads, kept separate from the
  optimization loss so the published gate thresholds stay comparable.

The fixed graph contains Initialize, eight slots, EvaluateQuality, and Viewer. An active slot
records:

```text
EvaluateTargets
-> ClearCoopGradients
-> EvaluateCoopGradients
-> ConvertCoopGradients
-> OptimizeFP32AndQuantize
```

Inactive slots record zero work; the graph therefore has 43 stable passes. Checkpoints retain
portable FP32 master/moment/control state and reconstruct the FP16 mirror and opaque scratch
after restore. EvaluateQuality scores the fixed uniform, highlight, and grazing held-out strata
against the FP32 master, the FP16 CoopVec mirror, and the zero baseline. Publication is rejected
unless the host quality gate accepts finite/non-negative output, the safe-log EMA thresholds,
both per-stratum mean/P95 improvements, and the FP16-to-FP32 loss ratio.

The full held-out report is logged whether the gate passes or fails, and every checkpoint save
logs the current step, EMA, learning rate, and training loss. A pass-only failure log would
leave a successful run unmeasurable, and the checkpoint interval already provides a 512-step
loss curve without an extra readback.

The production budget is 16,384 steps at batch 64 (1,048,576 samples). A completed LLVM Debug
run starts at an EMA safe-log loss of about 0.30 and reaches its floor near step 4,096; the
former 131,072-step budget spent 97% of its time on a plateau. The absolute EMA bound stays at
the V2 value of 0.025. The relative improvement and FP16-versus-FP32 budgets are scale free and
also keep their V2 values.

Every run carries a `trainingSeed` that perturbs both the initial weights and the whole sample
sequence, and it is persisted in the checkpoint so a resumed run continues the same stream.
Without it training is bit identical across runs and run-to-run variance cannot be measured.

Gradients are clipped per element to `kGradientClip` after batch normalization, so the bound is
independent of the batch size. Measured over eight seeds this lowers the held-out mean by 2.3%,
the 95th percentile by 6.6%, and the run-to-run standard deviation by 15%.

## Measured Training Behaviour

Eight seeds at the production configuration, reported by the held-out overall stratum:

| Configuration | held-out mean | std dev | CV | P95 |
|---|---:|---:|---:|---:|
| No clipping, no averaging | 0.009182 | 0.000935 | 10.2% | 0.022144 |
| Weight averaging over the second half | 0.009306 | 0.000943 | 10.1% | 0.022337 |
| Gradient clipping at 0.01 (shipping) | 0.008968 | 0.000784 | 8.7% | 0.020686 |

Three results are load bearing for future work:

- FP16 costs nothing. The FP16 mirror and the FP32 master reach the same held-out loss to five
  decimal places, so quantization-aware training is not a limiting factor.
- Weight averaging does nothing, which means the weights are not oscillating on the plateau. The
  large step-to-step swing in the training EMA is the sampling noise of a 64-sample estimate of
  the loss, not optimization instability. Run-to-run variance (about 10%) is much smaller than
  that apparent swing (about 34%).
- The remaining gap is therefore capacity or representation, not optimizer behaviour. The model
  beats a zero prediction by only 3.3x, and the 95th percentile is 2.3x the mean, so a heavy
  tail of hard samples stays unfit.

Raising the batch to 512 at an equal sample budget (2,048 steps) is worse, not better: the EMA
settles near 0.021 instead of 0.015, and the run additionally fails checkpoint validation. The
per-layer bias gradient is accumulated in FP16 by `coopVecReduceSumAccumulate`, so a large batch
is a plausible overflow source. Batch sizes above 64 are unvalidated; the sample buffers are
sized for the 512 capacity but the shipping configuration stays at 64.

Two reverse-mode constraints are load bearing and must not be reintroduced:

- A `[Differentiable]` function that returns an `IDifferentiable` aggregate across a module
  boundary silently drops its gradient. The training chain therefore carries only `float` and
  `float3` values; `NeuralAppearanceLobes` is a plain struct used by the runtime and quality
  paths alone.
- `pow` contributes no reverse-mode derivative here. The cube-root loss domain is written as
  `exp(log(x) / 3)`.

Both cooperative-vector gradient allocations reserve one extra 64-byte window and shift their
matrix offsets by the measured base misalignment. The allocator only guarantees its own buffer
alignment, so asserting the raw device address is not sufficient.

A cooperative-vector matrix multiply must also never sit behind a `[noinline]` boundary.
Wrapping the preview inference that way compiled to a pathological shader whose pipeline build
took 2,129 ms and which lost the device a few frames after training completed; inlining the same
call restores a 2.6 ms pipeline build and a stable preview.

The clear pass deliberately zeroes only the accumulated `TrainingOptimal` weight gradients and
the bias gradients. The converted row-major destination is fully overwritten by the conversion
pass every frame, so clearing it would be redundant work and would add a cross-frame
write-after-write hazard on a buffer the optimizer only reads.

## Preview

The preview renders three columns (native, neural, amplified error) over two rows (specular on
top, diffuse below). Both rows share a material and a direction pair, so a column comparison is
lobe-for-lobe. Inference is evaluated unconditionally so a workgroup straddling a panel boundary
stays on one path.

`nr_rhi_neural_appearance_shader_contract_test` renders the preview headlessly at 96 x 32 and
checks it against exact expectations: the pre-completion progress image pixel by pixel, the
constant both lobes must decode to under a zero-filled model, the invariant that the error
column equals the amplified absolute difference of the two columns to its left, and the analytic
`1/pi` upper bound that separates the projected diffuse row from the specular row. Swapping the
two rows fails the last check, so the contract has demonstrated discriminating power.

## V3 Artifact

`nr.neuralAppearanceAsset` owns portable artifact parsing, validation, and publication:

- `.nart` V3 is exactly 7,872 bytes: a 512-byte little-endian header and the 7,360-byte
  canonical model payload. There is no latent payload.
- The header stores eight fixed 24-byte layer descriptors: weight byte offset, row stride, input
  count, output count, bias byte offset, and bias count. The direction layer records
  `0xffffffff` as its no-bias offset. It also identifies the sole exported training-profile
  text/digest, the topology/basis/activation/output-semantic identifiers, the material input
  count, the completed budget fields, and the payload SHA-256.
- The reader rejects earlier generations, incorrect length or trailing data, unknown
  layout/profile values or digest, non-zero reserved or model padding, non-finite FP16 values,
  and hash failures. SHA-256 is supplied by the narrow Windows CNG `dependency.crypto` boundary.
- The writer and loader require the exact lowercase `.nart` extension. The writer uses a
  process-unique sibling temporary, flushes it, and atomically replaces the destination. A
  checkpoint remains independent of publication recovery.

A V3 artifact is scene independent, so V2's `.neural.json` sidecar, the binding contract, the
source-scene digest, and the identity-UV0 admission rules are all removed. Nothing in the
renderer consumes a `.nart` in this generation; the artifact is the trainer's published output
format for the next integration stage.

## Verification Status

- `cmake --build --preset debug` completes with the LLVM toolchain.
- `ctest --preset debug --output-on-failure` passes all 62 tests, including the V3 shader
  ABI/reflection/PSO contract, the headless preview pixel contract, the artifact round trip, the
  checkpoint transaction, and the host quality gate.
- `neuralMaterialViewer --train-and-save` completed the full 131,072-step, batch-64 budget in
  16,384 frames on an NVIDIA GeForce RTX 5060 Ti, passed the in-process held-out quality gate,
  published a 7,872-byte V3 artifact reporting 3,588 trainable scalars, and removed its
  completion checkpoint.
- `neuralMaterialViewer` with no arguments trained to completion and then rendered the
  comparison preview continuously without a device loss or a frame-fence timeout.

## Known Issue

Synchronization validation reports a cross-frame `WRITE_AFTER_WRITE` hazard on
`NeuralAppearance.TrainingControl`, one per frame. The cause is in the shared render graph rather
than this node: a retained buffer's cross-frame dependency is expressed with the access scope of
the previous frame's last use and the access scope of the new frame's first use, so a write located
at neither endpoint is never made available across the frame boundary. `TrainingControl` opens each
frame with an initialization read, so the previous frame's optimizer write is made available to
reads only.

The data is unaffected here, but the defect holds for every retained buffer with that access shape.
The measured evidence, the two mirror instances, the investigation method, and the fix direction are
recorded in
[Render Graph Cross-Frame Buffer Barrier Defect](render_graph_cross_frame_barrier_defect.md).

A related, node-owned instance was removed: the clear pass no longer touches
`RowMajorWeightGradients`, because the conversion pass fully overwrites it. That eliminated one
hazard and revealed the `TrainingControl` one, since validation reports only the first hazard per
submit.

## Deliberate Limits

- No PathTracing integration, descriptor set 6, or runtime neural material dispatch.
- No learned sampler, learned proposal PDF, MIS change, hot reload, or UI controls.
- No transmission, sheen, clearcoat, or layered-material coverage.
- No serialized `TrainingOptimal` data and no scalar fallback for an adapter lacking CoopVec.

Main implementation references:

- [../src/rhi/nrCooperativeVector.ixx](../src/rhi/nrCooperativeVector.ixx)
- [../src/neuralAppearanceAsset/exportModule.ixx](../src/neuralAppearanceAsset/exportModule.ixx)
- [../shader/include/neuralAppearance/layout.slang](../shader/include/neuralAppearance/layout.slang)
- [../shader/include/neuralAppearance/model.slang](../shader/include/neuralAppearance/model.slang)
- [../shader/include/neuralAppearance/reference.slang](../shader/include/neuralAppearance/reference.slang)
- [../src/renderPasses/NeuralAppearance/nrNeuralAppearanceNode.ixx](../src/renderPasses/NeuralAppearance/nrNeuralAppearanceNode.ixx)
