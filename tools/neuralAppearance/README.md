# Neural appearance fixture trainer

Status: legacy deterministic CPU golden; not the runtime trainer.

The current implementation trains device-resident FP32 model and latent state with Slang
reverse AD through Vulkan; see
[../../docs/neural_appearance_gpu_training.md](../../docs/neural_appearance_gpu_training.md).
This NumPy utility is retained to reproduce the earlier `z4 + d4` fixture, compare simple
CPU math, and regenerate its historical checked-in constants. Its output is not loaded by
or synchronized with the current GPU training state.

## Runtime direction-CDF table

The runtime trainer samples `theta_h` uniformly and uses a checked-in `96 x 96` inverse-CDF
table to draw `phi_d` with density proportional to `theta_d,max`. Drawing `theta_d`
conditionally then makes `(theta_d, phi_d)` joint-uniform over the valid Rusinkiewicz angular
domain. Regenerate and validate the dependency-free Slang constant module from the repository
root with:

```powershell
python tools/neuralAppearance/generate_half_vector_cdf.py
python tools/neuralAppearance/generate_half_vector_cdf.py --check
```

The logarithmic cosine grid resolves the rapidly changing grazing boundary. The table is a
shader constant array, not a texture or descriptor-backed runtime resource.

## Historical workflow

Run from the repository root:

```powershell
python -m pip install -r tools/neuralAppearance/requirements.txt
python tools/neuralAppearance/train_fixture.py
```

The default 10,000-step run takes roughly 12-20 seconds on the development machine.

The NumPy-only script deterministically builds a periodic `64 x 64` latent texture whose
texels store procedural `F0.rgb + roughness`, trains the fixed `z4 + d4 -> 16 -> 16 -> RGB`
decoder with explicit backpropagation and Adam, evaluates an independent held-out set, and
rewrites `shader/include/neuralAppearance/fixtureModel.slang` with FP16-quantized parameters. The
network input uses repeat-bilinear latent samples while the GGX target uses the continuous
procedural material, matching the viewer's native/reference path. The
direction vector is `(NoV, NoL, NoH, VoH)`, the output activation is softplus, and the loss
is L1 after mapping both prediction and target through `x / (1 + x)`. Training and held-out
direction pairs use the same fixed 55% uniform, 20% cosine-weighted, and 25% GGX-highlight
coverage mixture, but independent random streams.

For this fixed fixture and sampling distribution, the checked-in MVP accepts the final
FP16-quantized model when held-out mapped mean absolute error is at most `0.02` and mapped
P95 absolute error is at most `0.09`.

This is deliberately the smallest offline reproduction fixture. It uses fixed geometry and
shading normals `+Y`, opaque isotropic GGX, upper-hemisphere directions, mip zero, and an
analytic latent rather than joint latent optimization. It does not use SlangPy, reverse-mode
shader AD, CoopVec, source assets, latent mips, normal mapping, anisotropy, transmission,
layering, a viewer, or production renderer integration. The deployment metrics simulate
RGBA16F latent texels and FP16 parameter quantization while retaining FP32 arithmetic.

The reproduction environment pins NumPy in `requirements.txt`; the report records the
actual NumPy version used. The generated file declares `module fixtureModel;` and is imported as
`include.neuralAppearance.fixtureModel`. `W2` is stored row-major as four `float4` chunks
per output row, and `W3` as four chunks per RGB output row. Every emitted value is first
quantized through FP16. Because the current RHI does not enable native shader-float16
features, the MVP executes with FP32 constants, context storage, and accumulation. The
source latent image remains RGBA16F.
