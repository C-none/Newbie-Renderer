module model;
import common;
import include.neuralAppearance.layout;

// Both lobes decode with exp(logit) in the small regime and saturate smoothly at
// exp(logMaximum). The specular cap matches the finite rgba16f range the V2
// capped exponential guaranteed, and the diffuse cap stays just above 1/pi.
public static const float kNeuralAppearanceDiffuseLogMaximum = 1.0f;
public static const float kNeuralAppearanceSpecularLogMaximum = 11.0f;
public static const float kNeuralAppearanceLeakySlope = 0.01f;

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
    public float spatialPrefix[kNeuralAppearanceHiddenCount] = {};
    public NeuralAppearanceFrame frame = NeuralAppearanceFrame();

    [Differentiable]
    public __init()
    {
    }

    [Differentiable]
    public __init(NeuralAppearanceFrame inputFrame)
    {
        frame = inputFrame;
    }
};

public struct NeuralAppearanceDirectionFeatures : IDifferentiable
{
    public float values[kNeuralAppearanceDirectionFeatureCount] = {};

    [Differentiable]
    public __init()
    {
    }

    [Differentiable]
    public __init(float3 halfVector, float3 difference, float lightCosine, float viewCosine)
    {
        values[0] = halfVector.x;
        values[1] = halfVector.y;
        values[2] = halfVector.z;
        values[3] = difference.x;
        values[4] = difference.y;
        values[5] = difference.z;
        // The two geometry-frame cosines are the only inputs that survive the
        // learned rotation, so the decoder can still see the folding horizon.
        values[6] = lightCosine;
        values[7] = viewCosine;
    }
};

[Differentiable]
public float3 neuralAppearanceDifferentiableNormalize(float3 value, float3 fallback)
{
    float lengthSquared = dot(value, value);
    float valid = lengthSquared >= 1.0e-12f ? 1.0f : 0.0f;
    float3 selected = value * valid + fallback * (1.0f - valid);
    return selected * rsqrt(max(dot(selected, selected), 1.0e-12f));
}

[Differentiable]
public float neuralAppearanceLeakyRelu(float value)
{
    return value >= 0.0f ? value : kNeuralAppearanceLeakySlope * value;
}

// The residual formulation keeps a zero-initialized model at the geometry frame,
// so no hand-tuned identity weights are needed for the frame layer.
[Differentiable]
public NeuralAppearanceFrame neuralAppearanceBuildFrame(float rawValues[kNeuralAppearanceFrameValueCount])
{
    float3 normal = neuralAppearanceDifferentiableNormalize(
        float3(rawValues[0], 1.0f + rawValues[1], rawValues[2]), float3(0.0f, 1.0f, 0.0f));
    float3 tangent = neuralAppearanceDifferentiableNormalize(
        float3(1.0f + rawValues[3], rawValues[4], rawValues[5]), float3(1.0f, 0.0f, 0.0f));
    float3 bitangent = neuralAppearanceDifferentiableNormalize(
        cross(normal, tangent), float3(0.0f, 0.0f, -1.0f));
    return NeuralAppearanceFrame(normal, tangent, bitangent);
}

[Differentiable]
float3 neuralAppearanceDirectionInFrame(NeuralAppearanceFrame frame, float3 direction)
{
    return float3(dot(frame.tangent, direction), dot(frame.bitangent, direction),
                  dot(frame.normal, direction));
}

// Rusinkiewicz parameterization in the geometry frame: the half vector pins the
// specular peak, and the difference vector is the light direction expressed in
// the half-vector basis. The world tangent is the azimuth reference so the
// anisotropy tangent fed to the encoder stays in the same frame.
[Differentiable]
public NeuralAppearanceDirectionFeatures neuralAppearanceDirectionFeatures(
    NeuralMaterialContext context, float3 viewDirection, float3 lightDirection)
{
    float3 halfVector = neuralAppearanceDifferentiableNormalize(
        viewDirection + lightDirection, float3(0.0f, 1.0f, 0.0f));
    float3 halfTangent = neuralAppearanceDifferentiableNormalize(
        float3(1.0f, 0.0f, 0.0f) - halfVector * halfVector.x, float3(0.0f, 0.0f, 1.0f));
    float3 halfBitangent = cross(halfVector, halfTangent);
    float3 difference = float3(dot(lightDirection, halfTangent),
                               dot(lightDirection, halfBitangent),
                               dot(lightDirection, halfVector));
    return NeuralAppearanceDirectionFeatures(
        neuralAppearanceDirectionInFrame(context.frame, halfVector),
        neuralAppearanceDirectionInFrame(context.frame, difference),
        lightDirection.y, viewDirection.y);
}

