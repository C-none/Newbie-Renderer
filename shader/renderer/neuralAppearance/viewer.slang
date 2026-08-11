module viewer;
import common;
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

[[vk::binding(0, 1)]]
Sampler2D<float4> gLatentTexture0;

[[vk::binding(1, 1)]]
Sampler2D<float4> gLatentTexture1;

[[vk::binding(1, 2)]]
StructuredBuffer<float4> gModelParameters;

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

[noinline]
float3 evaluateNeuralAppearanceViewer(
    float2 materialUv, float3 viewDirection, float3 lightDirection)
{
    NeuralAppearanceLatent latent = sampleNeuralAppearanceLatent(
        gLatentTexture0, gLatentTexture1, materialUv);
    NeuralMaterialContext context = buildNeuralMaterialContextRuntime(
        gModelParameters, latent);
    return evaluateNeuralAppearanceRuntime(
        gModelParameters, context, viewDirection, lightDirection);
}

[shader("compute")]
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

    float panelCoordinate = outputUv.x * 3.0f;
    uint panel = min(uint(panelCoordinate), 2u);
    float2 materialUv = float2(frac(panelCoordinate), outputUv.y);

    float3 viewDirection = neuralAppearanceViewerViewDirection(materialUv);
    float3 lightDirection = neuralAppearanceViewerLightDirection(gNeuralAppearance.frameIndex);
    float3 outputValue;
    if (panel == 0u)
    {
        outputValue = neuralAppearanceReferenceSpecularProjected(
            materialUv, viewDirection, lightDirection);
    }
    else
    {
        float3 neuralValue = evaluateNeuralAppearanceViewer(
            materialUv, viewDirection, lightDirection);
        if (panel == 1u)
        {
            outputValue = neuralValue;
        }
        else
        {
            float3 nativeValue = neuralAppearanceReferenceSpecularProjected(
                materialUv, viewDirection, lightDirection);
            outputValue = abs(nativeValue - neuralValue) * max(gNeuralAppearance.errorGain, 0.0f);
        }
    }
    if (status.w < 0.5f || !all(isfinite(status)))
    {
        outputValue = lerp(outputValue, float3(1.0f, 0.0f, 1.0f), 0.5f);
    }
    gOutputColor[int2(dispatchThreadID.xy)] = float4(
        clamp(outputValue, float3(0.0f), float3(kNeuralAppearanceViewerFloat16Max)),
        1.0f);
}
