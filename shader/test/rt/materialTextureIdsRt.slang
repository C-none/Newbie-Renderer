module materialTextureIdsRt;
import common;

struct MaterialTextureIdPushConstants
{
    uint4 textureIdPairs0;
    uint2 textureIdPairs1;
};

[[vk::push_constant]]
MaterialTextureIdPushConstants gMaterialTextureIds;

RWTexture2D<float4> outputImage;

struct RayPayload
{
    float3 color;
};

[shader("raygeneration")]
void rgMain()
{
    uint2 pixel = DispatchRaysIndex().xy;
    uint baseColorTextureId = materialTextureId(
        gMaterialTextureIds.textureIdPairs0,
        gMaterialTextureIds.textureIdPairs1,
        MaterialTextureSlot.baseColor);

    float4 sampled = gSceneTextures[baseColorTextureId].SampleLevel(float2(0.5f, 0.5f), 0.0f);
    outputImage[int2(pixel)] = sampled;
}

[shader("miss")]
void msMain(inout RayPayload payload)
{
    payload.color = float3(0.0f, 0.0f, 0.0f);
}

[shader("closesthit")]
void chMain(inout RayPayload payload, BuiltInTriangleIntersectionAttributes attr)
{
    payload.color = float3(attr.barycentrics.x, attr.barycentrics.y, 0.0f);
}