[Differentiable]
public float3 neuralAppearanceDecodeLobe(float3 logits, float logMaximum)
{
    float3 excess = min(float3(logMaximum) - logits, float3(80.0f));
    return exp(logMaximum) / (float3(1.0f) + exp(excess));
}

// The aggregate form is runtime only. Training decodes each lobe separately so
// the reverse-mode chain never carries a struct across a module boundary.
public NeuralAppearanceLobes neuralAppearanceDecodeLobes(float logits[kNeuralAppearanceOutputCount])
{
    return NeuralAppearanceLobes(
        neuralAppearanceDecodeLobe(float3(logits[0], logits[1], logits[2]),
                                   kNeuralAppearanceDiffuseLogMaximum),
        neuralAppearanceDecodeLobe(float3(logits[3], logits[4], logits[5]),
                                   kNeuralAppearanceSpecularLogMaximum));
}

// -----------------------------------------------------------------------------
// FP32 master evaluation
// -----------------------------------------------------------------------------
//
// This path exists so the published quality gate can compare the FP16 mirror
// against the optimizer's master parameters. It is not used for deployment.

float[Outputs] neuralAppearanceAffineRuntime<let Inputs : int, let Outputs : int>(
    StructuredBuffer<float4> parameterBuffer, uint layerIndex, float inputs[Inputs])
{
    NeuralAppearanceAffineLayer layer = neuralAppearanceAffineLayer(layerIndex);
    float result[Outputs];
    for (uint output = 0u; output < uint(Outputs); ++output)
    {
        float value = layer.hasBias
            ? neuralAppearanceParameterScalarRuntime(parameterBuffer, layer.biasScalarOffset + output)
            : 0.0f;
        for (uint input = 0u; input < uint(Inputs); ++input)
        {
            value += neuralAppearanceParameterScalarRuntime(
                         parameterBuffer, layer.weightScalarOffset + output * uint(Inputs) + input) *
                     inputs[input];
        }
        result[output] = value;
    }
    return result;
}

float[N] neuralAppearanceLeakyReluArray<let N : int>(float values[N])
{
    float result[N];
    for (uint index = 0u; index < uint(N); ++index)
    {
        result[index] = neuralAppearanceLeakyRelu(values[index]);
    }
    return result;
}

public float[kNeuralAppearanceLatentCount] encodeNeuralAppearanceLatentRuntime(
    StructuredBuffer<float4> parameterBuffer, NeuralAppearanceMaterialInput material)
{
    float first[kNeuralAppearanceHiddenCount] = neuralAppearanceLeakyReluArray(
        neuralAppearanceAffineRuntime<int(kNeuralAppearanceMaterialInputCount), int(kNeuralAppearanceHiddenCount)>(
            parameterBuffer, kNeuralAppearanceEncodeFirstLayer, material.values));
    float second[kNeuralAppearanceHiddenCount] = neuralAppearanceLeakyReluArray(
        neuralAppearanceAffineRuntime<int(kNeuralAppearanceHiddenCount), int(kNeuralAppearanceHiddenCount)>(
            parameterBuffer, kNeuralAppearanceEncodeSecondLayer, first));
    return neuralAppearanceAffineRuntime<int(kNeuralAppearanceHiddenCount), int(kNeuralAppearanceLatentCount)>(
        parameterBuffer, kNeuralAppearanceEncodeThirdLayer, second);
}

public NeuralMaterialContext buildNeuralMaterialContextRuntime(
    StructuredBuffer<float4> parameterBuffer, float latent[kNeuralAppearanceLatentCount])
{
    NeuralMaterialContext context = NeuralMaterialContext(neuralAppearanceBuildFrame(
        neuralAppearanceAffineRuntime<int(kNeuralAppearanceLatentCount), int(kNeuralAppearanceFrameValueCount)>(
            parameterBuffer, kNeuralAppearanceFrameLayer, latent)));
    context.spatialPrefix =
        neuralAppearanceAffineRuntime<int(kNeuralAppearanceLatentCount), int(kNeuralAppearanceHiddenCount)>(
            parameterBuffer, kNeuralAppearanceSpatialLayer, latent);
    return context;
}

