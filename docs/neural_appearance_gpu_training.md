# Native GPU Neural Appearance Training and P0 Deployment

Status: V2 architecture, runtime integration, LLVM Debug acceptance, Release retraining, and
BoxTextured production publication are complete. The checked-in `.nart` and mirrored sidecar
listed below are the validated P0 asset pair; the historical V1 snapshot remains unsupported.

This document defines the current neural-material boundary. It supersedes the former tool-only
V1 snapshot description: the viewer/trainer remains the training owner, but a validated V2
artifact can now cross the model-load, Scene, acceleration-structure, and PathTracing layers.

Primary references:

- NVIDIA [Real-Time Neural Appearance Models](https://research.nvidia.com/labs/rtr/neural_appearance_models/)
- repository paper copy at
  [paper/nvidia_neural_materials_author_paper.pdf](paper/nvidia_neural_materials_author_paper.pdf)

## Global Cooperative Vector Contract

`VK_NV_cooperative_vector` is a global RHI requirement, not an optional neural feature. Device
creation rejects an adapter unless all of the following are admitted:

- `cooperativeVector` and `cooperativeVectorTraining`
- `shaderFloat16`, `storageBuffer16BitAccess`, and `uniformAndStorageBuffer16BitAccess`
- the Vulkan memory model and `VK_EXT_shader_replicated_composites`, both required by
  the SPIR-V emitted for the cooperative-vector arithmetic used by this implementation
- compute, raygen, and closest-hit cooperative-vector stage support
- FP16 `TrainingOptimal` accumulation, at least 32 vector components, and the required
  all-FP16 transposed tuple

[`nr.rhi:cooperativeVector`](../src/rhi/nrCooperativeVector.ixx) exposes the admitted snapshot
and the only typed matrix-conversion API. `TrainingOptimal` is device-specific opaque storage:
its required size is queried from Vulkan, its device address is 64-byte aligned, and it is never
serialized or described in a portable file. RenderGraph uses
`cooperativeVectorConvertRead/Write` for conversion passes, which compile to the dedicated
cooperative-vector conversion stage with transfer read/write access.

Stage roots explicitly declare their use: PathTracing raygen and closest hit require
`spvCooperativeVectorNV`; the neural gradient root also requires
`spvCooperativeVectorTrainingNV`.

## Model, Training, and Inference ABI

The model keeps the existing two learned frames, local Y-up directions, ReLU hidden layers, and
`exp(min(logit - 3, 11))` RGB output. It has five affine layers:

| Layer | Shape | FP16 weight offset | FP16 row stride | FP16 bias offset |
|---|---:|---:|---:|---:|
| F | 12 x 8 | 0 | 16 | 192 |
| S | 32 x 8 | 256 | 16 | 768 |
| D | 32 x 12 | 832 | 24 | none |
| H | 32 x 32 | 1600 | 64 | 3648 |
| O | 3 x 32 | 3712 | 64 | 3904 |

The canonical model blob is 3,968 bytes: 3,870 bytes of defined FP16 parameter values (1,935
scalars) plus the required internal and tail zero padding. Deployment latent state is two
row-major `64 x 64 RGBA16F` planes (32,768 bytes each). This layout is shared by the Slang
model, artifact reader/writer, and PathTracing runtime.

Training retains FP32 master model/latent values and FP32 Adam first/second moments. Forward
affine and latent reads use FP16 mirrors so training observes the same quantized values as
inference. Each affine reverse path accumulates weight gradients in a per-layer FP16
`TrainingOptimal` matrix, reduces its bias gradient where applicable, and converts the resulting
matrix back to canonical row-major FP16 before the FP32 optimizer updates masters and refreshes
the mirrors. Latent gradients remain per-sample and are deterministically reduced; no float
atomic capability is introduced.

The fixed graph contains Initialize, eight slots, Pack, and Viewer. An active slot records:

```text
EvaluateTargets
-> ClearCoopGradients
-> EvaluateCoopGradients
-> ConvertCoopGradients
-> OptimizeFP32AndQuantize
```

Inactive slots record zero work; the graph therefore has 43 stable passes. Checkpoints retain
portable FP32 master/moment/control state and reconstruct FP16 mirrors and opaque scratch after
restore. Pack evaluates the fixed uniform, highlight, and grazing held-out strata against the
FP32 master, FP16 CoopVec mirror, and zero baseline. Publication is rejected unless the host
quality gate accepts finite/non-negative output, the safe-log EMA thresholds, both per-stratum
mean/P95 improvements, and the FP16-to-FP32 loss ratio.

## V2 Artifact and Sidecar Admission

`nr.neuralAppearanceAsset` owns portable artifact parsing, validation, and publication:

- `.nart` V2 is exactly 70,016 bytes: a 512-byte little-endian header and 69,504-byte payload.
- The payload is the 3,968-byte canonical model followed by the two 32,768-byte latent planes.
  It contains 1,935 logical FP16 scalars (3,870 bytes) and exactly two zero-filled alignment ranges:
  `[216,256)` and `[3910,3968)`.
- The header stores five fixed 24-byte layer descriptors: weight byte offset, row stride, input
  count, output count, bias byte offset, and bias count. The direction layer records
  `0xffffffff` as its no-bias offset. It also identifies the sole exported training-profile
  text/digest, source Scene and material identity, identity UV0 contract, completed budget fields,
  and payload SHA-256.
- The reader rejects V1, incorrect length/trailing data, unknown layout/profile values or digest,
  non-zero reserved/model padding, non-finite FP16 values, invalid dimensions/offsets, and hash
  failures. SHA-256 is supplied by the narrow Windows CNG `dependency.crypto` boundary.
- The writer, loader, and sidecar resolver require the exact lowercase `.nart` extension. The
  writer uses a process-unique sibling temporary, flushes it, and atomically replaces the
  destination. A checkpoint remains independent of publication recovery.

Model load mirrors a source scene path below the main-repository asset root:

```text
assets/neuralAppearance/bindings/<assets-relative-scene>.neural.json
assets/neuralAppearance/artifacts/v2/<sidecar artifact path>
```

The strict sidecar contains exactly `schema`, `scene`, `material`, and `artifact`; P0 accepts
only schema `nr.neural-material-binding/v2`, material 0, and an exact lowercase `.nart` artifact
path. Parsing uses Boost.JSON, enforces UTF-8 and bounded input, rejects duplicate keys, and
rejects absolute, URI, wildcard, `..`, or root-escaping paths.
Missing sidecars leave loading native. A present but invalid sidecar/artifact rejects the entire
candidate Scene before commit, preserving the previously committed Scene.

`SceneTemplateCreateInfo::neuralMaterialBinding` transfers a validated immutable artifact to
`MaterialAssetRecord::neuralAppearance`. This is CPU metadata only. The Scene neither allocates
nor owns neural GPU handles.

P0 admission is intentionally narrow: one artifact for material 0 of an opaque, single-sided,
base-surface material with base-color UV0 and identity transform; normal, clearcoat, sheen,
transmission, and anisotropy features are rejected. The glTF Sample Assets submodule is never
written by this workflow. The validated BoxTextured production pair lives in the main repository:

```text
assets/neuralAppearance/bindings/glTF-Sample-Assets/Models/BoxTextured/glTF/BoxTextured.gltf.neural.json
assets/neuralAppearance/artifacts/v2/glTF-Sample-Assets/Models/BoxTextured/glTF/BoxTextured.material-0.nart
```

## PathTracing P0

`AccelerationStructureBuildNode` owns artifact GPU residency. It builds a parallel 16-byte
`RtNeuralMaterialRef` table, a canonical FP16 model `ByteAddressBuffer`, and two latent images.
It always supplies neutral fallback resources when no admitted artifact is resident. PathTracing
therefore has a fixed reflection/root ABI in descriptor set 6:

| Binding | Resource |
|---:|---|
| 0 | `gRtNeuralMaterialRefs` storage buffer |
| 1 | `gNeuralModelParameters` FP16 byte-address buffer |
| 2 | `gNeuralLatentTexture0` repeat/linear LOD-0 sampler |
| 3 | `gNeuralLatentTexture1` repeat/linear LOD-0 sampler |

P0 preserves the analytic BSDF sampler and PDF `q`. It keeps analytic diffuse and replaces only
the projected specular evaluation:

- closest hit uses `neuralProjected / q` as continuation specular weight before payload
  compaction;
- raygen NEE evaluates the same neural helper for the sampled light direction;
- the helper maps world directions into artifact-local `(dot(T,d), dot(N,d), dot(B,d))` with
  Y = N and returns analytic specular behavior when neural output is non-finite or negative.

The `ResolvedMaterialRayPayload` remains 128 bytes. Only for P0-eligible material rays, it
reuses transport data that analytic reconstruction does not consume: a pair of flags stores
neural activation/tangent handedness, the existing anisotropy-tangent lane stores mesh tangent,
and the clearcoat/sheen pair stores repeat-addressed Q0.16x2 UV. This does not alter SBT keys,
payload size, or non-neural material data.

## Verification and Publication Status

The P0 publication passed the following gates on 2026-08-12:

1. LLVM Debug build plus focused RHI capability/conversion, RenderGraph barrier, artifact/sidecar,
   training, and PathTracing payload/estimator tests.
2. Validation-clean GPU conversion and descriptor-set-6 execution on the target adapter.
3. Release build and a full 32,768-step, batch-64 training run producing V2 output.
4. Post-publication Debug reload and integration validation of that exact artifact.
5. Finite/non-negative output and the agreed safe-log/held-out quality gates.

- `cmake --build --preset debug` completed with the LLVM toolchain.
- `ctest --preset debug --output-on-failure` passed all 62 tests, including the real GPU CoopVec
  forward/backward/conversion/Adam numerical test, descriptor/root reflection, 43-pass training,
  artifact/sidecar transaction, Scene binding, PathTracing estimator, and window smoke coverage.
- The Release trainer completed 32,768 steps at batch 64 (2,097,152 samples) on an NVIDIA
  GeForce RTX 5060 Ti, passed the in-process held-out quality gate, published the V2 artifact,
  and removed its completion checkpoint.
- Post-publication Debug tests reloaded the exact production artifact, validated its BoxTextured
  material-0 contract, and exercised the PathTracing integration. The file is exactly 70,016
  bytes and has SHA-256
  `D8C311AB9FB17DCBFC1769A6D42C72964C0F3FE7D69A15EC30946076DCD5F9B3`.

Historical V1 training measurements and the former `full_training.bin` are not evidence for V2
deployment quality and remain intentionally rejected by the V2 loader.

## Deliberate P0 Limits

- No learned sampler, learned proposal PDF, MIS change, hierarchical latent, hot reload, UI
  controls, or multi-artifact/multi-material cache.
- No generic glTF material conversion or energy-conservation claim beyond the strict P0 material
  eligibility class.
- No serialized `TrainingOptimal` data or scalar fallback for an adapter lacking CoopVec.

Main implementation references:

- [../src/rhi/nrCooperativeVector.ixx](../src/rhi/nrCooperativeVector.ixx)
- [../src/neuralAppearanceAsset/exportModule.ixx](../src/neuralAppearanceAsset/exportModule.ixx)
- [../shader/include/neuralAppearance/layout.slang](../shader/include/neuralAppearance/layout.slang)
- [../src/renderPasses/NeuralAppearance/nrNeuralAppearanceNode.ixx](../src/renderPasses/NeuralAppearance/nrNeuralAppearanceNode.ixx)
- [../src/renderPasses/AccelerationStructureBuild/nrAccelerationStructureBuildNode.cpp](../src/renderPasses/AccelerationStructureBuild/nrAccelerationStructureBuildNode.cpp)
- [../shader/renderer/pathTracing/neuralMaterial.slang](../shader/renderer/pathTracing/neuralMaterial.slang)
