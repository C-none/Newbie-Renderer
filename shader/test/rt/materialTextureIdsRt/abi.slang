module abi;
import common;

public struct MaterialTextureIdPushConstants
{
    public uint4 textureIdPairs0;
    public uint2 textureIdPairs1;
};

[[vk::push_constant]]
public MaterialTextureIdPushConstants gMaterialTextureIds;

[[vk::binding(0, 2)]]
public RWTexture2D<float4> outputImage;

public struct RayPayload
{
    public float3 color;
};