public NeuralAppearanceLobes evaluateNeuralAppearanceRuntime(
    StructuredBuffer<float4> parameterBuffer, NeuralMaterialContext context,
    float3 viewDirection, float3 lightDirection)
{
    NeuralAppearanceDirectionFeatures directionFeatures =
        neuralAppearanceDirectionFeatures(context, viewDirection, lightDirection);
    float directional[kNeuralAppearanceHiddenCount] =
        neuralAppearanceAffineRuntime<int(kNeuralAppearanceDirectionFeatureCount), int(kNeuralAppearanceHiddenCount)>(
            parameterBuffer, kNeuralAppearanceDirectionLayer, directionFeatures.values);
    float firstHidden[kNeuralAppearanceHiddenCount];
    for (uint index = 0u; index < kNeuralAppearanceHiddenCount; ++index)
    {
        firstHidden[index] = neuralAppearanceLeakyRelu(context.spatialPrefix[index] + directional[index]);
    }
    float secondHidden[kNeuralAppearanceHiddenCount] = neuralAppearanceLeakyReluArray(
        neuralAppearanceAffineRuntime<int(kNeuralAppearanceHiddenCount), int(kNeuralAppearanceHiddenCount)>(
            parameterBuffer, kNeuralAppearanceHiddenLayer, firstHidden));
    return neuralAppearanceDecodeLobes(
        neuralAppearanceAffineRuntime<int(kNeuralAppearanceHiddenCount), int(kNeuralAppearanceOutputCount)>(
            parameterBuffer, kNeuralAppearanceOutputLayer, secondHidden));
}

public NeuralAppearanceLobes evaluateNeuralAppearanceRecomputedRuntime(
    StructuredBuffer<float4> parameterBuffer, NeuralAppearanceMaterialInput material,
    float3 viewDirection, float3 lightDirection)
{
    float latent[kNeuralAppearanceLatentCount] =
        encodeNeuralAppearanceLatentRuntime(parameterBuffer, material);
    return evaluateNeuralAppearanceRuntime(
        parameterBuffer, buildNeuralMaterialContextRuntime(parameterBuffer, latent),
        viewDirection, lightDirection);
}

// -----------------------------------------------------------------------------
// V3 FP16 CooperativeVector inference ABI
// -----------------------------------------------------------------------------
//
// This path deliberately has no stage entry point. It is shared by training, the
// preview viewer, and ray tracing. The portable parameter blob is always
// E1/E2/E3/F/S/D/H/O in the exact byte layout declared in layout.slang. Do not
// use a TrainingOptimal matrix here: that layout is device-local gradient
// scratch.

public CoopVec<half, N> neuralAppearanceCoopVecFromFloatArray<let N : int>(float values[N])
{
    CoopVec<half, N> result;
    [ForceUnroll]
    for (int index = 0; index < N; ++index)
    {
        result[index] = half(values[index]);
    }
    return result;
}

public float[N] neuralAppearanceFloatArrayFromCoopVec<let N : int>(CoopVec<half, N> value)
{
    float result[N];
    [ForceUnroll]
    for (int index = 0; index < N; ++index)
    {
        result[index] = float(value[index]);
    }
    return result;
}

CoopVec<half, Outputs> neuralAppearanceCoopLinear<let Inputs : int, let Outputs : int>(
    CoopVec<half, Inputs> input, ByteAddressBuffer parameters, uint layerIndex)
{
    NeuralAppearanceAffineLayer layer = neuralAppearanceAffineLayer(layerIndex);
    return coopVecMatMulAdd<half, Outputs>(
        input,
        CoopVecComponentType.Float16,
        parameters,
        int(layer.weightByteOffset),
        CoopVecComponentType.Float16,
        parameters,
        int(layer.biasByteOffset),
        CoopVecComponentType.Float16,
        CoopVecMatrixLayout.RowMajor,
        false,
        int(layer.rowStrideBytes));
}

