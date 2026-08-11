module model;
import common;
import include.neuralAppearance.layout;

public struct NeuralAppearanceFrame : IDifferentiable
{
    public float3 normal = float3(0.0f, 1.0f, 0.0f);
    public float normalPadding = 0.0f;
    public float3 tangent = float3(1.0f, 0.0f, 0.0f);
    public float tangentPadding = 0.0f;
    public float3 bitangent = float3(0.0f, 0.0f, -1.0f);
    public float bitangentPadding = 0.0f;

    [Differentiable]
    public __init()
    {
    }

    [Differentiable]
    public __init(float3 inputNormal, float3 inputTangent, float3 inputBitangent)
    {
        normal = inputNormal;
        tangent = inputTangent;
        bitangent = inputBitangent;
    }
};

public struct NeuralMaterialContext : IDifferentiable
{
    public float spatialPrefix[32] = {};
    public NeuralAppearanceFrame frames[2] = {
        NeuralAppearanceFrame(),
        NeuralAppearanceFrame(),
    };

    [Differentiable]
    public __init()
    {
    }

    [Differentiable]
    public __init(
        NeuralAppearanceFrame frame0, NeuralAppearanceFrame frame1)
    {
        frames[0] = frame0;
        frames[1] = frame1;
    }
};

struct NeuralAppearanceHiddenLayer : IDifferentiable
{
    float values[32] = {};

    [Differentiable]
    __init()
    {
    }

};

struct NeuralAppearanceDirectionFeatures : IDifferentiable
{
    float values[12] = {};

    [Differentiable]
    __init()
    {
    }

    [Differentiable]
    __init(float3 light0, float3 view0, float3 light1, float3 view1)
    {
        values[0] = light0.x;
        values[1] = light0.y;
        values[2] = light0.z;
        values[3] = view0.x;
        values[4] = view0.y;
        values[5] = view0.z;
        values[6] = light1.x;
        values[7] = light1.y;
        values[8] = light1.z;
        values[9] = view1.x;
        values[10] = view1.y;
        values[11] = view1.z;
    }
};

public NeuralAppearanceLatent sampleNeuralAppearanceLatent(
    Sampler2D<float4> latentTexture0, Sampler2D<float4> latentTexture1, float2 uv)
{
    return NeuralAppearanceLatent(
        latentTexture0.SampleLevel(uv, 0.0f),
        latentTexture1.SampleLevel(uv, 0.0f));
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
    float3 normal = neuralAppearanceDifferentiableNormalize(
        float3(rawValues[0], rawValues[1], rawValues[2]), float3(0.0f, 1.0f, 0.0f));
    // The learned tangent is normalized but intentionally not projected against N.
    float3 tangent = neuralAppearanceDifferentiableNormalize(
        float3(rawValues[3], rawValues[4], rawValues[5]), float3(1.0f, 0.0f, 0.0f));
    float3 bitangentFallback = neuralAppearanceDifferentiableOrthogonal(normal);
    float3 bitangent = neuralAppearanceDifferentiableNormalize(
        cross(normal, tangent), bitangentFallback);
    return NeuralAppearanceFrame(normal, tangent, bitangent);
}

[Differentiable]
NeuralAppearanceFrame neuralAppearanceBuildFrame(
    NeuralAppearanceParameters parameters, NeuralAppearanceLatent latent, uint frameIndex)
{
    float rawValues[6] = {};
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
    return neuralAppearanceBuildFrame(rawValues);
}

NeuralAppearanceFrame neuralAppearanceBuildFrameRuntime(
    StructuredBuffer<float4> parameterBuffer, NeuralAppearanceLatent latent, uint frameIndex)
{
    float rawValues[6] = {};
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
    return neuralAppearanceBuildFrame(rawValues);
}

[Differentiable]
public NeuralMaterialContext buildNeuralMaterialContext(
    NeuralAppearanceParameters parameters, NeuralAppearanceLatent latent)
{
    NeuralMaterialContext context = NeuralMaterialContext(
        neuralAppearanceBuildFrame(parameters, latent, 0u),
        neuralAppearanceBuildFrame(parameters, latent, 1u));
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
    return context;
}

public NeuralMaterialContext buildNeuralMaterialContextRuntime(
    StructuredBuffer<float4> parameterBuffer, NeuralAppearanceLatent latent)
{
    NeuralMaterialContext context = NeuralMaterialContext(
        neuralAppearanceBuildFrameRuntime(parameterBuffer, latent, 0u),
        neuralAppearanceBuildFrameRuntime(parameterBuffer, latent, 1u));
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
    float3 light0 = neuralAppearanceDirectionInFrame(context.frames[0], lightDirection);
    float3 view0 = neuralAppearanceDirectionInFrame(context.frames[0], viewDirection);
    float3 light1 = neuralAppearanceDirectionInFrame(context.frames[1], lightDirection);
    float3 view1 = neuralAppearanceDirectionInFrame(context.frames[1], viewDirection);
    return NeuralAppearanceDirectionFeatures(light0, view0, light1, view1);
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
    NeuralAppearanceHiddenLayer secondHidden = NeuralAppearanceHiddenLayer();
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
    NeuralAppearanceHiddenLayer secondHidden = NeuralAppearanceHiddenLayer();
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
    NeuralAppearanceHiddenLayer firstHidden = NeuralAppearanceHiddenLayer();
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
    NeuralAppearanceHiddenLayer firstHidden = NeuralAppearanceHiddenLayer();
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
