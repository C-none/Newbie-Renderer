module evaluateGradients;
import common;
import include.neuralAppearance.layout;
import include.neuralAppearance.model;
import include.neuralAppearance.trainingSampling;

struct NeuralAppearanceGradientPushConstants
{
    uint trainingStep;
    uint batchSize;
    uint latentWidth;
    uint latentHeight;
    uint mollificationSampleCount;
    uint mollificationStepCount;
    float initialAngleRadians;
    float padding;
};

struct NeuralAppearanceLossInputs : IDifferentiable
{
    NeuralAppearanceParameters parameters = NeuralAppearanceParameters();
    NeuralAppearanceLatentNeighborhood neighborhood = NeuralAppearanceLatentNeighborhood();
    no_diff float3 viewDirection = float3(0.0f, 1.0f, 0.0f);
    no_diff float3 lightDirection = float3(0.0f, 1.0f, 0.0f);
    no_diff float3 target = float3(0.0f);

    __init()
    {
    }

    __init(
        NeuralAppearanceParameters inputParameters,
        NeuralAppearanceLatentNeighborhood inputNeighborhood,
        float3 inputViewDirection, float3 inputLightDirection, float3 inputTarget)
    {
        parameters = inputParameters;
        neighborhood = inputNeighborhood;
        viewDirection = inputViewDirection;
        lightDirection = inputLightDirection;
        target = inputTarget;
    }
};

[[vk::binding(0, 1)]]
StructuredBuffer<float4> gModelParameters;

[[vk::binding(1, 1)]]
StructuredBuffer<float4> gTrainingLatent;

[[vk::binding(3, 2)]]
StructuredBuffer<float4> gSampleTargets;

[[vk::binding(0, 2)]]
RWStructuredBuffer<float4> gSampleGradients;

[[vk::binding(1, 2)]]
RWStructuredBuffer<uint4> gSampleTexelIndices;

[[vk::binding(2, 2)]]
RWStructuredBuffer<float4> gSampleMetrics;

[[vk::push_constant]]
ConstantBuffer<NeuralAppearanceGradientPushConstants> gTraining;

int neuralAppearanceRepeatCoordinate(int coordinate, int extent)
{
    int wrapped = coordinate % extent;
    return wrapped < 0 ? wrapped + extent : wrapped;
}

uint neuralAppearanceTexelIndex(int2 coordinate, uint width, uint height)
{
    int x = neuralAppearanceRepeatCoordinate(coordinate.x, int(width));
    int y = neuralAppearanceRepeatCoordinate(coordinate.y, int(height));
    return uint(y) * width + uint(x);
}

NeuralAppearanceLatentNeighborhood loadNeuralAppearanceLatentNeighborhood(
    float2 uv, uint width, uint height, out uint4 texelIndices)
{
    float2 texelPosition = frac(uv) * float2(width, height) - 0.5f;
    int2 baseTexel = int2(floor(texelPosition));

    texelIndices = uint4(
        neuralAppearanceTexelIndex(baseTexel, width, height),
        neuralAppearanceTexelIndex(baseTexel + int2(1, 0), width, height),
        neuralAppearanceTexelIndex(baseTexel + int2(0, 1), width, height),
        neuralAppearanceTexelIndex(baseTexel + int2(1, 1), width, height));

    float4 values[8] = {};
    for (uint tap = 0u; tap < 4u; ++tap)
    {
        for (uint textureIndex = 0u; textureIndex < 2u; ++textureIndex)
        {
            values[tap * 2u + textureIndex] =
                gTrainingLatent[texelIndices[tap] * 2u + textureIndex];
        }
    }
    return NeuralAppearanceLatentNeighborhood(values, frac(texelPosition));
}

[Differentiable]
float neuralAppearanceTrainingLoss(NeuralAppearanceLossInputs inputs)
{
    NeuralAppearanceLatent latent = interpolateNeuralAppearanceLatent(inputs.neighborhood);
    NeuralMaterialContext context = buildNeuralMaterialContext(inputs.parameters, latent);
    float3 prediction = evaluateNeuralAppearance(
        inputs.parameters, context, inputs.viewDirection, inputs.lightDirection);
    float3 predictionLog = log(float3(1.0f) + max(prediction, float3(0.0f)));
    float3 targetLog = log(float3(1.0f) + max(inputs.target, float3(0.0f)));
    return dot(abs(predictionLog - targetLog), float3(1.0f / 3.0f));
}

[shader("compute")]
[numthreads(32, 1, 1)]
void evaluateGradientsMain(uint3 dispatchThreadID: SV_DispatchThreadID)
{
    uint sampleIndex = dispatchThreadID.x;
    uint batchSize = min(gTraining.batchSize, kNeuralAppearanceBatchSize);
    if (sampleIndex >= batchSize || batchSize == 0u ||
        gTraining.latentWidth != kNeuralAppearanceLatentWidth ||
        gTraining.latentHeight != kNeuralAppearanceLatentHeight)
    {
        return;
    }

    NeuralAppearanceTrainingSample sample = neuralAppearanceTrainingSample(
        gTraining.trainingStep, sampleIndex,
        gTraining.mollificationStepCount, gTraining.initialAngleRadians);
    float4 targetRecord = gSampleTargets[sampleIndex];

    uint4 texelIndices;
    NeuralAppearanceLossInputs inputs = NeuralAppearanceLossInputs(
        loadNeuralAppearanceParameters(gModelParameters),
        loadNeuralAppearanceLatentNeighborhood(
            sample.uv, gTraining.latentWidth, gTraining.latentHeight, texelIndices),
        sample.directions.viewDirection, sample.directions.lightDirection, targetRecord.xyz);

    DifferentialPair<NeuralAppearanceLossInputs> pair = diffPair(inputs);
    bwd_diff(neuralAppearanceTrainingLoss)(pair, 1.0f);

    uint gradientBase = sampleIndex * kNeuralAppearanceGradientChunkCount;
    for (uint chunk = 0u; chunk < kNeuralAppearanceParameterChunkCount; ++chunk)
    {
        float4 gradient = pair.d.parameters.chunks[chunk];
        if (chunk == kNeuralAppearanceParameterChunkCount - 1u)
        {
            gradient.w = 0.0f;
        }
        gSampleGradients[gradientBase + chunk] = gradient;
    }
    for (uint tapChunk = 0u; tapChunk < 8u; ++tapChunk)
    {
        gSampleGradients[gradientBase + kNeuralAppearanceParameterChunkCount + tapChunk] =
            pair.d.neighborhood.values[tapChunk];
    }

    NeuralAppearanceLatent latent = interpolateNeuralAppearanceLatent(inputs.neighborhood);
    NeuralMaterialContext context = buildNeuralMaterialContext(inputs.parameters, latent);
    float3 prediction = evaluateNeuralAppearance(
        inputs.parameters, context, inputs.viewDirection, inputs.lightDirection);
    float3 predictionLog = log(float3(1.0f) + max(prediction, float3(0.0f)));
    float3 targetLog = log(float3(1.0f) + max(inputs.target, float3(0.0f)));
    float loss = dot(abs(predictionLog - targetLog), float3(1.0f / 3.0f));
    bool finite = isfinite(loss) && all(isfinite(prediction)) && all(isfinite(inputs.target));
    gSampleTexelIndices[sampleIndex] = texelIndices;
    gSampleMetrics[sampleIndex] = float4(
        loss, dot(prediction, float3(1.0f / 3.0f)),
        targetRecord.w, finite ? 1.0f : 0.0f);
}