CoopVec<half, Outputs> neuralAppearanceCoopLinearNoBias<let Inputs : int, let Outputs : int>(
    CoopVec<half, Inputs> input, ByteAddressBuffer parameters, uint layerIndex)
{
    NeuralAppearanceAffineLayer layer = neuralAppearanceAffineLayer(layerIndex);
    return coopVecMatMul<half, Outputs>(
        input,
        CoopVecComponentType.Float16,
        parameters,
        int(layer.weightByteOffset),
        CoopVecComponentType.Float16,
        CoopVecMatrixLayout.RowMajor,
        false,
        int(layer.rowStrideBytes));
}

CoopVec<half, N> neuralAppearanceCoopLeakyRelu<let N : int>(CoopVec<half, N> value)
{
    float values[N] = neuralAppearanceFloatArrayFromCoopVec(value);
    [ForceUnroll]
    for (int index = 0; index < N; ++index)
    {
        values[index] = neuralAppearanceLeakyRelu(values[index]);
    }
    return neuralAppearanceCoopVecFromFloatArray(values);
}

public CoopVec<half, 8> encodeNeuralAppearanceLatentCoop(
    ByteAddressBuffer parameters, NeuralAppearanceMaterialInput material)
{
    CoopVec<half, 12> materialVector = neuralAppearanceCoopVecFromFloatArray(material.values);
    CoopVec<half, 32> first = neuralAppearanceCoopLeakyRelu(
        neuralAppearanceCoopLinear<12, 32>(materialVector, parameters, kNeuralAppearanceEncodeFirstLayer));
    CoopVec<half, 32> second = neuralAppearanceCoopLeakyRelu(
        neuralAppearanceCoopLinear<32, 32>(first, parameters, kNeuralAppearanceEncodeSecondLayer));
    return neuralAppearanceCoopLinear<32, 8>(second, parameters, kNeuralAppearanceEncodeThirdLayer);
}

public NeuralMaterialContext buildNeuralMaterialContextCoop(
    ByteAddressBuffer parameters, CoopVec<half, 8> latent)
{
    NeuralMaterialContext context = NeuralMaterialContext(neuralAppearanceBuildFrame(
        neuralAppearanceFloatArrayFromCoopVec(
            neuralAppearanceCoopLinear<8, 6>(latent, parameters, kNeuralAppearanceFrameLayer))));
    context.spatialPrefix = neuralAppearanceFloatArrayFromCoopVec(
        neuralAppearanceCoopLinear<8, 32>(latent, parameters, kNeuralAppearanceSpatialLayer));
    return context;
}

public NeuralAppearanceLobes evaluateNeuralAppearanceCoop(
    ByteAddressBuffer parameters, NeuralMaterialContext context,
    float3 viewDirection, float3 lightDirection)
{
    NeuralAppearanceDirectionFeatures directionFeatures =
        neuralAppearanceDirectionFeatures(context, viewDirection, lightDirection);
    CoopVec<half, 32> directional = neuralAppearanceCoopLinearNoBias<8, 32>(
        neuralAppearanceCoopVecFromFloatArray(directionFeatures.values), parameters,
        kNeuralAppearanceDirectionLayer);
    float directionalValues[32] = neuralAppearanceFloatArrayFromCoopVec(directional);
    float firstHiddenValues[32];
    [ForceUnroll]
    for (int index = 0; index < 32; ++index)
    {
        firstHiddenValues[index] =
            neuralAppearanceLeakyRelu(context.spatialPrefix[index] + directionalValues[index]);
    }
    CoopVec<half, 32> secondHidden = neuralAppearanceCoopLeakyRelu(
        neuralAppearanceCoopLinear<32, 32>(
            neuralAppearanceCoopVecFromFloatArray(firstHiddenValues), parameters,
            kNeuralAppearanceHiddenLayer));
    return neuralAppearanceDecodeLobes(neuralAppearanceFloatArrayFromCoopVec(
        neuralAppearanceCoopLinear<32, 6>(secondHidden, parameters, kNeuralAppearanceOutputLayer)));
}

public NeuralAppearanceLobes evaluateNeuralAppearanceRuntime(
    ByteAddressBuffer parameters, NeuralAppearanceMaterialInput material,
    float3 viewDirection, float3 lightDirection)
{
    CoopVec<half, 8> latent = encodeNeuralAppearanceLatentCoop(parameters, material);
    return evaluateNeuralAppearanceCoop(
        parameters, buildNeuralMaterialContextCoop(parameters, latent), viewDirection, lightDirection);
}
