import argparse
import bisect
import math
import random
from pathlib import Path


DIRECTION_MINIMUM_COSINE = 1.0e-3
THETA_COUNT = 96
CDF_COUNT = 96
INTEGRATION_COUNT = 16384
VALIDATION_THETA_COUNT = 257
VALIDATION_DIRECTION_COUNT = 262144
HALF_PI = 0.5 * math.pi
THETA_MAXIMUM = math.acos(DIRECTION_MINIMUM_COSINE)
PROJECT_ROOT = Path(__file__).resolve().parents[2]
OUTPUT = PROJECT_ROOT / "shader/include/neuralAppearance/halfVectorConditionalCdf.slang"


def maximum_difference_angle(theta_h: float, phi_d: float) -> float:
    cosine_h = math.cos(theta_h)
    radial_y = math.sin(theta_h) * abs(math.cos(phi_d))
    radius = math.hypot(cosine_h, radial_y)
    boundary_angle = math.acos(min(1.0, DIRECTION_MINIMUM_COSINE / radius))
    return max(0.0, boundary_angle - math.atan2(radial_y, cosine_h))


def inverse_cdf_row(theta_h: float) -> list[float]:
    integration_step = HALF_PI / INTEGRATION_COUNT
    weights = [
        maximum_difference_angle(theta_h, (index + 0.5) * integration_step)
        for index in range(INTEGRATION_COUNT)
    ]
    cumulative = [0.0]
    for weight in weights:
        cumulative.append(cumulative[-1] + weight)

    total = cumulative[-1]
    if total <= 0.0:
        return [index * HALF_PI / (CDF_COUNT - 1) for index in range(CDF_COUNT)]

    result = []
    for index in range(CDF_COUNT):
        target = index * total / (CDF_COUNT - 1)
        bin_index = min(bisect.bisect_right(cumulative, target) - 1, INTEGRATION_COUNT - 1)
        weight = weights[bin_index]
        fraction = 0.0 if weight <= 0.0 else (target - cumulative[bin_index]) / weight
        result.append((bin_index + min(max(fraction, 0.0), 1.0)) * integration_step)
    result[0] = 0.0
    result[-1] = HALF_PI
    return result


def build_table() -> list[list[float]]:
    rows = []
    for index in range(THETA_COUNT):
        theta_coordinate = index / (THETA_COUNT - 1)
        theta_h = math.acos(DIRECTION_MINIMUM_COSINE**theta_coordinate)
        if index + 1 == THETA_COUNT:
            theta_h = THETA_MAXIMUM - 1.0e-7
        rows.append(inverse_cdf_row(theta_h))
    return rows


def interpolated_inverse_cdf(rows: list[list[float]], theta_h: float, unit_value: float) -> float:
    theta_coordinate = (
        math.log(max(math.cos(theta_h), DIRECTION_MINIMUM_COSINE))
        / math.log(DIRECTION_MINIMUM_COSINE)
        * (THETA_COUNT - 1)
    )
    theta_index = min(max(int(theta_coordinate), 0), THETA_COUNT - 2)
    theta_fraction = min(max(theta_coordinate - theta_index, 0.0), 1.0)

    cdf_coordinate = min(max(unit_value, 0.0), 1.0) * (CDF_COUNT - 1)
    cdf_index = min(int(cdf_coordinate), CDF_COUNT - 2)
    cdf_fraction = cdf_coordinate - cdf_index

    lower = rows[theta_index][cdf_index] * (1.0 - cdf_fraction) + rows[theta_index][
        cdf_index + 1
    ] * cdf_fraction
    upper = rows[theta_index + 1][cdf_index] * (1.0 - cdf_fraction) + rows[theta_index + 1][
        cdf_index + 1
    ] * cdf_fraction
    return lower * (1.0 - theta_fraction) + upper * theta_fraction


def full_difference_azimuth(rows: list[list[float]], theta_h: float, unit_value: float) -> float:
    quadrant_coordinate = unit_value * 4.0
    quadrant = min(int(quadrant_coordinate), 3)
    quarter_azimuth = interpolated_inverse_cdf(rows, theta_h, quadrant_coordinate - quadrant)
    if quadrant == 0:
        return quarter_azimuth
    if quadrant == 1:
        return math.pi - quarter_azimuth
    if quadrant == 2:
        return math.pi + quarter_azimuth
    return 2.0 * math.pi - quarter_azimuth


def validate_table(rows: list[list[float]]) -> tuple[float, float]:
    maximum_error = 0.0
    error_sum = 0.0
    sample_count = 0
    for theta_index in range(VALIDATION_THETA_COUNT):
        theta_h = THETA_MAXIMUM * (theta_index + 0.5) / VALIDATION_THETA_COUNT
        reference = inverse_cdf_row(theta_h)
        for cdf_index, reference_value in enumerate(reference):
            unit_value = cdf_index / (CDF_COUNT - 1)
            error = abs(interpolated_inverse_cdf(rows, theta_h, unit_value) - reference_value)
            maximum_error = max(maximum_error, error)
            error_sum += error
            sample_count += 1

    mean_error = error_sum / sample_count
    if maximum_error > 0.013 or mean_error > 0.0015:
        raise RuntimeError(
            f"inverse-CDF error exceeds its contract: maximum={maximum_error}, mean={mean_error}"
        )
    return maximum_error, mean_error


