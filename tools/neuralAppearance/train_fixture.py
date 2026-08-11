#!/usr/bin/env python3
"""Train and emit the deterministic neural-appearance fixture model.

This is intentionally a small NumPy-only reproduction aid, not the proposed GPU
training pipeline.  The latent texture directly stores F0.rgb and roughness, while
the decoder learns the current isotropic GGX projected-specular response.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path

import numpy as np


LATENT_WIDTH = 64
LATENT_HEIGHT = 64
TRAINING_SEED = 20_260_809
MIN_ROUGHNESS = np.float32(0.045)
MIN_COS_THETA = np.float32(1.0e-5)
PI = np.float32(3.14159265358979323846)
TWO_PI = np.float32(6.28318530717958647692)

PARAMETER_NAMES = ("Wz", "Wd", "b1", "W2", "b2", "W3", "b3")
MAX_HELDOUT_MAPPED_L1 = 0.02
MAX_HELDOUT_MAPPED_P95 = 0.09


def fixture_continuous(uv: np.ndarray) -> np.ndarray:
    """Return periodic procedural F0.rgb + roughness for arbitrary UVs."""

    wrapped = np.remainder(np.asarray(uv, dtype=np.float32), np.float32(1.0))
    phase = wrapped * TWO_PI
    wave_x = np.float32(0.5) + np.float32(0.5) * np.sin(phase[..., 0])
    wave_y = np.float32(0.5) + np.float32(0.5) * np.sin(phase[..., 1])
    wave_xy = np.float32(0.5) + np.float32(0.5) * np.sin(phase[..., 0] + phase[..., 1])
    roughness_wave = np.float32(0.5) + np.float32(0.5) * np.cos(phase[..., 0] - phase[..., 1])
    f0 = np.float32(0.04) + np.float32(0.76) * np.stack((wave_x, wave_y, wave_xy), axis=-1)
    roughness = np.float32(0.32) + np.float32(0.58) * roughness_wave
    return np.concatenate((f0, roughness[..., None]), axis=-1).astype(np.float32, copy=False)


def make_latent_texture() -> np.ndarray:
    x = (np.arange(LATENT_WIDTH, dtype=np.float32) + np.float32(0.5)) / np.float32(LATENT_WIDTH)
    y = (np.arange(LATENT_HEIGHT, dtype=np.float32) + np.float32(0.5)) / np.float32(LATENT_HEIGHT)
    uv_x, uv_y = np.meshgrid(x, y, indexing="xy")
    return fixture_continuous(np.stack((uv_x, uv_y), axis=-1))


def sample_repeat_bilinear(texture: np.ndarray, uv: np.ndarray) -> np.ndarray:
    """Match normalized repeat + linear filtering, including the -0.5 texel offset."""

    height, width, _ = texture.shape
    wrapped = np.remainder(np.asarray(uv, dtype=np.float32), np.float32(1.0))
    position_x = wrapped[..., 0] * np.float32(width) - np.float32(0.5)
    position_y = wrapped[..., 1] * np.float32(height) - np.float32(0.5)
    floor_x = np.floor(position_x)
    floor_y = np.floor(position_y)
    blend_x = (position_x - floor_x).astype(np.float32, copy=False)
    blend_y = (position_y - floor_y).astype(np.float32, copy=False)
    x0 = floor_x.astype(np.int64) % width
    y0 = floor_y.astype(np.int64) % height
    x1 = (x0 + 1) % width
    y1 = (y0 + 1) % height

    top = texture[y0, x0] * (np.float32(1.0) - blend_x[..., None]) + texture[y0, x1] * blend_x[..., None]
    bottom = texture[y1, x0] * (np.float32(1.0) - blend_x[..., None]) + texture[y1, x1] * blend_x[..., None]
    return (top * (np.float32(1.0) - blend_y[..., None]) + bottom * blend_y[..., None]).astype(
        np.float32, copy=False
    )


def _schlick_weight(cosine: np.ndarray) -> np.ndarray:
    value = np.clip(np.float32(1.0) - np.abs(cosine), np.float32(0.0), np.float32(1.0))
    value2 = value * value
    return value2 * value2 * value


def ggx_specular_projected(z: np.ndarray, direction: np.ndarray) -> np.ndarray:
    """Match BaseSurfaceBsdfLobe's current isotropic opaque reflection for N=Ng=+Y."""

    f0 = z[:, :3]
    roughness = np.clip(z[:, 3], MIN_ROUGHNESS, np.float32(1.0))
    no_v, no_l, no_h, vo_h = (direction[:, index] for index in range(4))
    alpha = np.maximum(roughness * roughness, MIN_ROUGHNESS * MIN_ROUGHNESS)
    alpha2 = alpha * alpha

    distribution_denominator = (no_h * alpha2 - no_h) * no_h + np.float32(1.0)
    distribution = alpha2 / np.maximum(
        PI * distribution_denominator * distribution_denominator, np.float32(1.0e-7)
    )

    length_v = np.sqrt(np.maximum(alpha2 + (np.float32(1.0) - alpha2) * no_v * no_v, np.float32(0.0)))
    length_l = np.sqrt(np.maximum(alpha2 + (np.float32(1.0) - alpha2) * no_l * no_l, np.float32(0.0)))
    geometry = np.float32(2.0) * no_v * no_l / np.maximum(
        no_l * length_v + no_v * length_l, np.float32(1.0e-7)
    )

    fresnel_weight = _schlick_weight(vo_h)
    fresnel = f0 + (np.float32(1.0) - f0) * fresnel_weight[:, None]

    safe_roughness = np.maximum(roughness, MIN_ROUGHNESS)
    clamped_no_v = np.clip(no_v, np.float32(0.0), np.float32(1.0))
    directional_albedo = np.float32(1.0) - np.clip(
        np.power(safe_roughness, clamped_no_v / safe_roughness)
        * ((safe_roughness * clamped_no_v + np.float32(0.0266916)) / (np.float32(0.466495) + clamped_no_v)),
        np.float32(0.0),
        np.float32(1.0),
    )
    safe_directional_albedo = np.maximum(directional_albedo, np.float32(1.0e-4))
    energy_weight = np.float32(1.0) + f0 * (
        (np.float32(1.0) - safe_directional_albedo) / safe_directional_albedo
    )[:, None]

    denominator = np.maximum(np.float32(4.0) * no_v * no_l, np.float32(1.0e-7))
    projected = energy_weight * fresnel * (distribution * geometry * no_l / denominator)[:, None]
    valid = (no_v > MIN_COS_THETA) & (no_l > MIN_COS_THETA) & (no_h > MIN_COS_THETA) & (vo_h > MIN_COS_THETA)
    return np.where(valid[:, None], projected, np.float32(0.0)).astype(np.float32, copy=False)


