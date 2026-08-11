module model;
import common;
import include.neuralAppearance.layout;

public struct NeuralAppearanceFrame : IDifferentiable
{
    public float3 normal;
    public float normalPadding;
    public float3 tangent;
    public float tangentPadding;
    public float3 bitangent;
    public float bitangentPadding;
};

public struct NeuralMaterialContext : IDifferentiable
{
    public float spatialPrefix[32];
    public NeuralAppearanceFrame frames[2];
};

struct NeuralAppearanceHiddenLayer : IDifferentiable
{
    float values[32];
};

struct NeuralAppearanceDirectionFeatures : IDifferentiable
{
    float values[12];
};

public NeuralAppearanceLatent sampleNeuralAppearanceLatent(
    Sampler2D<float4> latentTexture0, Sampler2D<float4> latentTexture1, float2 uv)
{
    NeuralAppearanceLatent latent;
    latent.chunks[0] = latentTexture0.SampleLevel(uv, 0.0f);
    latent.chunks[1] = latentTexture1.SampleLevel(uv, 0.0f);
    return latent;
}

[Differentiable]
float3 neuralAppearanceDifferentiableNormalize(float3 value, float3 fallback)
{
    float lengthSquared = dot(value, value);
    float valid = lengthSquared >= 1.0e-12f ? 1.0f : 0.0f;
    float3 selected = value * valid + fallback * (1.0f - valid);
    return selected * rsqrt(max(dot(selected, selected), 1.0e-12f));
}

[Differentiable]
float3 neuralAppearanceDifferentiableOrthogonal(float3 normal)
{
    float useZ = abs(normal.z) < 0.999f ? 1.0f : 0.0f;
    float3 axis = float3(0.0f, 1.0f - useZ, useZ);
    float3 tangent = cross(axis, normal);
    return tangent * rsqrt(max(dot(tangent, tangent), 1.0e-12f));
}

[Differentiable]
NeuralAppearanceFrame neuralAppearanceBuildFrame(float rawValues[6])
{
    NeuralAppearanceFrame frame;
    frame.normal = neuralAppearanceDifferentiableNormalize(
        float3(rawValues[0], rawValues[1], rawValues[2]), float3(0.0f, 1.0f, 0.0f));
    frame.normalPadding = 0.0f;
    // The learned tangent is normalized but intentionally not projected against N.
    frame.tangent = neuralAppearanceDifferentiableNormalize(
        float3(rawValues[3], rawValues[4], rawValues[5]), float3(1.0f, 0.0f, 0.0f));
    frame.tangentPadding = 0.0f;
    float3 bitangentFallback = neuralAppearanceDifferentiableOrthogonal(frame.normal);
    frame.bitangent = neuralAppearanceDifferentiableNormalize(
        cross(frame.normal, frame.tangent), bitangentFallback);
    frame.bitangentPadding = 0.0f;
    return frame;
}

[Differentiable]
public NeuralMaterialContext buildNeuralMaterialContext(
    NeuralAppearanceParameters parameters, NeuralAppearanceLatent latent)
{
    NeuralMaterialContext context;
    for (uint output = 0u; output < 32u; ++output)
    {
        float activation = neuralAppearanceParameterScalar(
            parameters, kNeuralAppearanceFirstBiasOffset + output);
        for (uint input = 0u; input < 8u; ++input)
        {
            activation += neuralAppearanceParameterScalar(
                parameters, kNeuralAppearanceSpatialWeightOffset + output * 8u + input) *
                latent.chunks[input >> 2u][input & 3u];
        }
        context.spatialPrefix[output] = activation;
    }

    for (uint frameIndex = 0u; frameIndex < 2u; ++frameIndex)
    {
        float rawValues[6];
        for (uint output = 0u; output < 6u; ++output)
        {
            uint row = frameIndex * 6u + output;
            float value = neuralAppearanceParameterScalar(
                parameters, kNeuralAppearanceFrameBiasOffset + row);
            for (uint input = 0u; input < 8u; ++input)
            {
                value += neuralAppearanceParameterScalar(
                    parameters, kNeuralAppearanceFrameWeightOffset + row * 8u + input) *
                    latent.chunks[input >> 2u][input & 3u];
            }
            rawValues[output] = value;
        }
        context.frames[frameIndex] = neuralAppearanceBuildFrame(rawValues);
    }
    return context;
}

