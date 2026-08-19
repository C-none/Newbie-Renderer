module layout;

public static const uint kNeuralAppearanceParameterScalarCount = 3588u;
public static const uint kNeuralAppearanceParameterChunkCount = 897u;
// Upper bound for the per-step batch. The active batch is a host push constant,
// so this only fixes the sample buffer capacity and the shader-side clamp.
public static const uint kNeuralAppearanceBatchSize = 512u;
public static const uint kNeuralAppearanceTrainingSchemaMagic = 0x4e415433u;

// V3 deployment ABI. The optimizer owns FP32 master parameters in the float4
// buffer below, while all inference and differentiable affine reads use the
// tightly specified FP16 row-major mirror described by the layer table. Keep
// these byte offsets in sync with the portable .nart writer/reader;
// TrainingOptimal data is never described here because it is device-local and
// opaque.
public static const uint kNeuralAppearanceInferenceParameterBytes = 7360u;
public static const uint kNeuralAppearanceInferenceScalarBytes = 2u;

// Eight-affine topology driving the encode / frame / appearance segments:
// E1(12->32), E2(32->32), E3(32->8), F(8->6), S(8->32), D(8->32, no bias),
// H(32->32), O(32->6). Material parameters replace the V2 latent texture, and
// the decoder emits a diffuse and a specular lobe instead of one specular lobe.
public static const uint kNeuralAppearanceAffineLayerCount = 8u;

public static const uint kNeuralAppearanceMaterialInputCount = 12u;
public static const uint kNeuralAppearanceLatentCount = 8u;
public static const uint kNeuralAppearanceFrameValueCount = 6u;
public static const uint kNeuralAppearanceDirectionFeatureCount = 8u;
public static const uint kNeuralAppearanceHiddenCount = 32u;
public static const uint kNeuralAppearanceOutputCount = 6u;

public struct NeuralAppearanceAffineLayer
{
    public uint inputCount;
    public uint outputCount;
    public uint weightByteOffset;
    public uint biasByteOffset;
    public uint rowStrideBytes;
    public uint weightScalarOffset;
    public uint biasScalarOffset;
    public bool hasBias;

    public __init(
        uint inputs, uint outputs, uint weightBytes, uint biasBytes, uint strideBytes,
        uint weightScalars, uint biasScalars, bool bias)
    {
        inputCount = inputs;
        outputCount = outputs;
        weightByteOffset = weightBytes;
        biasByteOffset = biasBytes;
        rowStrideBytes = strideBytes;
        weightScalarOffset = weightScalars;
        biasScalarOffset = biasScalars;
        hasBias = bias;
    }

    public uint weightScalarCount() { return inputCount * outputCount; }
    public uint weightByteCount() { return outputCount * rowStrideBytes; }
    public uint biasByteCount() { return hasBias ? outputCount * kNeuralAppearanceInferenceScalarBytes : 0u; }
};

// Weight blocks stay 64-byte aligned so every CooperativeVector matrix starts on
// the same boundary the V2 blob used. Bias blocks follow their weight block and
// the remaining gaps are the portable padding ranges asserted by the contract.
public NeuralAppearanceAffineLayer neuralAppearanceAffineLayer(uint layerIndex)
{
    switch (layerIndex)
    {
    case 0u:
        return NeuralAppearanceAffineLayer(12u, 32u, 0u, 768u, 24u, 0u, 384u, true);
    case 1u:
        return NeuralAppearanceAffineLayer(32u, 32u, 832u, 2880u, 64u, 416u, 1440u, true);
    case 2u:
        return NeuralAppearanceAffineLayer(32u, 8u, 2944u, 3456u, 64u, 1472u, 1728u, true);
    case 3u:
        return NeuralAppearanceAffineLayer(8u, 6u, 3520u, 3648u, 16u, 1736u, 1784u, true);
    case 4u:
        return NeuralAppearanceAffineLayer(8u, 32u, 3712u, 4224u, 16u, 1790u, 2046u, true);
    case 5u:
        return NeuralAppearanceAffineLayer(8u, 32u, 4288u, 0u, 16u, 2078u, 0u, false);
    case 6u:
        return NeuralAppearanceAffineLayer(32u, 32u, 4800u, 6848u, 64u, 2334u, 3358u, true);
    default:
        return NeuralAppearanceAffineLayer(32u, 6u, 6912u, 7296u, 64u, 3390u, 3582u, true);
    }
}

