module raygen;
import common;
import test.rt.materialTextureIdsRt.abi;

[shader("raygeneration")]
void rgMain()
{
    uint2 pixel = DispatchRaysIndex().xy;
    uint baseColorTextureId = materialTextureId(gMaterialTextureIds.textureIdPairs0,
                                                gMaterialTextureIds.textureIdPairs1, MaterialTextureSlot.baseColor);

    float4 sampled = gSceneTextures[baseColorTextureId].SampleLevel(float2(0.5f, 0.5f), 0.0f);
    outputImage[int2(pixel)] = sampled;
}