public NeuralMaterialContext buildNeuralMaterialContextRuntime(
    StructuredBuffer<float4> parameterBuffer, NeuralAppearanceLatent latent)
{
    NeuralMaterialContext context;
    for (uint output = 0u; output < 32u; ++output)
    {
        float activation = neuralAppearanceParameterScalarRuntime(
            parameterBuffer, kNeuralAppearanceFirstBiasOffset + output);
        for (uint input = 0u; input < 8u; ++input)
        {
            activation += neuralAppearanceParameterScalarRuntime(
                parameterBuffer, kNeuralAppearanceSpatialWeightOffset + output * 8u + input) *
                latent.chunks[input >> 2u][input & 3u];
        }
        context.spatialPrefix[output] = activation;
    }

    for (uint frameIndex = 0u; frameIndex < 2u; ++frameIndex)
    {
        float rawValues[6];
        for (uint output = 0u; output < 6u; ++output)
        {
            uint row = frameIndex * 6u + output;
            float value = neuralAppearanceParameterScalarRuntime(
                parameterBuffer, kNeuralAppearanceFrameBiasOffset + row);
            for (uint input = 0u; input < 8u; ++input)
            {
                value += neuralAppearanceParameterScalarRuntime(
                    parameterBuffer, kNeuralAppearanceFrameWeightOffset + row * 8u + input) *
                    latent.chunks[input >> 2u][input & 3u];
            }
            rawValues[output] = value;
        }
        context.frames[frameIndex] = neuralAppearanceBuildFrame(rawValues);
    }
    return context;
}

[Differentiable]
float3 neuralAppearanceDirectionInFrame(NeuralAppearanceFrame frame, float3 direction)
{
    float3 normalizedDirection = neuralAppearanceDifferentiableNormalize(
        direction, float3(0.0f, 1.0f, 0.0f));
    return float3(dot(frame.tangent, normalizedDirection),
                  dot(frame.bitangent, normalizedDirection),
                  dot(frame.normal, normalizedDirection));
}

[Differentiable]
NeuralAppearanceDirectionFeatures neuralAppearanceDirectionFeatures(
    NeuralMaterialContext context, float3 viewDirection, float3 lightDirection)
{
    NeuralAppearanceDirectionFeatures features;
    for (uint frameIndex = 0u; frameIndex < 2u; ++frameIndex)
    {
        float3 localLight = neuralAppearanceDirectionInFrame(context.frames[frameIndex], lightDirection);
        float3 localView = neuralAppearanceDirectionInFrame(context.frames[frameIndex], viewDirection);
        uint offset = frameIndex * 6u;
        features.values[offset + 0u] = localLight.x;
        features.values[offset + 1u] = localLight.y;
        features.values[offset + 2u] = localLight.z;
        features.values[offset + 3u] = localView.x;
        features.values[offset + 4u] = localView.y;
        features.values[offset + 5u] = localView.z;
    }
    return features;
}

[Differentiable]
float3 neuralAppearanceDecodeOutput(float3 logits)
{
    // Capping the exponent argument at 11 keeps decoded RGB below the finite
    // rgba16f range while retaining exp(logits - 3) throughout that range.
    return exp(min(logits - float3(3.0f), float3(11.0f)));
}

[Differentiable]
float3 neuralAppearanceFinishDecode(
    NeuralAppearanceParameters parameters, NeuralAppearanceHiddenLayer firstHidden)
{
    NeuralAppearanceHiddenLayer secondHidden;
    for (uint output = 0u; output < 32u; ++output)
    {
        float activation = neuralAppearanceParameterScalar(
            parameters, kNeuralAppearanceSecondBiasOffset + output);
        for (uint input = 0u; input < 32u; ++input)
        {
            activation += neuralAppearanceParameterScalar(
                parameters, kNeuralAppearanceSecondWeightOffset + output * 32u + input) *
                firstHidden.values[input];
        }
        secondHidden.values[output] = max(activation, 0.0f);
    }

    float3 logits;
    for (uint channel = 0u; channel < 3u; ++channel)
    {
        float value = neuralAppearanceParameterScalar(
            parameters, kNeuralAppearanceOutputBiasOffset + channel);
        for (uint input = 0u; input < 32u; ++input)
        {
            value += neuralAppearanceParameterScalar(
                parameters, kNeuralAppearanceOutputWeightOffset + channel * 32u + input) *
                secondHidden.values[input];
        }
        logits[channel] = value;
    }

    return neuralAppearanceDecodeOutput(logits);
}