def sample_direction_encoding(rng: np.random.Generator, roughness: np.ndarray) -> np.ndarray:
    """Sample physical upper-hemisphere direction pairs with extra highlight coverage."""

    count = roughness.shape[0]
    modes = rng.random(count, dtype=np.float32)
    random_values = rng.random((count, 3), dtype=np.float32)
    no_v = np.maximum(random_values[:, 0], np.float32(0.002))
    no_l = np.maximum(random_values[:, 1], np.float32(0.002))
    delta_phi = TWO_PI * random_values[:, 2]

    cosine_mask = (modes >= np.float32(0.55)) & (modes < np.float32(0.75))
    no_v[cosine_mask] = np.sqrt(no_v[cosine_mask])
    no_l[cosine_mask] = np.sqrt(no_l[cosine_mask])

    highlight_mask = modes >= np.float32(0.75)
    highlight_count = int(np.count_nonzero(highlight_mask))
    if highlight_count:
        highlight_no_v = np.float32(0.04) + np.float32(0.96) * random_values[highlight_mask, 0]
        normal_values = rng.standard_normal((highlight_count, 2), dtype=np.float32)
        highlight_roughness = roughness[highlight_mask]
        no_v[highlight_mask] = highlight_no_v
        no_l[highlight_mask] = np.clip(
            highlight_no_v + normal_values[:, 0] * (np.float32(0.025) + np.float32(0.16) * highlight_roughness),
            np.float32(0.002),
            np.float32(1.0),
        )
        delta_phi[highlight_mask] = PI + normal_values[:, 1] * (
            np.float32(0.025) + np.float32(0.42) * highlight_roughness
        )

    sin_v = np.sqrt(np.maximum(np.float32(1.0) - no_v * no_v, np.float32(0.0)))
    sin_l = np.sqrt(np.maximum(np.float32(1.0) - no_l * no_l, np.float32(0.0)))
    view = np.stack((sin_v, no_v, np.zeros_like(sin_v)), axis=-1)
    light = np.stack((sin_l * np.cos(delta_phi), no_l, sin_l * np.sin(delta_phi)), axis=-1)
    half_vector = view + light
    half_vector /= np.maximum(np.linalg.norm(half_vector, axis=1, keepdims=True), np.float32(1.0e-7))
    no_h = np.clip(half_vector[:, 1], np.float32(0.0), np.float32(1.0))
    vo_h = np.clip(np.sum(view * half_vector, axis=1), np.float32(0.0), np.float32(1.0))
    return np.stack((no_v, no_l, no_h, vo_h), axis=-1).astype(np.float32, copy=False)