public static const uint kNeuralAppearanceEncodeFirstLayer = 0u;
public static const uint kNeuralAppearanceEncodeSecondLayer = 1u;
public static const uint kNeuralAppearanceEncodeThirdLayer = 2u;
public static const uint kNeuralAppearanceFrameLayer = 3u;
public static const uint kNeuralAppearanceSpatialLayer = 4u;
public static const uint kNeuralAppearanceDirectionLayer = 5u;
public static const uint kNeuralAppearanceHiddenLayer = 6u;
public static const uint kNeuralAppearanceOutputLayer = 7u;

public uint neuralAppearanceInferenceByteOffset(uint scalarIndex)
{
    for (uint layerIndex = 0u; layerIndex < kNeuralAppearanceAffineLayerCount; ++layerIndex)
    {
        NeuralAppearanceAffineLayer layer = neuralAppearanceAffineLayer(layerIndex);
        uint weightLocal = scalarIndex - layer.weightScalarOffset;
        if (scalarIndex >= layer.weightScalarOffset && weightLocal < layer.weightScalarCount())
        {
            uint row = weightLocal / layer.inputCount;
            uint column = weightLocal % layer.inputCount;
            return layer.weightByteOffset + row * layer.rowStrideBytes +
                   column * kNeuralAppearanceInferenceScalarBytes;
        }
        uint biasLocal = scalarIndex - layer.biasScalarOffset;
        if (layer.hasBias && scalarIndex >= layer.biasScalarOffset && biasLocal < layer.outputCount)
        {
            return layer.biasByteOffset + biasLocal * kNeuralAppearanceInferenceScalarBytes;
        }
    }
    return kNeuralAppearanceInferenceParameterBytes;
}

public bool neuralAppearanceParameterIsBias(uint scalarIndex)
{
    for (uint layerIndex = 0u; layerIndex < kNeuralAppearanceAffineLayerCount; ++layerIndex)
    {
        NeuralAppearanceAffineLayer layer = neuralAppearanceAffineLayer(layerIndex);
        uint biasLocal = scalarIndex - layer.biasScalarOffset;
        if (layer.hasBias && scalarIndex >= layer.biasScalarOffset && biasLocal < layer.outputCount)
        {
            return true;
        }
    }
    return false;
}

// A blob byte is portable padding when no weight row and no bias vector covers
// it. The published artifact requires every such byte to be zero.
public bool neuralAppearanceInferenceByteIsPadding(uint byteOffset)
{
    for (uint layerIndex = 0u; layerIndex < kNeuralAppearanceAffineLayerCount; ++layerIndex)
    {
        NeuralAppearanceAffineLayer layer = neuralAppearanceAffineLayer(layerIndex);
        uint weightLocal = byteOffset - layer.weightByteOffset;
        if (byteOffset >= layer.weightByteOffset && weightLocal < layer.weightByteCount())
        {
            return weightLocal % layer.rowStrideBytes >=
                   layer.inputCount * kNeuralAppearanceInferenceScalarBytes;
        }
        uint biasLocal = byteOffset - layer.biasByteOffset;
        if (layer.hasBias && byteOffset >= layer.biasByteOffset && biasLocal < layer.biasByteCount())
        {
            return false;
        }
    }
    return true;
}

public struct NeuralAppearanceMaterialInput : IDifferentiable
{
    public float values[kNeuralAppearanceMaterialInputCount] = {};

    [Differentiable]
    public __init()
    {
    }

    [Differentiable]
    public __init(
        float3 baseColor, float metallic, float roughness, float3 shadingNormal,
        float3 anisotropyTangent, float anisotropyStrength)
    {
        values[0] = baseColor.x;
        values[1] = baseColor.y;
        values[2] = baseColor.z;
        values[3] = metallic;
        values[4] = roughness;
        values[5] = shadingNormal.x;
        values[6] = shadingNormal.y;
        values[7] = shadingNormal.z;
        values[8] = anisotropyTangent.x;
        values[9] = anisotropyTangent.y;
        values[10] = anisotropyTangent.z;
        values[11] = anisotropyStrength;
    }
};

// Deliberately not IDifferentiable: a reverse-mode chain that returns this
// struct across a module boundary silently loses its gradient, so the training
// path keeps its differentiable values as plain float/float3 and only the
// runtime and quality paths use this aggregate.
public struct NeuralAppearanceLobes
{
    public float3 diffuse = float3(0.0f);
    public float3 specular = float3(0.0f);

    public __init()
    {
    }

    public __init(float3 inputDiffuse, float3 inputSpecular)
    {
        diffuse = inputDiffuse;
        specular = inputSpecular;
    }
};

public float neuralAppearanceParameterScalarRuntime(
    StructuredBuffer<float4> parameterBuffer, uint scalarIndex)
{
    return parameterBuffer[scalarIndex >> 2u][scalarIndex & 3u];
}