float3 neuralAppearanceFinishDecodeRuntime(
    StructuredBuffer<float4> parameterBuffer, NeuralAppearanceHiddenLayer firstHidden)
{
    NeuralAppearanceHiddenLayer secondHidden;
    for (uint output = 0u; output < 32u; ++output)
    {
        float activation = neuralAppearanceParameterScalarRuntime(
            parameterBuffer, kNeuralAppearanceSecondBiasOffset + output);
        for (uint input = 0u; input < 32u; ++input)
        {
            activation += neuralAppearanceParameterScalarRuntime(
                parameterBuffer, kNeuralAppearanceSecondWeightOffset + output * 32u + input) *
                firstHidden.values[input];
        }
        secondHidden.values[output] = max(activation, 0.0f);
    }

    float3 logits;
    for (uint channel = 0u; channel < 3u; ++channel)
    {
        float value = neuralAppearanceParameterScalarRuntime(
            parameterBuffer, kNeuralAppearanceOutputBiasOffset + channel);
        for (uint input = 0u; input < 32u; ++input)
        {
            value += neuralAppearanceParameterScalarRuntime(
                parameterBuffer, kNeuralAppearanceOutputWeightOffset + channel * 32u + input) *
                secondHidden.values[input];
        }
        logits[channel] = value;
    }

    return neuralAppearanceDecodeOutput(logits);
}

[Differentiable]
public float3 evaluateNeuralAppearance(
    NeuralAppearanceParameters parameters, NeuralMaterialContext context,
    float3 viewDirection, float3 lightDirection)
{
    NeuralAppearanceDirectionFeatures directionFeatures =
        neuralAppearanceDirectionFeatures(context, viewDirection, lightDirection);
    NeuralAppearanceHiddenLayer firstHidden;
    for (uint output = 0u; output < 32u; ++output)
    {
        float activation = context.spatialPrefix[output];
        for (uint input = 0u; input < 12u; ++input)
        {
            activation += neuralAppearanceParameterScalar(
                parameters, kNeuralAppearanceDirectionWeightOffset + output * 12u + input) *
                directionFeatures.values[input];
        }
        firstHidden.values[output] = max(activation, 0.0f);
    }
    return neuralAppearanceFinishDecode(parameters, firstHidden);
}

public float3 evaluateNeuralAppearanceRuntime(
    StructuredBuffer<float4> parameterBuffer, NeuralMaterialContext context,
    float3 viewDirection, float3 lightDirection)
{
    NeuralAppearanceDirectionFeatures directionFeatures =
        neuralAppearanceDirectionFeatures(context, viewDirection, lightDirection);
    NeuralAppearanceHiddenLayer firstHidden;
    for (uint output = 0u; output < 32u; ++output)
    {
        float activation = context.spatialPrefix[output];
        for (uint input = 0u; input < 12u; ++input)
        {
            activation += neuralAppearanceParameterScalarRuntime(
                parameterBuffer, kNeuralAppearanceDirectionWeightOffset + output * 12u + input) *
                directionFeatures.values[input];
        }
        firstHidden.values[output] = max(activation, 0.0f);
    }
    return neuralAppearanceFinishDecodeRuntime(parameterBuffer, firstHidden);
}

[Differentiable]
public float3 evaluateNeuralAppearanceRecomputed(
    NeuralAppearanceParameters parameters, NeuralAppearanceLatent latent,
    float3 viewDirection, float3 lightDirection)
{
    NeuralMaterialContext context = buildNeuralMaterialContext(parameters, latent);
    return evaluateNeuralAppearance(parameters, context, viewDirection, lightDirection);
}

public float3 evaluateNeuralAppearanceRecomputedRuntime(
    StructuredBuffer<float4> parameterBuffer, NeuralAppearanceLatent latent,
    float3 viewDirection, float3 lightDirection)
{
    NeuralMaterialContext context = buildNeuralMaterialContextRuntime(parameterBuffer, latent);
    return evaluateNeuralAppearanceRuntime(
        parameterBuffer, context, viewDirection, lightDirection);
}