def make_dataset(rng: np.random.Generator, texture: np.ndarray, count: int) -> dict[str, np.ndarray]:
    uv = rng.random((count, 2), dtype=np.float32)
    z = sample_repeat_bilinear(texture, uv)
    reference_material = fixture_continuous(uv)
    direction = sample_direction_encoding(rng, reference_material[:, 3])
    target = ggx_specular_projected(reference_material, direction)
    return {"uv": uv, "z": z, "direction": direction, "target": target}


def _softplus(value: np.ndarray) -> np.ndarray:
    return np.log1p(np.exp(-np.abs(value))) + np.maximum(value, np.float32(0.0))


def _sigmoid(value: np.ndarray) -> np.ndarray:
    positive = value >= np.float32(0.0)
    result = np.empty_like(value)
    result[positive] = np.float32(1.0) / (np.float32(1.0) + np.exp(-value[positive]))
    exponent = np.exp(value[~positive])
    result[~positive] = exponent / (np.float32(1.0) + exponent)
    return result


def initialize_parameters(rng: np.random.Generator, target_mean: np.ndarray) -> dict[str, np.ndarray]:
    combined = rng.standard_normal((16, 8), dtype=np.float32) * np.sqrt(np.float32(2.0 / 8.0))
    parameters = {
        "Wz": combined[:, :4].copy(),
        "Wd": combined[:, 4:].copy(),
        "b1": np.full(16, np.float32(0.02), dtype=np.float32),
        "W2": rng.standard_normal((16, 16), dtype=np.float32) * np.sqrt(np.float32(2.0 / 16.0)),
        "b2": np.full(16, np.float32(0.02), dtype=np.float32),
        "W3": rng.standard_normal((3, 16), dtype=np.float32) * np.float32(0.08),
        "b3": np.log(np.expm1(np.maximum(target_mean, np.float32(1.0e-4)))).astype(np.float32),
    }
    return parameters


def forward(
    parameters: dict[str, np.ndarray], z: np.ndarray, direction: np.ndarray, *, cache: bool = False
) -> np.ndarray | tuple[np.ndarray, tuple[np.ndarray, ...]]:
    pre1 = z @ parameters["Wz"].T + direction @ parameters["Wd"].T + parameters["b1"]
    hidden1 = np.maximum(pre1, np.float32(0.0))
    pre2 = hidden1 @ parameters["W2"].T + parameters["b2"]
    hidden2 = np.maximum(pre2, np.float32(0.0))
    pre3 = hidden2 @ parameters["W3"].T + parameters["b3"]
    prediction = _softplus(pre3).astype(np.float32, copy=False)
    if cache:
        return prediction, (pre1, hidden1, pre2, hidden2, pre3)
    return prediction


def mapped_l1_and_gradients(
    parameters: dict[str, np.ndarray], z: np.ndarray, direction: np.ndarray, target: np.ndarray
) -> tuple[np.float32, dict[str, np.ndarray]]:
    prediction, activations = forward(parameters, z, direction, cache=True)
    pre1, hidden1, pre2, hidden2, pre3 = activations
    mapped_prediction = prediction / (np.float32(1.0) + prediction)
    mapped_target = target / (np.float32(1.0) + target)
    difference = mapped_prediction - mapped_target
    loss = np.mean(np.abs(difference), dtype=np.float32)

    output_count = np.float32(difference.size)
    grad_prediction = np.sign(difference) / (
        output_count * (np.float32(1.0) + prediction) * (np.float32(1.0) + prediction)
    )
    grad_pre3 = grad_prediction * _sigmoid(pre3)
    grad_w3 = grad_pre3.T @ hidden2
    grad_b3 = np.sum(grad_pre3, axis=0, dtype=np.float32)

    grad_hidden2 = grad_pre3 @ parameters["W3"]
    grad_pre2 = grad_hidden2 * (pre2 > np.float32(0.0))
    grad_w2 = grad_pre2.T @ hidden1
    grad_b2 = np.sum(grad_pre2, axis=0, dtype=np.float32)

    grad_hidden1 = grad_pre2 @ parameters["W2"]
    grad_pre1 = grad_hidden1 * (pre1 > np.float32(0.0))
    gradients = {
        "Wz": grad_pre1.T @ z,
        "Wd": grad_pre1.T @ direction,
        "b1": np.sum(grad_pre1, axis=0, dtype=np.float32),
        "W2": grad_w2,
        "b2": grad_b2,
        "W3": grad_w3,
        "b3": grad_b3,
    }
    return loss, {name: value.astype(np.float32, copy=False) for name, value in gradients.items()}


