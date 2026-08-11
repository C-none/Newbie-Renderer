module modelContract;
import common;
import include.neuralAppearance.fixture;
import include.neuralAppearance.layout;
import include.neuralAppearance.model;

[[vk::binding(0, 1)]]
Sampler2D<float4> gLatentTexture0;

[[vk::binding(1, 1)]]
Sampler2D<float4> gLatentTexture1;

[[vk::binding(1, 2)]]
StructuredBuffer<float4> gModelParameters;

[[vk::binding(0, 2)]]
RWStructuredBuffer<uint> gContractResults;

bool neuralAppearanceValuesMatch(float3 cached, float3 recomputed)
{
    float3 tolerance = 3.0e-5f * (float3(1.0f) + max(abs(cached), abs(recomputed)));
    return all(isfinite(cached)) && all(isfinite(recomputed)) &&
           all(abs(cached - recomputed) <= tolerance);
}

bool neuralAppearanceContractMatches(float3 cached, float3 recomputed)
{
    return neuralAppearanceValuesMatch(cached, recomputed) &&
           all(cached >= float3(0.0f)) && all(recomputed >= float3(0.0f));
}

bool neuralAppearanceFrameUnitFinite(NeuralAppearanceFrame frame)
{
    float3 lengths = float3(length(frame.normal), length(frame.tangent), length(frame.bitangent));
    return all(isfinite(frame.normal)) && all(isfinite(frame.tangent)) &&
           all(isfinite(frame.bitangent)) &&
           all(abs(lengths - float3(1.0f)) <= float3(2.0e-4f));
}

bool neuralAppearanceFrameCrossMatches(NeuralAppearanceFrame frame)
{
    float3 crossValue = cross(frame.normal, frame.tangent);
    float crossLengthSquared = dot(crossValue, crossValue);
    float3 expected = crossLengthSquared >= 1.0e-12f
                          ? crossValue * rsqrt(max(crossLengthSquared, 1.0e-12f))
                          : orthogonalTangent(frame.normal);
    return all(abs(expected - frame.bitangent) <= float3(2.0e-4f)) &&
           abs(dot(frame.bitangent, frame.normal)) <= 2.0e-4f &&
           abs(dot(frame.bitangent, frame.tangent)) <= 2.0e-4f;
}

bool neuralAppearancePrefixPathsMatch(
    NeuralMaterialContext runtimeContext, NeuralMaterialContext differentiableContext)
{
    bool matches = true;
    for (uint value = 0u; value < 32u; ++value)
    {
        float runtimeValue = runtimeContext.spatialPrefix[value];
        float differentiableValue = differentiableContext.spatialPrefix[value];
        float tolerance = 3.0e-5f *
                          (1.0f + max(abs(runtimeValue), abs(differentiableValue)));
        matches = matches && isfinite(runtimeValue) && isfinite(differentiableValue) &&
                  abs(runtimeValue - differentiableValue) <= tolerance;
    }
    return matches;
}

bool neuralAppearanceFramePathsMatch(
    NeuralAppearanceFrame runtimeFrame, NeuralAppearanceFrame differentiableFrame)
{
    return neuralAppearanceValuesMatch(runtimeFrame.normal, differentiableFrame.normal) &&
           neuralAppearanceValuesMatch(runtimeFrame.tangent, differentiableFrame.tangent) &&
           neuralAppearanceValuesMatch(runtimeFrame.bitangent, differentiableFrame.bitangent) &&
           runtimeFrame.normalPadding == differentiableFrame.normalPadding &&
           runtimeFrame.tangentPadding == differentiableFrame.tangentPadding &&
           runtimeFrame.bitangentPadding == differentiableFrame.bitangentPadding;
}