def validate_direction_domain(rows: list[list[float]]) -> float:
    generator = random.Random(0x243F6A88)
    minimum_direction_cosine = 1.0
    for _ in range(VALIDATION_DIRECTION_COUNT):
        theta_h = generator.random() * THETA_MAXIMUM
        phi_d = full_difference_azimuth(rows, theta_h, generator.random())
        theta_d = generator.random() * maximum_difference_angle(theta_h, phi_d)
        direction_cosine = math.cos(theta_h) * math.cos(theta_d) - (
            math.sin(theta_h) * math.sin(theta_d) * abs(math.cos(phi_d))
        )
        minimum_direction_cosine = min(minimum_direction_cosine, direction_cosine)

    if minimum_direction_cosine < DIRECTION_MINIMUM_COSINE - 1.0e-12:
        raise RuntimeError(f"generated an invalid direction cosine: {minimum_direction_cosine}")
    return minimum_direction_cosine


def slang_float(value: float, significant_digits: int = 9) -> str:
    literal = f"{value:.{significant_digits}g}"
    if "." not in literal and "e" not in literal:
        literal += ".0"
    return literal + "f"


def generate_slang(rows: list[list[float]]) -> str:
    values = [value for row in rows for value in row]
    lines = [
        "// Generated by tools/neuralAppearance/generate_half_vector_cdf.py. Do not edit by hand.",
        "module halfVectorConditionalCdf;",
        "",
        f"public static const uint kNeuralAppearanceHalfVectorCdfThetaCount = {THETA_COUNT}u;",
        f"public static const uint kNeuralAppearanceHalfVectorCdfValueCount = {CDF_COUNT}u;",
        "public static const float kNeuralAppearanceHalfVectorDirectionMinimumCosine =",
        f"    {slang_float(DIRECTION_MINIMUM_COSINE)};",
        "public static const float kNeuralAppearanceHalfVectorThetaMaximum =",
        f"    {slang_float(THETA_MAXIMUM, 12)};",
        "public static const float kNeuralAppearanceHalfVectorInverseCdf",
        "    [kNeuralAppearanceHalfVectorCdfThetaCount * kNeuralAppearanceHalfVectorCdfValueCount] =",
        "{",
    ]
    for offset in range(0, len(values), 8):
        literals = ", ".join(slang_float(value) for value in values[offset : offset + 8])
        lines.append(f"    {literals},")
    lines.extend(
        [
            "};",
            "",
            "public float neuralAppearanceHalfVectorDifferenceAzimuthInverseCdf(",
            "    float thetaH, float unitValue)",
            "{",
            "    // Log-cosine rows concentrate resolution where the valid domain changes fastest near grazing.",
            "    float thetaUnit = log(max(cos(thetaH), kNeuralAppearanceHalfVectorDirectionMinimumCosine)) /",
            "                      log(kNeuralAppearanceHalfVectorDirectionMinimumCosine);",
            "    float thetaCoordinate = saturate(thetaUnit) *",
            "                            float(kNeuralAppearanceHalfVectorCdfThetaCount - 1u);",
            "    uint thetaIndex = min(uint(thetaCoordinate), kNeuralAppearanceHalfVectorCdfThetaCount - 2u);",
            "    float thetaFraction = thetaCoordinate - float(thetaIndex);",
            "",
            "    float cdfCoordinate = saturate(unitValue) *",
            "                          float(kNeuralAppearanceHalfVectorCdfValueCount - 1u);",
            "    uint cdfIndex = min(uint(cdfCoordinate), kNeuralAppearanceHalfVectorCdfValueCount - 2u);",
            "    float cdfFraction = cdfCoordinate - float(cdfIndex);",
            "    uint lowerOffset = thetaIndex * kNeuralAppearanceHalfVectorCdfValueCount + cdfIndex;",
            "    uint upperOffset = lowerOffset + kNeuralAppearanceHalfVectorCdfValueCount;",
            "    float lower = lerp(kNeuralAppearanceHalfVectorInverseCdf[lowerOffset],",
            "                       kNeuralAppearanceHalfVectorInverseCdf[lowerOffset + 1u], cdfFraction);",
            "    float upper = lerp(kNeuralAppearanceHalfVectorInverseCdf[upperOffset],",
            "                       kNeuralAppearanceHalfVectorInverseCdf[upperOffset + 1u], cdfFraction);",
            "    return lerp(lower, upper, thetaFraction);",
            "}",
            "",
        ]
    )
    return "\n".join(lines)


def main() -> None:
    parser = argparse.ArgumentParser(description="Generate the neural-appearance half-vector inverse CDF.")
    parser.add_argument("--check", action="store_true", help="Fail if the checked-in Slang table is stale.")
    arguments = parser.parse_args()

    rows = build_table()
    maximum_error, mean_error = validate_table(rows)
    minimum_direction_cosine = validate_direction_domain(rows)
    source = generate_slang(rows)

    if arguments.check:
        if not OUTPUT.is_file() or OUTPUT.read_text(encoding="utf-8") != source:
            raise SystemExit(f"stale generated file: {OUTPUT}")
    else:
        OUTPUT.write_text(source, encoding="utf-8", newline="\n")

    print(
        f"table={THETA_COUNT}x{CDF_COUNT} max_phi_error={maximum_error:.8f} "
        f"mean_phi_error={mean_error:.8f} min_direction_cosine={minimum_direction_cosine:.9f}"
    )


if __name__ == "__main__":
    main()