def train(
    parameters: dict[str, np.ndarray],
    dataset: dict[str, np.ndarray],
    rng: np.random.Generator,
    steps: int,
    batch_size: int,
    learning_rate: float,
) -> list[float]:
    moments = {name: np.zeros_like(parameters[name]) for name in PARAMETER_NAMES}
    velocities = {name: np.zeros_like(parameters[name]) for name in PARAMETER_NAMES}
    beta1 = np.float32(0.9)
    beta2 = np.float32(0.999)
    epsilon = np.float32(1.0e-8)
    learning_rate = np.float32(learning_rate)
    beta1_power = np.float32(1.0)
    beta2_power = np.float32(1.0)
    losses: list[float] = []

    for step in range(1, steps + 1):
        indices = rng.integers(0, dataset["z"].shape[0], size=batch_size)
        loss, gradients = mapped_l1_and_gradients(
            parameters,
            dataset["z"][indices],
            dataset["direction"][indices],
            dataset["target"][indices],
        )
        beta1_power *= beta1
        beta2_power *= beta2
        progress = np.float32(step / steps)
        scheduled_rate = learning_rate * (
            np.float32(0.1) + np.float32(0.9) * np.float32(0.5)
            * (np.float32(1.0) + np.cos(PI * progress))
        )
        for name in PARAMETER_NAMES:
            gradient = np.clip(gradients[name], np.float32(-1.0), np.float32(1.0))
            moments[name] *= beta1
            moments[name] += (np.float32(1.0) - beta1) * gradient
            velocities[name] *= beta2
            velocities[name] += (np.float32(1.0) - beta2) * gradient * gradient
            corrected_moment = moments[name] / (np.float32(1.0) - beta1_power)
            corrected_velocity = velocities[name] / (np.float32(1.0) - beta2_power)
            parameters[name] -= scheduled_rate * corrected_moment / (np.sqrt(corrected_velocity) + epsilon)

        if step == 1 or step == steps or step % max(steps // 10, 1) == 0:
            losses.append(float(loss))
            print(f"step {step:5d}/{steps}: mapped_l1={float(loss):.7f}", flush=True)

    return losses


def assert_valid(name: str, values: np.ndarray, *, nonnegative: bool) -> None:
    if not np.all(np.isfinite(values)):
        raise RuntimeError(f"{name} contains a non-finite value")
    if nonnegative and np.any(values < np.float32(0.0)):
        raise RuntimeError(f"{name} contains a negative value")


def metric_set(prediction: np.ndarray, target: np.ndarray) -> dict[str, float]:
    absolute = np.abs(prediction - target)
    mapped_prediction = prediction / (np.float32(1.0) + prediction)
    mapped_target = target / (np.float32(1.0) + target)
    mapped_absolute = np.abs(mapped_prediction - mapped_target)
    return {
        "mapped_l1": float(np.mean(mapped_absolute, dtype=np.float64)),
        "mapped_p95_absolute": float(np.percentile(mapped_absolute, 95.0)),
        "mapped_max_absolute": float(np.max(mapped_absolute)),
        "mae": float(np.mean(absolute, dtype=np.float64)),
        "rmse": float(np.sqrt(np.mean(np.square(prediction - target), dtype=np.float64))),
        "p95_absolute": float(np.percentile(absolute, 95.0)),
        "max_absolute": float(np.max(absolute)),
    }


def quantize_parameters(parameters: dict[str, np.ndarray]) -> dict[str, np.ndarray]:
    return {name: value.astype(np.float16).astype(np.float32) for name, value in parameters.items()}


def _literal(value: np.float32 | np.float16 | float) -> str:
    converted = float(np.float32(value))
    if not np.isfinite(converted):
        raise RuntimeError("cannot emit a non-finite Slang constant")
    if converted == 0.0:
        converted = 0.0
    return f"{converted:.9g}f"


def _float4_array(name: str, values: np.ndarray) -> list[str]:
    reshaped = values.reshape(-1, 4)
    lines = [f"public static const float4 {name}[{reshaped.shape[0]}] =", "{"]
    lines.extend(f"    float4({', '.join(_literal(component) for component in row)})," for row in reshaped)
    lines.append("};")
    return lines


def _float_array(name: str, values: np.ndarray) -> list[str]:
    lines = [f"public static const float {name}[{values.size}] =", "{"]
    lines.extend(f"    {_literal(value)}," for value in values.reshape(-1))
    lines.append("};")
    return lines


def emit_slang(output: Path, parameters: dict[str, np.ndarray]) -> None:
    quantized = {name: value.astype(np.float16) for name, value in parameters.items()}
    lines = [
        "// Generated by tools/neuralAppearance/train_fixture.py. Do not edit by hand.",
        "module fixtureModel;",
        "",
        f"public static const uint kNeuralAppearanceLatentWidth = {LATENT_WIDTH}u;",
        f"public static const uint kNeuralAppearanceLatentHeight = {LATENT_HEIGHT}u;",
        f"public static const uint kNeuralAppearanceSeed = {TRAINING_SEED}u;",
        "",
        "public float4 neuralAppearanceFixtureContinuous(float2 uv)",
        "{",
        "    float2 phase = 6.28318531f * frac(uv);",
        "    float3 waves = 0.5f + 0.5f * float3(sin(phase.x), sin(phase.y), sin(phase.x + phase.y));",
        "    float3 f0 = 0.04f + 0.76f * waves;",
        "    float roughness = 0.32f + 0.58f * (0.5f + 0.5f * cos(phase.x - phase.y));",
        "    return float4(f0, roughness);",
        "}",
        "",
        "public float4 neuralAppearanceFixtureLatentTexel(uint2 texel)",
        "{",
        "    float2 size = float2(kNeuralAppearanceLatentWidth, kNeuralAppearanceLatentHeight);",
        "    return neuralAppearanceFixtureContinuous((float2(texel) + 0.5f) / size);",
        "}",
        "",
    ]
    lines.extend(_float4_array("kNeuralAppearanceWz", quantized["Wz"]))
    lines.append("")
    lines.extend(_float4_array("kNeuralAppearanceWd", quantized["Wd"]))
    lines.append("")
    lines.extend(_float_array("kNeuralAppearanceB1", quantized["b1"]))
    lines.append("")
    lines.extend(_float4_array("kNeuralAppearanceW2", quantized["W2"]))
    lines.append("")
    lines.extend(_float_array("kNeuralAppearanceB2", quantized["b2"]))
    lines.append("")
    lines.extend(_float4_array("kNeuralAppearanceW3", quantized["W3"]))
    lines.extend(
        [
            "",
            "public static const float3 kNeuralAppearanceB3 =",
            f"    float3({', '.join(_literal(value) for value in quantized['b3'])});",
            "",
        ]
    )
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text("\n".join(lines), encoding="utf-8", newline="\n")


def validate_sampler(texture: np.ndarray) -> dict[str, float]:
    texel_x = (np.arange(LATENT_WIDTH, dtype=np.float32) + np.float32(0.5)) / np.float32(LATENT_WIDTH)
    texel_y = (np.arange(LATENT_HEIGHT, dtype=np.float32) + np.float32(0.5)) / np.float32(LATENT_HEIGHT)
    uv_x, uv_y = np.meshgrid(texel_x, texel_y, indexing="xy")
    centers = np.stack((uv_x, uv_y), axis=-1)
    center_error = np.max(np.abs(sample_repeat_bilinear(texture, centers) - texture))

    seam_uv = np.array(
        [[0.0, 0.125], [0.25, 0.0], [0.999, 0.375], [-0.125, 1.25], [2.5, -3.75]], dtype=np.float32
    )
    repeat_error = np.max(
        np.abs(
            sample_repeat_bilinear(texture, seam_uv)
            - sample_repeat_bilinear(texture, seam_uv + np.array([3.0, -2.0], dtype=np.float32))
        )
    )
    if center_error > np.float32(1.0e-6) or repeat_error > np.float32(2.0e-5):
        raise RuntimeError(f"repeat bilinear validation failed: center={center_error}, repeat={repeat_error}")
    return {"texel_center_max": float(center_error), "integer_repeat_max": float(repeat_error)}


def parse_args() -> argparse.Namespace:
    repository_root = Path(__file__).resolve().parents[2]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--steps", type=int, default=10_000)
    parser.add_argument("--batch-size", type=int, default=2_048)
    parser.add_argument("--train-samples", type=int, default=65_536)
    parser.add_argument("--heldout-samples", type=int, default=32_768)
    parser.add_argument("--learning-rate", type=float, default=2.0e-3)
    parser.add_argument(
        "--output",
        type=Path,
        default=repository_root / "shader" / "include" / "neuralAppearance" / "fixtureModel.slang",
    )
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    if min(args.steps, args.batch_size, args.train_samples, args.heldout_samples) <= 0:
        raise ValueError("steps, batch size, and sample counts must be positive")

    texture = make_latent_texture()
    sampler_metrics = validate_sampler(texture)
    train_dataset = make_dataset(np.random.default_rng(TRAINING_SEED + 1), texture, args.train_samples)
    heldout_dataset = make_dataset(np.random.default_rng(TRAINING_SEED + 2), texture, args.heldout_samples)
    assert_valid("latent texture", texture, nonnegative=True)
    assert_valid("training target", train_dataset["target"], nonnegative=True)
    assert_valid("held-out target", heldout_dataset["target"], nonnegative=True)

    parameters = initialize_parameters(
        np.random.default_rng(TRAINING_SEED + 3), np.mean(train_dataset["target"], axis=0, dtype=np.float32)
    )
    loss_trace = train(
        parameters,
        train_dataset,
        np.random.default_rng(TRAINING_SEED + 4),
        args.steps,
        args.batch_size,
        args.learning_rate,
    )
    for name, value in parameters.items():
        assert_valid(name, value, nonnegative=False)

    fp32_prediction = forward(parameters, heldout_dataset["z"], heldout_dataset["direction"])
    fp16_parameters = quantize_parameters(parameters)
    fp16_texture = texture.astype(np.float16).astype(np.float32)
    fp16_z = sample_repeat_bilinear(fp16_texture, heldout_dataset["uv"])
    fp16_prediction = forward(fp16_parameters, fp16_z, heldout_dataset["direction"])
    assert_valid("FP32 held-out prediction", fp32_prediction, nonnegative=True)
    assert_valid("FP16 held-out prediction", fp16_prediction, nonnegative=True)
    fp32_metrics = metric_set(fp32_prediction, heldout_dataset["target"])
    fp16_metrics = metric_set(fp16_prediction, heldout_dataset["target"])
    zero_baseline = metric_set(np.zeros_like(heldout_dataset["target"]), heldout_dataset["target"])
    accepted = (
        fp16_metrics["mapped_l1"] <= MAX_HELDOUT_MAPPED_L1
        and fp16_metrics["mapped_p95_absolute"] <= MAX_HELDOUT_MAPPED_P95
    )
    if not accepted:
        raise RuntimeError(
            "quantized model misses the held-out quality gate: "
            f"mapped_l1={fp16_metrics['mapped_l1']:.8f}, "
            f"mapped_p95={fp16_metrics['mapped_p95_absolute']:.8f}"
        )
    emit_slang(args.output.resolve(), parameters)

    report = {
        "seed": TRAINING_SEED,
        "numpy_version": np.__version__,
        "accepted": accepted,
        "acceptance": {
            "max_heldout_mapped_l1": MAX_HELDOUT_MAPPED_L1,
            "max_heldout_mapped_p95_absolute": MAX_HELDOUT_MAPPED_P95,
        },
        "latent_shape": [LATENT_HEIGHT, LATENT_WIDTH, 4],
        "configuration": {
            "steps": args.steps,
            "batch_size": args.batch_size,
            "train_samples": args.train_samples,
            "heldout_samples": args.heldout_samples,
            "learning_rate": args.learning_rate,
        },
        "sampler": sampler_metrics,
        "loss_trace": loss_trace,
        "heldout_fp32": fp32_metrics,
        "heldout_fp16": fp16_metrics,
        "heldout_zero_baseline": zero_baseline,
        "fp16_mapped_l1_improvement_over_zero": 1.0 - fp16_metrics["mapped_l1"] / zero_baseline["mapped_l1"],
        "fp16_mapped_l1_delta": fp16_metrics["mapped_l1"] - fp32_metrics["mapped_l1"],
        "output": str(args.output.resolve()),
    }
    print(json.dumps(report, indent=2, sort_keys=True))


if __name__ == "__main__":
    main()
