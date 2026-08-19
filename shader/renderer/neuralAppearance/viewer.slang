module viewer;
import common;
import include.neuralAppearance.fixture;
import include.neuralAppearance.layout;
import include.neuralAppearance.model;
import include.neuralAppearance.reference;

static const float kNeuralAppearanceViewerFloat16Max = 65504.0f;

struct NeuralAppearancePushConstants
{
    uint width;
    uint height;
    uint frameIndex;
    uint totalTrainingSteps;
    uint comparisonEnabled;
    float errorGain;
};

[[vk::binding(1, 2)]]
ByteAddressBuffer gInferenceParameters;

[[vk::binding(2, 2)]]
StructuredBuffer<float4> gTrainingStatus;

[[vk::binding(0, 2)]]
[format("rgba16f")]
RWTexture2D<float4> gOutputColor;

[[vk::push_constant]]
ConstantBuffer<NeuralAppearancePushConstants> gNeuralAppearance;

float3 neuralAppearanceViewerViewDirection(float2 uv)
{
    float2 offset = (uv - 0.5f) * 0.75f;
    return safeNormalize(float3(offset.x, 1.0f, offset.y), float3(0.0f, 1.0f, 0.0f));
}

float3 neuralAppearanceViewerLightDirection(uint frameIndex)
{
    float phase = float(frameIndex) * 0.015f;
    return safeNormalize(float3(cos(phase), 0.65f, sin(phase)), float3(0.0f, 1.0f, 0.0f));
}

[shader("compute")]
[require(spvCooperativeVectorNV)]
[numthreads(8, 8, 1)]
void viewerMain(uint3 dispatchThreadID: SV_DispatchThreadID)
{
    if (dispatchThreadID.x >= gNeuralAppearance.width ||
        dispatchThreadID.y >= gNeuralAppearance.height)
    {
        return;
    }

    float2 outputSize = float2(max(gNeuralAppearance.width, 1u),
                               max(gNeuralAppearance.height, 1u));
    float2 outputUv = (float2(dispatchThreadID.xy) + 0.5f) / outputSize;
    float4 status = gTrainingStatus[0];
    bool statusFinite = all(isfinite(status));
    float totalTrainingSteps = float(max(gNeuralAppearance.totalTrainingSteps, 1u));
    float completedTrainingSteps = statusFinite ? clamp(status.z, 0.0f, totalTrainingSteps) : 0.0f;
    if (!statusFinite || status.w < 0.5f || completedTrainingSteps < totalTrainingSteps ||
        gNeuralAppearance.comparisonEnabled == 0u)
    {
        float progress = completedTrainingSteps / totalTrainingSteps;
        float3 background = lerp(float3(0.008f, 0.012f, 0.02f),
                                 float3(0.025f, 0.04f, 0.065f), outputUv.y);
        bool insideTrack = abs(outputUv.y - 0.5f) <= 0.035f &&
                           outputUv.x >= 0.08f && outputUv.x <= 0.92f;
        float normalizedTrackX = saturate((outputUv.x - 0.08f) / 0.84f);
        float3 trackColor = normalizedTrackX <= progress
                                ? float3(0.08f, 0.72f, 0.92f)
                                : float3(0.055f, 0.075f, 0.1f);
        float3 progressColor = insideTrack ? trackColor : background;
        if (!statusFinite || status.w < 0.5f)
        {
            progressColor = lerp(progressColor, float3(1.0f, 0.0f, 1.0f), 0.5f);
        }
        gOutputColor[int2(dispatchThreadID.xy)] = float4(progressColor, 1.0f);
        return;
    }

    // Three columns compare native, neural, and amplified error. The upper row
    // shows the specular lobe and the lower row the diffuse lobe.
    float columnCoordinate = outputUv.x * 3.0f;
    uint column = min(uint(columnCoordinate), 2u);
    float rowCoordinate = outputUv.y * 2.0f;
    bool diffuseRow = rowCoordinate >= 1.0f;
    float2 materialUv = float2(frac(columnCoordinate), frac(rowCoordinate));

    float3 viewDirection = neuralAppearanceViewerViewDirection(materialUv);
    float3 lightDirection = neuralAppearanceViewerLightDirection(gNeuralAppearance.frameIndex);
    NeuralAppearanceFixture fixture = neuralAppearanceFixture(materialUv);
    NeuralAppearanceLobes nativeLobes =
        neuralAppearanceReferenceLobes(fixture, viewDirection, lightDirection);
    float3 nativeValue = diffuseRow ? nativeLobes.diffuse : nativeLobes.specular;

    // Inference is evaluated unconditionally and never behind a [noinline]
    // boundary: a non-inlined cooperative-vector call compiles to a pathological
    // shader that loses the device, and an unconditional call keeps every
    // invocation in a workgroup on the same path across a panel boundary.
    NeuralAppearanceLobes neuralLobes = evaluateNeuralAppearanceRuntime(
        gInferenceParameters, neuralAppearanceMaterialInput(fixture), viewDirection, lightDirection);
    float3 neuralValue = diffuseRow ? neuralLobes.diffuse : neuralLobes.specular;

    float3 outputValue;
    if (column == 0u)
    {
        outputValue = nativeValue;
    }
    else
    {
        outputValue = column == 1u
                          ? neuralValue
                          : abs(nativeValue - neuralValue) * max(gNeuralAppearance.errorGain, 0.0f);
    }
    if (status.w < 0.5f || !all(isfinite(status)))
    {
        outputValue = lerp(outputValue, float3(1.0f, 0.0f, 1.0f), 0.5f);
    }
    gOutputColor[int2(dispatchThreadID.xy)] = float4(
        clamp(outputValue, float3(0.0f), float3(kNeuralAppearanceViewerFloat16Max)),
        1.0f);
}