[shader("compute")]
[numthreads(1, 1, 1)]
void modelContractMain(uint3 dispatchThreadID: SV_DispatchThreadID)
{
    static const float2 materialUv = float2(24.5f / 64.0f, 40.5f / 64.0f);
    NeuralAppearanceLatent latent = sampleNeuralAppearanceLatent(
        gLatentTexture0, gLatentTexture1, materialUv);
    NeuralAppearanceParameters parameters = loadNeuralAppearanceParameters(gModelParameters);
    NeuralMaterialContext context = buildNeuralMaterialContextRuntime(gModelParameters, latent);
    NeuralMaterialContext differentiableContext = buildNeuralMaterialContext(parameters, latent);

    bool latentFinite = true;
    for (uint chunk = 0u; chunk < 2u; ++chunk)
    {
        latentFinite = latentFinite && all(isfinite(latent.chunks[chunk]));
    }
    gContractResults[0] = uint(latentFinite);

    bool parametersFinite = true;
    for (uint chunk = 0u; chunk < kNeuralAppearanceParameterChunkCount; ++chunk)
    {
        parametersFinite = parametersFinite && all(isfinite(parameters.chunks[chunk]));
    }
    gContractResults[1] = uint(parametersFinite);

    bool prefixFinite = true;
    for (uint value = 0u; value < 32u; ++value)
    {
        prefixFinite = prefixFinite && isfinite(context.spatialPrefix[value]);
    }
    gContractResults[2] = uint(prefixFinite);
    gContractResults[3] = uint(neuralAppearanceFrameUnitFinite(context.frames[0]));
    gContractResults[4] = uint(neuralAppearanceFrameCrossMatches(context.frames[0]));
    gContractResults[5] = uint(neuralAppearanceFrameUnitFinite(context.frames[1]));
    gContractResults[6] = uint(neuralAppearanceFrameCrossMatches(context.frames[1]));

    float3 viewDirections[4] = {
        safeNormalize(float3(0.15f, 0.98f, 0.09f), float3(0.0f, 1.0f, 0.0f)),
        safeNormalize(float3(0.71f, 0.62f, -0.33f), float3(0.0f, 1.0f, 0.0f)),
        safeNormalize(float3(-0.54f, 0.44f, 0.72f), float3(0.0f, 1.0f, 0.0f)),
        safeNormalize(float3(-0.82f, 0.25f, -0.51f), float3(0.0f, 1.0f, 0.0f)),
    };
    float3 lightDirections[4] = {
        safeNormalize(float3(0.42f, 0.87f, 0.25f), float3(0.0f, 1.0f, 0.0f)),
        safeNormalize(float3(-0.65f, 0.66f, 0.37f), float3(0.0f, 1.0f, 0.0f)),
        safeNormalize(float3(0.76f, 0.36f, -0.54f), float3(0.0f, 1.0f, 0.0f)),
        safeNormalize(float3(-0.25f, 0.22f, -0.94f), float3(0.0f, 1.0f, 0.0f)),
    };

    for (uint pair = 0u; pair < 4u; ++pair)
    {
        float3 cached = evaluateNeuralAppearanceRuntime(
            gModelParameters, context, viewDirections[pair], lightDirections[pair]);
        float3 recomputed = evaluateNeuralAppearanceRecomputedRuntime(
            gModelParameters, latent, viewDirections[pair], lightDirections[pair]);
        float3 differentiablePath = evaluateNeuralAppearance(
            parameters, differentiableContext, viewDirections[pair], lightDirections[pair]);
        float3 differentiableRecomputed = evaluateNeuralAppearanceRecomputed(
            parameters, latent, viewDirections[pair], lightDirections[pair]);
        gContractResults[7u + pair] = uint(
            neuralAppearanceContractMatches(cached, recomputed) &&
            neuralAppearanceContractMatches(cached, differentiablePath) &&
            neuralAppearanceContractMatches(cached, differentiableRecomputed));
    }
    gContractResults[11] = uint(
        neuralAppearancePrefixPathsMatch(context, differentiableContext));
    gContractResults[12] = uint(
        neuralAppearanceFramePathsMatch(context.frames[0], differentiableContext.frames[0]) &&
        neuralAppearanceFramePathsMatch(context.frames[1], differentiableContext.frames[1]));

    NeuralAppearanceFixture fixture = neuralAppearanceFixture(materialUv);
    gContractResults[13] = uint(
        all(isfinite(fixture.shadingNormal)) && abs(fixture.shadingNormal.y - 1.0f) > 1.0e-4f);
    gContractResults[14] = uint(
        fixture.anisotropyStrength > 0.0f && all(isfinite(fixture.anisotropyTangent)) &&
        abs(length(fixture.anisotropyTangent) - 1.0f) <= 2.0e-4f);

    bool allPassed = true;
    for (uint flag = 0u; flag < 15u; ++flag)
    {
        allPassed = allPassed && gContractResults[flag] != 0u;
    }
    gContractResults[15] = uint(allPassed);
}
