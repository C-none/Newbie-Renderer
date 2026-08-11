# Single-Scale Neural Specular Evaluation MVP

Status: superseded offline MVP; retained as a deterministic historical golden.

The current native Vulkan/Slang GPU-training and learned-frame implementation is
[Native GPU Neural Appearance Training](neural_appearance_gpu_training.md). The details
and measurements below describe the earlier NumPy-trained `z4 + d4` baseline only; its
generated constants are not the current runtime training state.

This document describes the smallest closed loop that demonstrates a spatial latent,
a compact neural decoder, and real-time Slang/Vulkan evaluation inside Newbie-Renderer.
It intentionally does not implement the platform-sized original proposal.

This is not a complete reproduction of NVIDIA Real-Time Neural Appearance Models. The
full method includes hierarchical latent textures, learned shading frames, importance
decoding, material layering, and production renderer integration. This MVP deliberately
stops once the neural evaluation loop is measurable and visible.

Primary references:

- NVIDIA [Real-Time Neural Appearance Models](https://research.nvidia.com/labs/rtr/neural_appearance_models/)
- repository paper copy at [paper/nvidia_neural_materials_author_paper.pdf](paper/nvidia_neural_materials_author_paper.pdf)
- the current authoritative material implementation in
  [../shader/include/material/payload.slang](../shader/include/material/payload.slang)

## 1. Implemented boundary

| Concern | Frozen MVP decision |
|---|---|
| Material domain | One periodic procedural, opaque, isotropic, fully metallic GGX material |
| Spatial state | 64 x 64 RGBA16F latent storing F0.rgb and roughness at texel centers |
| Sampling | Repeat, bilinear, mip zero, exactly one shader-visible SampleLevel call |
| Direction input | NoV, NoL, NoH, VoH |
| Decoder | z4 + d4 -> 16 ReLU -> 16 ReLU -> RGB softplus |
| Reusable state | Direction-independent float spatialPrefix[16], 64 bytes |
| Target | BaseSurfaceBsdfLobe.evaluate(...).specularProjected |
| Offline training | Deterministic NumPy backpropagation and Adam |
| Deployment values | RGBA16F latent and FP16-quantized weights emitted as FP32 Slang constants |
| Viewer | Standalone three-panel native / neural / absolute-error compute viewer |
| Production renderer | Unchanged; no scene, path payload, or material-table integration |

The current RHI does not enable native shader-float16 storage/arithmetic. Extending the
device feature chain would make this first loop larger without proving the neural
evaluation idea, so the model preserves FP16-quantized numeric values but uses FP32
constants, context storage, and accumulation. The latent image remains RGBA16F.

## 2. Minimal data flow

Offline:

~~~text
periodic F0 + roughness
    -> 64 x 64 latent texels
    -> independent UV/direction training samples
    -> mapped-L1 training of fixed 2 x 16 decoder
    -> FP16 quantization
    -> generated fixtureModel.slang
~~~

Runtime:

~~~text
generate 64 x 64 RGBA16F latent
    -> sample latent once at material UV
    -> build direction-independent spatialPrefix
    -> evaluate one or more view/light direction pairs
    -> show native | neural | amplified absolute error
~~~

The split first layer is:

~~~text
first([z, d]) = Wz * z + Wd * d + b1
spatialPrefix = Wz * z + b1
query          = ReLU(spatialPrefix + Wd * d)
~~~

The evaluation function accepts only NeuralMaterialContext and directions. It cannot
sample a texture or recover UV. A dedicated contract shader verifies that one prepared
context produces the same result as recomputing the complete first layer for six
direction pairs.

## 3. Surviving historical artifacts

Offline training and generated parameters:

- [../tools/neuralAppearance/train_fixture.py](../tools/neuralAppearance/train_fixture.py)
- [../tools/neuralAppearance/README.md](../tools/neuralAppearance/README.md)
- [../shader/include/neuralAppearance/fixtureModel.slang](../shader/include/neuralAppearance/fixtureModel.slang)

The earlier model, runtime shader roots, node, viewer, and GPU contract were replaced
in place by the native GPU-training implementation. Their old source revisions are not
part of the current worktree; use [neural_appearance_gpu_training.md](neural_appearance_gpu_training.md)
for the runnable implementation map.

## 4. Measured acceptance

The deterministic default training run uses seed 20260809, 65,536 training samples,
32,768 independent held-out samples, batch size 2,048, and 10,000 Adam steps.

The checked-in generated model produced:

| Metric | Result | Gate |
|---|---:|---:|
| FP32 held-out mapped mean absolute error | 0.01802183 | <= 0.02 |
| Quantized held-out mapped mean absolute error | 0.01802370 | <= 0.02 |
| Quantized held-out mapped P95 absolute error | 0.08696430 | <= 0.09 |
| Improvement over zero baseline | 81.23% | diagnostic |
| Quantization mapped-mean delta | +0.000001872 | diagnostic |
| Texel-center sampler max error | 0 | <= 0.000001 |
| Integer-repeat sampler max error | 0.000000149 | <= 0.00002 |

All held-out targets and predictions were finite and non-negative. With the pinned NumPy
environment, the generated Slang file is deterministic for the frozen inputs; its canonical
LF-output SHA-256 is:

~~~text
144F4A258B2C51976028DC182DAAA03256E7A6B621F1F753AF65D9BB4B264A63
~~~

The old 64-byte-context GPU acceptance contract was superseded together with its runtime
shader. It is not an acceptance gate for the current 160-byte learned-frame context.

## 5. Reproduce the historical offline golden

From the repository root:

~~~powershell
python -m pip install -r tools/neuralAppearance/requirements.txt
python tools/neuralAppearance/train_fixture.py
~~~

This regenerates only the legacy `fixtureModel.slang` constants. It does not build or
control the current viewer; current build, shader-check, and GPU-test commands live in
[neural_appearance_gpu_training.md](neural_appearance_gpu_training.md).

## 6. Deliberately deferred

The following work is outside this MVP and must be justified by a separate measured
goal before implementation:

- jointly optimized latent values
- hierarchical latent MIPs and LOD
- learned shading frames
- importance decoder, proposal, or neural sampling
- normal maps, anisotropy, transmission, and layered materials
- glTF fixture loading and material conversion
- SlangPy reverse AD, custom filtered scatter derivatives, and GPU Adam
- CoopVec and native FP16 RHI feature enablement
- checkpoints, portable binary artifacts, hashes, hot reload, and generation retirement
- production scene/material/path-payload integration
- WebSocket/Lua control, EXR sweeps, and large image-regression infrastructure

The MVP therefore proves only this narrower statement: a compact neural decoder can
approximate a spatially varying GGX projected-specular function and run through the
project's real-time Slang/Vulkan render graph with a single latent sample. It does not
claim the paper's complete feature set, compression ratio, or speedup over complex
layered materials.
